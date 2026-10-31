#include "include/installer.hpp"
#include "include/block_device.hpp"
#include "include/fat32.hpp"
#include <stddef.h>
#include <stdint.h>

namespace {
BlockDevice* gSource = nullptr;
BlockDevice* gTarget = nullptr;
InstallerStats gStats{};

struct PartitionContext { BlockDevice* parent; uint64_t first; uint64_t count; };
static PartitionContext gEspCtx{}, gSystemCtx{};
static BlockDevice gEsp{}, gSystem{};
static bool gHasGpt=false;

#pragma pack(push,1)
struct ProtectivePartition { uint8_t status,chsFirst[3],type,chsLast[3]; uint32_t firstLba,sectors; };
struct ProtectiveMbr { uint8_t boot[446]; ProtectivePartition part[4]; uint16_t signature; };
struct GptHeader {
    uint64_t signature; uint32_t revision,headerSize,headerCrc,reserved; uint64_t currentLba,backupLba,firstUsable,lastUsable;
    uint8_t diskGuid[16]; uint64_t entriesLba; uint32_t entryCount,entrySize,entriesCrc;
};
struct GptEntry { uint8_t typeGuid[16],uniqueGuid[16]; uint64_t firstLba,lastLba,attributes; uint16_t name[36]; };
struct Fat32BpbInstall {
    uint8_t jump[3]; char oem[8]; uint16_t bytesPerSector; uint8_t sectorsPerCluster; uint16_t reservedSectorCount;
    uint8_t fatCount; uint16_t rootEntryCount,totalSectors16; uint8_t media; uint16_t fatSize16,sectorsPerTrack,headCount;
    uint32_t hiddenSectors,totalSectors32,fatSize32; uint16_t extFlags,fsVersion; uint32_t rootCluster; uint16_t fsInfo,backupBootSector;
    uint8_t reserved[12],driveNumber,ntReserved,bootSignature; uint32_t volumeId; char volumeLabel[11],fsType[8];
};
#pragma pack(pop)

constexpr uint64_t GPT_SIG=0x5452415020494645ULL;
constexpr uint64_t ESP_SECTORS=196608; // 96 MiB; enough for the 64 MiB rootfs template + loader/kernel.
constexpr uint64_t ALIGN_SECTORS=2048;
constexpr uint64_t MIN_GPT_DISK_SECTORS=393216; // 192 MiB

static uint32_t crc32(const void* data,size_t bytes){const auto*p=static_cast<const uint8_t*>(data);uint32_t c=0xFFFFFFFFu;for(size_t i=0;i<bytes;++i){c^=p[i];for(int b=0;b<8;++b)c=(c>>1)^(0xEDB88320u&(0u-(c&1u)));}return ~c;}
static uint32_t crc32_update(uint32_t c,const void*data,size_t bytes){const auto*p=static_cast<const uint8_t*>(data);for(size_t i=0;i<bytes;++i){c^=p[i];for(int b=0;b<8;++b)c=(c>>1)^(0xEDB88320u&(0u-(c&1u)));}return c;}
static uint32_t crc32_block(BlockDevice*dev){if(!dev||!dev->read||!dev->sectorSize)return 0;alignas(4096) static uint8_t b[4096];uint32_t c=0xFFFFFFFFu;uint64_t lba=0;while(lba<dev->sectorCount){uint32_t n=sizeof(b)/dev->sectorSize;if(!n)n=1;if(n>dev->sectorCount-lba)n=(uint32_t)(dev->sectorCount-lba);if(!block_read(dev,lba,n,b))return 0;c=crc32_update(c,b,(size_t)n*dev->sectorSize);lba+=n;}return ~c;}
static size_t text_len(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
static void text_append(char*out,size_t cap,const char*s){size_t n=text_len(out),i=0;if(!cap)return;while(n+1<cap&&s&&s[i])out[n++]=s[i++];out[n]=0;}
static void text_append_hex32(char*out,size_t cap,uint32_t v){static const char h[]="0123456789ABCDEF";char b[11]={'0','x',0,0,0,0,0,0,0,0,0};for(int i=0;i<8;++i)b[2+i]=h[(v>>(28-i*4))&0xF];b[10]=0;text_append(out,cap,b);}
static void zero(void* p,size_t n){auto*b=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)b[i]=0;}
static void copy(void*d,const void*s,size_t n){auto*o=static_cast<uint8_t*>(d);auto*i=static_cast<const uint8_t*>(s);for(size_t x=0;x<n;++x)o[x]=i[x];}
static uint64_t align_up(uint64_t v,uint64_t a){return (v+a-1)&~(a-1);}

static bool part_read(BlockDevice*d,uint64_t lba,uint32_t count,void*out){auto*c=static_cast<PartitionContext*>(d->context);if(!c||lba>c->count||count>c->count-lba)return false;return block_read(c->parent,c->first+lba,count,out);}
static bool part_write(BlockDevice*d,uint64_t lba,uint32_t count,const void*in){auto*c=static_cast<PartitionContext*>(d->context);if(!c||lba>c->count||count>c->count-lba)return false;return block_write(c->parent,c->first+lba,count,in);}
static bool part_flush(BlockDevice*d){auto*c=static_cast<PartitionContext*>(d->context);return c&&block_flush(c->parent);}
static void make_part(BlockDevice&out,PartitionContext&ctx,BlockDevice*parent,uint64_t first,uint64_t count,const char*name){ctx={parent,first,count};out={name,parent->sectorSize,count,&ctx,part_read,part_write,part_flush};}

static bool guid_eq(const uint8_t*a,const uint8_t*b){for(int i=0;i<16;++i)if(a[i]!=b[i])return false;return true;}
static const uint8_t ESP_GUID[16]={0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
static const uint8_t ZERONT_GUID[16]={0x5A,0x4E,0x54,0x30,0x33,0x00,0x42,0x45,0x9A,0x31,0x5A,0x45,0x52,0x4F,0x4E,0x54};

static bool scan_gpt(){
    gHasGpt=false;if(!gTarget||gTarget->sectorSize!=512||gTarget->sectorCount<100)return false;
    alignas(4096) uint8_t sector[512]{};if(!block_read(gTarget,1,1,sector))return false;auto*h=reinterpret_cast<GptHeader*>(sector);
    if(h->signature!=GPT_SIG||h->headerSize<92||h->entrySize!=sizeof(GptEntry)||h->entryCount<2)return false;
    const uint32_t saved=h->headerCrc;h->headerCrc=0;if(crc32(h,h->headerSize)!=saved)return false;h->headerCrc=saved;
    alignas(4096) uint8_t entries[16384]{};const uint32_t bytes=h->entryCount*h->entrySize;if(bytes>sizeof(entries))return false;const uint32_t sectors=(bytes+511)/512;
    if(!block_read(gTarget,h->entriesLba,sectors,entries)||crc32(entries,bytes)!=h->entriesCrc)return false;
    uint64_t espFirst=0,espCount=0,sysFirst=0,sysCount=0;auto*e=reinterpret_cast<GptEntry*>(entries);
    for(uint32_t i=0;i<h->entryCount;++i){if(!e[i].firstLba||e[i].lastLba<e[i].firstLba)continue;uint64_t cnt=e[i].lastLba-e[i].firstLba+1;if(guid_eq(e[i].typeGuid,ESP_GUID)){espFirst=e[i].firstLba;espCount=cnt;}else if(guid_eq(e[i].typeGuid,ZERONT_GUID)){sysFirst=e[i].firstLba;sysCount=cnt;}}
    if(!espFirst||!sysFirst)return false;make_part(gEsp,gEspCtx,gTarget,espFirst,espCount,"zeroNT ESP");make_part(gSystem,gSystemCtx,gTarget,sysFirst,sysCount,"zeroNT System");gHasGpt=true;return true;
}

static void set_guid(uint8_t dst[16],uint32_t seed){for(int i=0;i<16;++i)dst[i]=static_cast<uint8_t>((seed>>(8*(i&3)))+i*29u);dst[6]=(dst[6]&0x0F)|0x40;dst[8]=(dst[8]&0x3F)|0x80;}
static void set_name(uint16_t dst[36],const char*s){for(int i=0;i<36;++i)dst[i]=0;for(int i=0;i<35&&s&&s[i];++i)dst[i]=static_cast<uint8_t>(s[i]);}

static bool create_gpt(){
    if(!gTarget||!gTarget->write||gTarget->sectorSize!=512||gTarget->sectorCount<MIN_GPT_DISK_SECTORS)return false;
    const uint64_t last=gTarget->sectorCount-1,backupEntries=last-32;
    const uint64_t espFirst=ALIGN_SECTORS,espLast=espFirst+ESP_SECTORS-1;
    const uint64_t sysFirst=align_up(espLast+1,ALIGN_SECTORS),sysLast=backupEntries-1;
    if(sysLast<=sysFirst||sysLast-sysFirst+1<gSource->sectorCount)return false;
    alignas(4096) uint8_t sector[512]{};ProtectiveMbr m{};m.part[0].type=0xEE;m.part[0].firstLba=1;m.part[0].sectors=gTarget->sectorCount-1>0xFFFFFFFFULL?0xFFFFFFFFu:static_cast<uint32_t>(gTarget->sectorCount-1);m.signature=0xAA55;copy(sector,&m,sizeof(m));if(!block_write(gTarget,0,1,sector))return false;
    alignas(4096) uint8_t entries[16384]{};auto*e=reinterpret_cast<GptEntry*>(entries);copy(e[0].typeGuid,ESP_GUID,16);set_guid(e[0].uniqueGuid,0x45535033u);e[0].firstLba=espFirst;e[0].lastLba=espLast;set_name(e[0].name,"zeroNT EFI System");copy(e[1].typeGuid,ZERONT_GUID,16);set_guid(e[1].uniqueGuid,0x5A4E5433u);e[1].firstLba=sysFirst;e[1].lastLba=sysLast;set_name(e[1].name,"zeroNT System");
    const uint32_t entriesCrc=crc32(entries,sizeof(entries));
    GptHeader h{};h.signature=GPT_SIG;h.revision=0x00010000;h.headerSize=92;h.currentLba=1;h.backupLba=last;h.firstUsable=34;h.lastUsable=backupEntries-1;set_guid(h.diskGuid,static_cast<uint32_t>(gTarget->sectorCount));h.entriesLba=2;h.entryCount=128;h.entrySize=128;h.entriesCrc=entriesCrc;h.headerCrc=0;h.headerCrc=crc32(&h,h.headerSize);zero(sector,sizeof(sector));copy(sector,&h,sizeof(h));if(!block_write(gTarget,1,1,sector)||!block_write(gTarget,2,32,entries))return false;
    GptHeader b=h;b.currentLba=last;b.backupLba=1;b.entriesLba=backupEntries;b.headerCrc=0;b.headerCrc=crc32(&b,b.headerSize);if(!block_write(gTarget,backupEntries,32,entries)){return false;}zero(sector,sizeof(sector));copy(sector,&b,sizeof(b));if(!block_write(gTarget,last,1,sector)||!block_flush(gTarget))return false;
    make_part(gEsp,gEspCtx,gTarget,espFirst,ESP_SECTORS,"zeroNT ESP");make_part(gSystem,gSystemCtx,gTarget,sysFirst,sysLast-sysFirst+1,"zeroNT System");gHasGpt=true;gStats.gptCreated=true;return true;
}

static uint32_t choose_spc(uint64_t sectors){if(sectors<532480)return 1;if(sectors<16777216)return 8;return 16;}
static bool format_fat32(BlockDevice*dev,const char label[11],uint32_t hidden){
    if(!dev||!dev->write||dev->sectorSize!=512||dev->sectorCount<65536||dev->sectorCount>0xFFFFFFFFULL)return false;
    const uint32_t total=static_cast<uint32_t>(dev->sectorCount),reserved=32,fats=2,spc=choose_spc(total);uint32_t fatSectors=1;
    for(int pass=0;pass<16;++pass){uint32_t data=total-reserved-fats*fatSectors;uint32_t clusters=data/spc;uint32_t need=((clusters+2)*4+511)/512;if(need==fatSectors)break;fatSectors=need;}
    Fat32BpbInstall b{};b.jump[0]=0xEB;b.jump[1]=0x58;b.jump[2]=0x90;const char oem[8]={'Z','E','R','O','N','T','3','3'};copy(b.oem,oem,8);b.bytesPerSector=512;b.sectorsPerCluster=static_cast<uint8_t>(spc);b.reservedSectorCount=reserved;b.fatCount=fats;b.media=0xF8;b.sectorsPerTrack=63;b.headCount=255;b.hiddenSectors=hidden;b.totalSectors32=total;b.fatSize32=fatSectors;b.rootCluster=2;b.fsInfo=1;b.backupBootSector=6;b.driveNumber=0x80;b.bootSignature=0x29;b.volumeId=0x33092026u;copy(b.volumeLabel,label,11);const char fs[8]={'F','A','T','3','2',' ',' ',' '};copy(b.fsType,fs,8);
    alignas(4096) uint8_t sec[512]{};copy(sec,&b,sizeof(b));sec[510]=0x55;sec[511]=0xAA;if(!block_write(dev,0,1,sec)||!block_write(dev,6,1,sec))return false;
    zero(sec,512);*reinterpret_cast<uint32_t*>(sec+0)=0x41615252u;*reinterpret_cast<uint32_t*>(sec+484)=0x61417272u;*reinterpret_cast<uint32_t*>(sec+488)=0xFFFFFFFFu;*reinterpret_cast<uint32_t*>(sec+492)=0xFFFFFFFFu;*reinterpret_cast<uint32_t*>(sec+508)=0xAA550000u;if(!block_write(dev,1,1,sec)||!block_write(dev,7,1,sec))return false;
    zero(sec,512);for(uint32_t f=0;f<fats;++f){const uint64_t base=reserved+uint64_t(f)*fatSectors;for(uint32_t x=0;x<fatSectors;++x)if(!block_write(dev,base+x,1,sec))return false;*reinterpret_cast<uint32_t*>(sec+0)=0x0FFFFFF8u;*reinterpret_cast<uint32_t*>(sec+4)=0xFFFFFFFFu;*reinterpret_cast<uint32_t*>(sec+8)=0x0FFFFFFFu;if(!block_write(dev,base,1,sec))return false;zero(sec,512);}
    const uint64_t firstData=reserved+uint64_t(fats)*fatSectors;for(uint32_t s=0;s<spc;++s)if(!block_write(dev,firstData+s,1,sec))return false;return block_flush(dev);
}

static bool file_exists(Fat32Volume&v,const char*path){uint8_t b=0;size_t got=0;Fat32DirEntry st{};return fat32_stat(v,path,st)||(fat32_read_file(v,path,&b,0,got)&&got==0);}
static bool read_source(Fat32Volume&src,const char*path,uint8_t*buf,size_t cap,size_t&got){got=0;return fat32_read_file(src,path,buf,cap,got);}
static bool copy_small(Fat32Volume&src,const char*sp,Fat32Volume&dst,const char*dp){static uint8_t buf[1024*1024];size_t got=0;if(!read_source(src,sp,buf,sizeof(buf),got))return false;fat32_delete_file(dst,dp);if(!fat32_create_file(dst,dp))return false;return fat32_write_file(dst,dp,buf,got);}
static void ensure_dir(Fat32Volume&v,const char*p){Fat32DirEntry st{};if(!fat32_stat(v,p,st))fat32_mkdir(v,p);}

static bool populate_esp(){
    Fat32Volume src{},esp{};if(!fat32_mount(gSource,src)||!fat32_mount(&gEsp,esp))return false;
    ensure_dir(esp,"/EFI");ensure_dir(esp,"/EFI/BOOT");ensure_dir(esp,"/SYSTEM");ensure_dir(esp,"/CONFIG");
    if(!copy_small(src,"/INSTALL/BOOTX64.EFI",esp,"/EFI/BOOT/BOOTX64.EFI"))return false;
    if(!copy_small(src,"/INSTALL/KERNEL.ELF",esp,"/SYSTEM/KERNEL.ELF"))return false;
    fat32_delete_file(esp,"/SYSTEM/ROOTFS.IMG");if(!fat32_create_file(esp,"/SYSTEM/ROOTFS.IMG"))return false;
    const uint64_t bytes=gSource->sectorCount*uint64_t(gSource->sectorSize);if(!fat32_write_file_from_block(esp,"/SYSTEM/ROOTFS.IMG",gSource,bytes))return false;
    static uint8_t kernelBuf[1024*1024];size_t kernelBytes=0;if(!read_source(src,"/INSTALL/KERNEL.ELF",kernelBuf,sizeof(kernelBuf),kernelBytes))return false;
    const uint32_t kernelCrc=crc32(kernelBuf,kernelBytes);const uint32_t rootfsCrc=crc32_block(gSource);if(!kernelCrc||!rootfsCrc)return false;
    char cfg[320]{};text_append(cfg,sizeof(cfg),"kernel=\\SYSTEM\\KERNEL.ELF\r\nrootfs=\\SYSTEM\\ROOTFS.IMG\r\nvideo=auto\r\nmode=normal\r\nregister_boot_entry=1\r\nkernel_crc32=");text_append_hex32(cfg,sizeof(cfg),kernelCrc);text_append(cfg,sizeof(cfg),"\r\nrootfs_crc32=");text_append_hex32(cfg,sizeof(cfg),rootfsCrc);text_append(cfg,sizeof(cfg),"\r\n");
    fat32_delete_file(esp,"/CONFIG/BOOT.CFG");if(!fat32_create_file(esp,"/CONFIG/BOOT.CFG")||!fat32_write_file(esp,"/CONFIG/BOOT.CFG",cfg,text_len(cfg)))return false;
    return block_flush(esp.device);
}

static bool write_marker(Fat32Volume&v){static const char marker[]="zeroNT v33.0\r\ninstalled=1\r\nlayout=gpt-esp-system\r\n";ensure_dir(v,"/SYSTEM");fat32_delete_file(v,"/SYSTEM/INSTALL.OK");if(!fat32_create_file(v,"/SYSTEM/INSTALL.OK")||!fat32_write_file(v,"/SYSTEM/INSTALL.OK",marker,sizeof(marker)-1))return false;return block_flush(v.device);}
static bool copy_template_to_system(InstallerProgressFn progress,void*ctx){if(gSystem.sectorCount<gSource->sectorCount)return false;alignas(4096) static uint8_t buffer[4096];uint64_t lba=0,total=gSource->sectorCount;if(progress)progress(0,total,ctx);while(lba<total){uint32_t n=static_cast<uint32_t>((total-lba)>8?8:(total-lba));if(!block_read(gSource,lba,n,buffer)||!block_write(&gSystem,lba,n,buffer))return false;lba+=n;gStats.copiedSectors=lba;if(progress&&((lba&0x1FFu)==0||lba==total))progress(lba,total,ctx);}return block_flush(&gSystem);}
}

bool installer_init(BlockDevice*sourceTemplate,BlockDevice*targetDisk){gSource=sourceTemplate;gTarget=targetDisk;gStats={};gStats.initialized=true;gStats.sourceReady=gSource&&gSource->read&&gSource->sectorSize==512&&gSource->sectorCount>0;gStats.targetReady=gTarget&&gTarget->read&&gTarget->write&&gTarget->sectorSize==512&&gTarget->sectorCount>0;gStats.sourceSectors=gStats.sourceReady?gSource->sectorCount:0;gStats.targetSectors=gStats.targetReady?gTarget->sectorCount:0;scan_gpt();gStats.state=installer_probe_target();return gStats.sourceReady&&gStats.targetReady;}
InstallerTargetState installer_probe_target(){if(!gTarget||!gTarget->read){gStats.state=InstallerTargetState::NoTarget;return gStats.state;}scan_gpt();BlockDevice*dev=gHasGpt?&gSystem:gTarget;Fat32Volume v{};if(!fat32_mount(dev,v)){gStats.state=InstallerTargetState::Blank;return gStats.state;}if(file_exists(v,"/SYSTEM/INSTALL.OK")){gStats.state=InstallerTargetState::Installed;return gStats.state;}if(file_exists(v,"/SYSTEM/VERSION.TXT")||file_exists(v,"/SYSTEM/USERS.DAT")||file_exists(v,"/README.TXT")){gStats.state=InstallerTargetState::ExistingZeroNT;return gStats.state;}gStats.state=InstallerTargetState::Blank;return gStats.state;}
bool installer_needs_install(){auto s=installer_probe_target();return s==InstallerTargetState::Blank||s==InstallerTargetState::NoTarget;}

bool installer_install(InstallerProgressFn progress,void*context){
    gStats.installAttempted=true;gStats.installSucceeded=false;gStats.copiedSectors=0;if(!gSource||!gTarget||!gSource->read||!gTarget->write)return false;
    if(!scan_gpt()){if(!create_gpt())return false;}
    if(!format_fat32(&gEsp,"ZERONT ESP ",static_cast<uint32_t>(gEspCtx.first)))return false;gStats.espFormatted=true;
    if(!copy_template_to_system(progress,context))return false;Fat32Volume sys{};if(!fat32_mount(&gSystem,sys)||!write_marker(sys))return false;
    if(!populate_esp())return false;gStats.bootFilesInstalled=true;
    if(!installer_verify_target())return false;gStats.installSucceeded=true;gStats.state=InstallerTargetState::Installed;if(progress)progress(gSource->sectorCount,gSource->sectorCount,context);return true;
}

bool installer_repair(){if(!scan_gpt())return false;if(!format_fat32(&gEsp,"ZERONT ESP ",static_cast<uint32_t>(gEspCtx.first)))return false;gStats.espFormatted=true;const bool ok=populate_esp();gStats.bootFilesInstalled=ok;gStats.repairSucceeded=ok;return ok;}
bool installer_upgrade(InstallerProgressFn progress,void*context){if(!scan_gpt())return false;Fat32Volume sys{};if(!fat32_mount(&gSystem,sys))return false;/* Public-beta upgrade deliberately preserves the existing system partition. Core boot files are refreshed on ESP; app/data migration remains filesystem-level. */if(!installer_repair())return false;gStats.upgradeSucceeded=true;if(progress)progress(1,1,context);return true;}

bool installer_verify_target(){gStats.verifySucceeded=false;if(!scan_gpt())return false;Fat32Volume sys{},esp{};if(!fat32_mount(&gSystem,sys)||!fat32_mount(&gEsp,esp))return false;if(!file_exists(sys,"/SYSTEM/INSTALL.OK")||!file_exists(esp,"/EFI/BOOT/BOOTX64.EFI")||!file_exists(esp,"/SYSTEM/KERNEL.ELF")||!file_exists(esp,"/SYSTEM/ROOTFS.IMG"))return false;Fat32CheckStats check{};if(!fat32_check_volume(sys,check)||!check.healthy)return false;gStats.verifySucceeded=true;return true;}
bool installer_mount_target(Fat32Volume&outVolume){if(scan_gpt())return fat32_mount(&gSystem,outVolume);if(!gTarget)return false;return fat32_mount(gTarget,outVolume);}
InstallerStats installer_stats(){return gStats;}
