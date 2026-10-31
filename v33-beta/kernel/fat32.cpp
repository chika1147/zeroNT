#include "include/fat32.hpp"

namespace {
#pragma pack(push, 1)
struct Fat32Bpb {
    uint8_t jump[3]; char oem[8]; uint16_t bytesPerSector; uint8_t sectorsPerCluster;
    uint16_t reservedSectorCount; uint8_t fatCount; uint16_t rootEntryCount;
    uint16_t totalSectors16; uint8_t media; uint16_t fatSize16; uint16_t sectorsPerTrack;
    uint16_t headCount; uint32_t hiddenSectors; uint32_t totalSectors32; uint32_t fatSize32;
    uint16_t extFlags; uint16_t fsVersion; uint32_t rootCluster; uint16_t fsInfo;
    uint16_t backupBootSector; uint8_t reserved[12]; uint8_t driveNumber; uint8_t ntReserved;
    uint8_t bootSignature; uint32_t volumeId; char volumeLabel[11]; char fsType[8];
};

struct RawDirEntry {
    uint8_t name[11]; uint8_t attr; uint8_t ntReserved; uint8_t createTimeTenth;
    uint16_t createTime; uint16_t createDate; uint16_t accessDate; uint16_t firstClusterHigh;
    uint16_t writeTime; uint16_t writeDate; uint16_t firstClusterLow; uint32_t fileSize;
};

struct RawLfnEntry {
    uint8_t order; uint16_t name1[5]; uint8_t attr; uint8_t type; uint8_t checksum;
    uint16_t name2[6]; uint16_t firstClusterLow; uint16_t name3[2];
};
#pragma pack(pop)

struct DirLocation {
    uint64_t lba;
    uint32_t offset;
    uint32_t parentCluster;
    RawDirEntry entry;
    uint8_t lfnCount;
};
struct EntryPosition { uint64_t lba; uint32_t offset; };

constexpr uint8_t ATTR_READ_ONLY = 0x01;
constexpr uint8_t ATTR_DIRECTORY = 0x10;
constexpr uint8_t ATTR_ARCHIVE = 0x20;
constexpr uint8_t ATTR_VOLUME_ID = 0x08;
constexpr uint8_t ATTR_LFN = 0x0F;
constexpr uint32_t FAT32_EOC_VALUE = 0x0FFFFFFFu;
constexpr uint32_t FAT32_EOC = 0x0FFFFFF8u;
constexpr uint32_t FAT32_BAD = 0x0FFFFFF7u;
constexpr uint32_t MAX_CHAIN_STEPS = 1024 * 1024;

alignas(4096) static uint8_t gSector[4096];
alignas(4096) static uint8_t gSector2[4096];
// Dedicated read cache for FAT sectors. Large sequential allocation/check
// operations used to issue one device read for every 32-bit FAT entry.  A
// single 512-byte sector contains 128 entries, so caching here reduces the
// common installer path by two orders of magnitude without changing on-disk
// semantics.
alignas(4096) static uint8_t gFatCache[4096];
static BlockDevice* gFatCacheDevice = nullptr;
static uint64_t gFatCacheLba = ~0ULL;
static bool gFatCacheValid = false;

static void invalidate_fat_cache() {
    gFatCacheDevice = nullptr;
    gFatCacheLba = ~0ULL;
    gFatCacheValid = false;
}

static void zero_bytes(void* ptr, size_t bytes) {
    auto* p = static_cast<uint8_t*>(ptr);
    for (size_t i=0;i<bytes;++i) p[i]=0;
}
static void copy_bytes(void* dst, const void* src, size_t bytes) {
    auto* d=static_cast<uint8_t*>(dst); const auto* s=static_cast<const uint8_t*>(src);
    for (size_t i=0;i<bytes;++i) d[i]=s[i];
}
static uint8_t upper_ascii(uint8_t c) { return (c>='a'&&c<='z') ? static_cast<uint8_t>(c-'a'+'A') : c; }
static bool is_power_of_two(uint32_t v) { return v && ((v & (v-1))==0); }
static uint32_t entry_cluster(const RawDirEntry& e) { return (static_cast<uint32_t>(e.firstClusterHigh)<<16)|e.firstClusterLow; }
static void set_entry_cluster(RawDirEntry& e, uint32_t cluster) {
    e.firstClusterHigh=static_cast<uint16_t>(cluster>>16); e.firstClusterLow=static_cast<uint16_t>(cluster);
}
static uint64_t cluster_lba(const Fat32Volume& v, uint32_t cluster) {
    return v.firstDataLba + static_cast<uint64_t>(cluster-2)*v.stats.sectorsPerCluster;
}
static uint32_t max_cluster(const Fat32Volume& v) {
    const uint64_t m=v.stats.dataClusters+1;
    return m>0x0FFFFFF6u ? 0x0FFFFFF6u : static_cast<uint32_t>(m);
}

static bool read_fat_entry(Fat32Volume& v, uint32_t cluster, uint32_t& next) {
    if (!v.device || v.stats.bytesPerSector>sizeof(gFatCache) || cluster>max_cluster(v)) return false;
    const uint64_t off=static_cast<uint64_t>(cluster)*4;
    const uint64_t lba=v.fatStartLba+off/v.stats.bytesPerSector;
    const uint32_t pos=static_cast<uint32_t>(off%v.stats.bytesPerSector);
    if (!gFatCacheValid || gFatCacheDevice!=v.device || gFatCacheLba!=lba) {
        if (!block_read(v.device,lba,1,gFatCache)) return false;
        gFatCacheDevice=v.device;
        gFatCacheLba=lba;
        gFatCacheValid=true;
    }
    next=static_cast<uint32_t>(gFatCache[pos]) |
         (static_cast<uint32_t>(gFatCache[pos+1])<<8) |
         (static_cast<uint32_t>(gFatCache[pos+2])<<16) |
         (static_cast<uint32_t>(gFatCache[pos+3])<<24);
    next &= 0x0FFFFFFFu;
    return true;
}

static bool write_fat_entry(Fat32Volume& v, uint32_t cluster, uint32_t value) {
    if (!v.device || !v.device->write || v.stats.bytesPerSector>sizeof(gSector) || cluster>max_cluster(v)) return false;
    const uint64_t off=static_cast<uint64_t>(cluster)*4;
    const uint32_t pos=static_cast<uint32_t>(off%v.stats.bytesPerSector);
    invalidate_fat_cache();
    for (uint32_t copy=0; copy<v.stats.fatCount; ++copy) {
        const uint64_t lba=v.fatStartLba + static_cast<uint64_t>(copy)*v.stats.fatSectors + off/v.stats.bytesPerSector;
        if (!block_read(v.device,lba,1,gSector)) return false;
        uint32_t old=static_cast<uint32_t>(gSector[pos]) |
                     (static_cast<uint32_t>(gSector[pos+1])<<8) |
                     (static_cast<uint32_t>(gSector[pos+2])<<16) |
                     (static_cast<uint32_t>(gSector[pos+3])<<24);
        const uint32_t merged=(old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        gSector[pos]=static_cast<uint8_t>(merged);
        gSector[pos+1]=static_cast<uint8_t>(merged>>8);
        gSector[pos+2]=static_cast<uint8_t>(merged>>16);
        gSector[pos+3]=static_cast<uint8_t>(merged>>24);
        if (!block_write(v.device,lba,1,gSector)) return false;
    }
    invalidate_fat_cache();
    return true;
}

static bool zero_cluster(Fat32Volume& v, uint32_t cluster) {
    zero_bytes(gSector,v.stats.bytesPerSector);
    const uint64_t lba=cluster_lba(v,cluster);
    for (uint32_t s=0;s<v.stats.sectorsPerCluster;++s)
        if (!block_write(v.device,lba+s,1,gSector)) return false;
    return true;
}

static bool allocate_cluster(Fat32Volume& v, uint32_t& out, bool clearData=true) {
    const uint32_t last=max_cluster(v);
    if(last<2)return false;
    uint32_t start=v.allocHint;
    if(start<2||start>last)start=2;
    // Scan forward from the last successful allocation and wrap once. This is
    // critical for large sequential files: the old implementation restarted at
    // cluster 2 for every allocation and became O(n^2).
    for(uint32_t pass=0;pass<2;++pass){
        const uint32_t begin=pass==0?start:2;
        const uint32_t end=pass==0?last:(start>2?start-1:1);
        if(begin>end)continue;
        for(uint32_t c=begin;c<=end;++c){
            uint32_t x=0;if(!read_fat_entry(v,c,x))return false;
            if(x==0){
                if(!write_fat_entry(v,c,FAT32_EOC_VALUE))return false;
                if(clearData && !zero_cluster(v,c)){write_fat_entry(v,c,0);return false;}
                out=c;
                v.allocHint=(c<last)?c+1:2;
                return true;
            }
        }
    }
    return false;
}

static bool free_chain(Fat32Volume& v, uint32_t first) {
    uint32_t c=first;
    for (uint32_t n=0;n<MAX_CHAIN_STEPS && c>=2 && c<FAT32_EOC;++n) {
        uint32_t next=FAT32_EOC_VALUE;
        if (!read_fat_entry(v,c,next)) return false;
        if (!write_fat_entry(v,c,0)) return false;
        if(v.allocHint<2||c<v.allocHint)v.allocHint=c;
        if (next==FAT32_BAD || next>=FAT32_EOC) return true;
        c=next;
    }
    return first<2;
}

static bool resize_chain(Fat32Volume& v, uint32_t& first, uint32_t required, bool clearNew=true) {
    if (required==0) {
        if (first>=2 && !free_chain(v,first)) return false;
        first=0; return true;
    }
    if (first<2) { if (!allocate_cluster(v,first,clearNew)) return false; }
    uint32_t c=first;
    for (uint32_t index=1; index<required; ++index) {
        uint32_t next=0; if (!read_fat_entry(v,c,next)) return false;
        if (next==0 || next>=FAT32_EOC || next==FAT32_BAD) {
            uint32_t fresh=0; if (!allocate_cluster(v,fresh,clearNew)) return false;
            if (!write_fat_entry(v,c,fresh)) { free_chain(v,fresh); return false; }
            next=fresh;
        }
        c=next;
    }
    uint32_t tail=0; if (!read_fat_entry(v,c,tail)) return false;
    if (tail>=2 && tail<FAT32_EOC && tail!=FAT32_BAD) {
        if (!write_fat_entry(v,c,FAT32_EOC_VALUE)) return false;
        if (!free_chain(v,tail)) return false;
    } else if (tail<FAT32_EOC) {
        if (!write_fat_entry(v,c,FAT32_EOC_VALUE)) return false;
    }
    return true;
}

static void name83_to_string(const uint8_t raw[11], char out[13]) {
    size_t n=0; for(size_t i=0;i<8&&raw[i]!=' ';++i) out[n++]=static_cast<char>(raw[i]);
    bool ext=false; for(size_t i=8;i<11;++i) if(raw[i]!=' ') ext=true;
    if(ext&&n<12) out[n++]='.';
    for(size_t i=8;i<11&&n<12;++i) if(raw[i]!=' ') out[n++]=static_cast<char>(raw[i]);
    out[n]=0;
}

static bool valid_name_char(uint8_t c) {
    if (c<=0x20 || c>=0x7F) return false;
    switch(c) { case '"': case '*': case '+': case ',': case '/': case ':': case ';': case '<': case '=': case '>': case '?': case '[': case '\\': case ']': case '|': return false; default: return true; }
}
static bool make_name83(const char* component, size_t len, uint8_t out[11]) {
    if(!component||!len) return false;
    for(int i=0;i<11;++i) out[i]=' ';
    size_t dot=len;
    for(size_t i=0;i<len;++i) if(component[i]=='.') { if(dot!=len) return false; dot=i; }
    const size_t base=dot, ext=dot<len ? len-dot-1 : 0;
    if(base==0||base>8||ext>3) return false;
    for(size_t i=0;i<base;++i){uint8_t c=static_cast<uint8_t>(component[i]); if(!valid_name_char(c))return false; out[i]=upper_ascii(c);}
    for(size_t i=0;i<ext;++i){uint8_t c=static_cast<uint8_t>(component[dot+1+i]); if(!valid_name_char(c))return false; out[8+i]=upper_ascii(c);}
    return true;
}
static bool raw_name_matches(const RawDirEntry& e,const uint8_t wanted[11]) { for(int i=0;i<11;++i) if(upper_ascii(e.name[i])!=wanted[i]) return false; return true; }
static bool is_dot_entry(const RawDirEntry& e) { return e.name[0]=='.' && (e.name[1]==' ' || e.name[1]=='.'); }
static Fat32DirEntry convert_entry(const RawDirEntry& raw) {
    Fat32DirEntry e{}; name83_to_string(raw.name,e.name); e.attributes=raw.attr;
    e.firstCluster=entry_cluster(raw); e.size=raw.fileSize; e.createDate=raw.createDate;
    e.createTime=raw.createTime; e.writeDate=raw.writeDate; e.writeTime=raw.writeTime; e.accessDate=raw.accessDate;
    return e;
}

static bool find_in_directory_loc(Fat32Volume& v,uint32_t dir,const uint8_t wanted[11],DirLocation& found);
static bool mark_deleted(Fat32Volume& v,const DirLocation& loc);

static uint8_t lfn_checksum(const uint8_t shortName[11]) {
    uint8_t sum=0;for(int i=0;i<11;++i)sum=static_cast<uint8_t>(((sum&1)?0x80:0)+(sum>>1)+shortName[i]);return sum;
}
static bool eq_name_ci_utf8(const char*a,const char*b){if(!a||!b)return false;for(;;++a,++b){uint8_t x=(uint8_t)*a,y=(uint8_t)*b;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return false;if(!x)return true;}}
static size_t utf8_to_utf16(const char*src,size_t len,uint16_t*out,size_t cap){
    size_t i=0,n=0;while(i<len&&n<cap){uint32_t cp=0;uint8_t c=(uint8_t)src[i++];if(c<0x80)cp=c;else if((c&0xE0)==0xC0&&i<len){cp=((c&0x1F)<<6)|((uint8_t)src[i++]&0x3F);}else if((c&0xF0)==0xE0&&i+1<len){cp=((c&0x0F)<<12)|(((uint8_t)src[i]&0x3F)<<6)|((uint8_t)src[i+1]&0x3F);i+=2;}else if((c&0xF8)==0xF0&&i+2<len){cp='?';i+=3;}else cp='?';if(cp==0||cp>0xFFFF)cp='?';out[n++]=(uint16_t)cp;}return n;
}
static size_t utf16_to_utf8(const uint16_t*src,size_t count,char*out,size_t cap){
    if(!out||!cap)return 0;size_t n=0;for(size_t i=0;i<count&&src[i]&&src[i]!=0xFFFF;++i){uint32_t cp=src[i];if(cp<0x80){if(n+1>=cap)break;out[n++]=(char)cp;}else if(cp<0x800){if(n+2>=cap)break;out[n++]=(char)(0xC0|(cp>>6));out[n++]=(char)(0x80|(cp&0x3F));}else{if(n+3>=cap)break;out[n++]=(char)(0xE0|(cp>>12));out[n++]=(char)(0x80|((cp>>6)&0x3F));out[n++]=(char)(0x80|(cp&0x3F));}}out[n]=0;return n;
}
static void lfn_copy_piece(const RawLfnEntry&e,uint16_t units[260]){uint8_t ord=e.order&0x1Fu;if(!ord||ord>20)return;size_t base=(ord-1)*13;for(int i=0;i<5;++i)units[base+i]=e.name1[i];for(int i=0;i<6;++i)units[base+5+i]=e.name2[i];for(int i=0;i<2;++i)units[base+11+i]=e.name3[i];}
static void fill_lfn_entry(RawLfnEntry&e,uint8_t order,bool last,uint8_t checksum,const uint16_t*units,size_t count){zero_bytes(&e,sizeof(e));e.order=(uint8_t)(order|(last?0x40:0));e.attr=ATTR_LFN;e.type=0;e.checksum=checksum;e.firstClusterLow=0;uint16_t tmp[13];size_t base=(order-1)*13;for(int i=0;i<13;++i){size_t k=base+i;tmp[i]=k<count?units[k]:(k==count?0:0xFFFF);}for(int i=0;i<5;++i)e.name1[i]=tmp[i];for(int i=0;i<6;++i)e.name2[i]=tmp[5+i];for(int i=0;i<2;++i)e.name3[i]=tmp[11+i];}
static bool short_alias_for_component(Fat32Volume&v,uint32_t dir,const char*name,size_t len,uint8_t out[11]){
    if(make_name83(name,len,out)){DirLocation x{};if(!find_in_directory_loc(v,dir,out,x))return true;}
    for(int i=0;i<11;++i)out[i]=' ';size_t dot=len;for(size_t i=0;i<len;++i)if(name[i]=='.')dot=i;size_t extStart=(dot<len)?dot+1:len;char base[7]{};size_t bn=0;for(size_t i=0;i<dot&&bn<6;++i){uint8_t c=(uint8_t)name[i];if(c<0x80&&valid_name_char(c)&&c!='.')base[bn++]=(char)upper_ascii(c);}if(!bn){base[bn++]='F';base[bn++]='I';base[bn++]='L';base[bn++]='E';}
    for(int num=1;num<=99;++num){for(int i=0;i<11;++i)out[i]=' ';size_t pos=0;for(size_t i=0;i<bn&&pos<6;++i)out[pos++]=(uint8_t)base[i];out[pos++]='~';if(num>=10&&pos<8)out[pos++]=(uint8_t)('0'+num/10);if(pos<8)out[pos++]=(uint8_t)('0'+num%10);size_t ep=0;for(size_t i=extStart;i<len&&ep<3;++i){uint8_t c=(uint8_t)name[i];if(c<0x80&&valid_name_char(c)&&c!='.')out[8+ep++]=upper_ascii(c);}DirLocation x{};if(!find_in_directory_loc(v,dir,out,x))return true;}return false;
}
static bool write_raw_position(Fat32Volume&v,const EntryPosition&p,const void*entry){if(!block_read(v.device,p.lba,1,gSector))return false;copy_bytes(gSector+p.offset,entry,32);return block_write(v.device,p.lba,1,gSector);}
static bool find_free_positions(Fat32Volume&v,uint32_t dir,uint32_t need,EntryPosition*out){
    if(!need||need>21)return false;for(int grow=0;grow<4;++grow){uint32_t c=dir,last=dir,run=0;EntryPosition temp[21]{};for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&c>=2&&c<FAT32_EOC;++chain){last=c;uint64_t first=cluster_lba(v,c);for(uint32_t s=0;s<v.stats.sectorsPerCluster;++s){if(!block_read(v.device,first+s,1,gSector))return false;uint32_t cnt=v.stats.bytesPerSector/32;for(uint32_t i=0;i<cnt;++i){auto*raw=reinterpret_cast<RawDirEntry*>(gSector+i*32);if(raw->name[0]==0x00||raw->name[0]==0xE5){if(run<need)temp[run]={first+s,i*32};++run;if(run>=need){for(uint32_t j=0;j<need;++j)out[j]=temp[j];return true;}}else run=0;}}uint32_t n=0;if(!read_fat_entry(v,c,n))return false;if(n==FAT32_BAD||n>=FAT32_EOC)break;c=n;}uint32_t fresh=0;if(!allocate_cluster(v,fresh))return false;if(!write_fat_entry(v,last,fresh)){free_chain(v,fresh);return false;}}return false;
}
static bool create_named_entry(Fat32Volume&v,uint32_t parent,const char*component,size_t len,const RawDirEntry&prototype,DirLocation&outLoc){
    if(!component||!len||len>255)return false;uint8_t alias[11];if(!short_alias_for_component(v,parent,component,len,alias))return false;uint8_t plain[11];bool shortOnly=make_name83(component,len,plain);if(shortOnly){for(int i=0;i<11;++i)if(plain[i]!=alias[i]){shortOnly=false;break;}}
    uint16_t units[260]{};size_t unitCount=utf8_to_utf16(component,len,units,255);if(!unitCount)return false;uint32_t lcnt=shortOnly?0u:(uint32_t)((unitCount+12)/13);EntryPosition pos[21]{};if(!find_free_positions(v,parent,lcnt+1,pos))return false;uint8_t sum=lfn_checksum(alias);
    for(uint32_t disk=0;disk<lcnt;++disk){uint8_t ord=(uint8_t)(lcnt-disk);RawLfnEntry le{};fill_lfn_entry(le,ord,ord==lcnt,sum,units,unitCount);if(!write_raw_position(v,pos[disk],&le))return false;}
    RawDirEntry e=prototype;for(int i=0;i<11;++i)e.name[i]=alias[i];if(!write_raw_position(v,pos[lcnt],&e))return false;outLoc={pos[lcnt].lba,pos[lcnt].offset,parent,e,(uint8_t)lcnt};return true;
}
static bool find_in_directory_name_loc(Fat32Volume&v,uint32_t dir,const char*component,size_t len,DirLocation&found,char*outName=nullptr){
    if(!component||!len)return false;uint32_t c=dir;uint16_t lfnUnits[260]{};bool haveLfn=false;uint8_t lfnSum=0,lfnCount=0;for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&c>=2&&c<FAT32_EOC;++chain){uint64_t first=cluster_lba(v,c);for(uint32_t s=0;s<v.stats.sectorsPerCluster;++s){if(!block_read(v.device,first+s,1,gSector))return false;uint32_t cnt=v.stats.bytesPerSector/32;for(uint32_t i=0;i<cnt;++i){auto*raw=reinterpret_cast<RawDirEntry*>(gSector+i*32);if(raw->name[0]==0x00)return false;if(raw->name[0]==0xE5){haveLfn=false;lfnCount=0;continue;}if(raw->attr==ATTR_LFN){auto*le=reinterpret_cast<RawLfnEntry*>(raw);uint8_t ord=le->order&0x1F;if(le->order&0x40){for(auto&u:lfnUnits)u=0xFFFF;haveLfn=true;lfnSum=le->checksum;lfnCount=ord;}if(haveLfn&&ord&&ord<=20&&le->checksum==lfnSum)lfn_copy_piece(*le,lfnUnits);else{haveLfn=false;lfnCount=0;}continue;}if(raw->attr&ATTR_VOLUME_ID){haveLfn=false;lfnCount=0;continue;}char display[256]{};if(haveLfn&&lfnSum==lfn_checksum(raw->name))utf16_to_utf8(lfnUnits,255,display,sizeof(display));if(!display[0])name83_to_string(raw->name,display);char wanted[256]{};size_t wn=len<255?len:255;for(size_t k=0;k<wn;++k)wanted[k]=component[k];wanted[wn]=0;if(eq_name_ci_utf8(display,wanted)){found={first+s,i*32,dir,*raw,lfnCount};if(outName){size_t k=0;for(;display[k]&&k<255;++k)outName[k]=display[k];outName[k]=0;}return true;}haveLfn=false;lfnCount=0;}}uint32_t next=0;if(!read_fat_entry(v,c,next))return false;if(next==FAT32_BAD||next>=FAT32_EOC)break;c=next;}return false;
}
static bool split_parent_component(Fat32Volume&v,const char*path,uint32_t&parent,char component[256],size_t&componentLen){
    if(!path)return false;parent=v.stats.rootCluster;size_t pos=0;while(path[pos]=='/'||path[pos]=='\\')++pos;if(!path[pos])return false;for(;;){size_t start=pos;while(path[pos]&&path[pos]!='/'&&path[pos]!='\\')++pos;size_t len=pos-start;while(path[pos]=='/'||path[pos]=='\\')++pos;bool last=path[pos]==0;if(!len||len>255)return false;if(last){componentLen=len;for(size_t i=0;i<len;++i)component[i]=path[start+i];component[len]=0;return true;}DirLocation e{};if(!find_in_directory_name_loc(v,parent,path+start,len,e)||!(e.entry.attr&ATTR_DIRECTORY))return false;parent=entry_cluster(e.entry);if(parent<2)return false;}}
static bool mark_deleted_with_lfn(Fat32Volume&v,const DirLocation&loc){
    uint32_t c=loc.parentCluster;EntryPosition recent[20]{};uint8_t recentCount=0;for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&c>=2&&c<FAT32_EOC;++chain){uint64_t first=cluster_lba(v,c);for(uint32_t s=0;s<v.stats.sectorsPerCluster;++s){if(!block_read(v.device,first+s,1,gSector))return false;uint32_t cnt=v.stats.bytesPerSector/32;for(uint32_t i=0;i<cnt;++i){auto*raw=reinterpret_cast<RawDirEntry*>(gSector+i*32);EntryPosition ep{first+s,i*32};if(raw->attr==ATTR_LFN&&raw->name[0]!=0xE5){if(recentCount<20)recent[recentCount++]=ep;else recentCount=0;continue;}if(ep.lba==loc.lba&&ep.offset==loc.offset){for(uint8_t j=0;j<recentCount;++j){if(!block_read(v.device,recent[j].lba,1,gSector))return false;gSector[recent[j].offset]=0xE5;if(!block_write(v.device,recent[j].lba,1,gSector))return false;}return mark_deleted(v,loc);}recentCount=0;}}uint32_t n=0;if(!read_fat_entry(v,c,n))return false;if(n>=FAT32_EOC||n==FAT32_BAD)break;c=n;}return false;
}

static bool find_in_directory_loc(Fat32Volume& v,uint32_t dir,const uint8_t wanted[11],DirLocation& found) {
    uint32_t c=dir;
    for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&c>=2&&c<FAT32_EOC;++chain){
        const uint64_t first=cluster_lba(v,c);
        for(uint32_t s=0;s<v.stats.sectorsPerCluster;++s){
            if(!block_read(v.device,first+s,1,gSector))return false;
            const uint32_t count=v.stats.bytesPerSector/sizeof(RawDirEntry);
            for(uint32_t i=0;i<count;++i){
                auto* raw=reinterpret_cast<RawDirEntry*>(gSector+i*sizeof(RawDirEntry));
                if(raw->name[0]==0x00)return false;
                if(raw->name[0]==0xE5||raw->attr==ATTR_LFN||(raw->attr&ATTR_VOLUME_ID))continue;
                if(raw_name_matches(*raw,wanted)){found={first+s,static_cast<uint32_t>(i*sizeof(RawDirEntry)),dir,*raw,0};return true;}
            }
        }
        uint32_t next=0;if(!read_fat_entry(v,c,next))return false; if(next==FAT32_BAD||next>=FAT32_EOC)break; c=next;
    }
    return false;
}
[[maybe_unused]] static bool find_in_directory(Fat32Volume& v,uint32_t dir,const uint8_t wanted[11],RawDirEntry& found){DirLocation l{};if(!find_in_directory_loc(v,dir,wanted,l))return false;found=l.entry;return true;}

[[maybe_unused]] static bool split_parent(Fat32Volume& v,const char* path,uint32_t& parent,uint8_t name[11]) {
    char component[256]{};size_t len=0;if(!split_parent_component(v,path,parent,component,len))return false;return make_name83(component,len,name);
}
static bool resolve_path_loc(Fat32Volume& v,const char* path,DirLocation& loc,bool wantDir) {
    uint32_t parent=0;char component[256]{};size_t len=0;if(!split_parent_component(v,path,parent,component,len))return false;
    if(!find_in_directory_name_loc(v,parent,component,len,loc))return false;
    if(wantDir && !(loc.entry.attr&ATTR_DIRECTORY))return false;if(!wantDir && (loc.entry.attr&ATTR_DIRECTORY))return false;return true;
}
static bool resolve_path(Fat32Volume& v,const char* path,RawDirEntry& found,bool wantDir) {
    if(path && ((path[0]=='/'&&path[1]==0)||(path[0]=='\\'&&path[1]==0)) && wantDir){zero_bytes(&found,sizeof(found));found.attr=ATTR_DIRECTORY;set_entry_cluster(found,v.stats.rootCluster);return true;}
    DirLocation l{};if(!resolve_path_loc(v,path,l,wantDir))return false;found=l.entry;return true;
}

static bool write_dir_entry(Fat32Volume& v,const DirLocation& loc,const RawDirEntry& entry) {
    if(!block_read(v.device,loc.lba,1,gSector))return false;
    copy_bytes(gSector+loc.offset,&entry,sizeof(entry));
    return block_write(v.device,loc.lba,1,gSector);
}
static bool mark_deleted(Fat32Volume& v,const DirLocation& loc) {
    if(!block_read(v.device,loc.lba,1,gSector))return false; gSector[loc.offset]=0xE5; return block_write(v.device,loc.lba,1,gSector);
}

[[maybe_unused]] static bool find_free_slot(Fat32Volume& v,uint32_t dir,DirLocation& slot) {
    uint32_t c=dir,last=dir;
    for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&c>=2&&c<FAT32_EOC;++chain){
        last=c; const uint64_t first=cluster_lba(v,c);
        for(uint32_t s=0;s<v.stats.sectorsPerCluster;++s){
            if(!block_read(v.device,first+s,1,gSector))return false;
            const uint32_t count=v.stats.bytesPerSector/sizeof(RawDirEntry);
            for(uint32_t i=0;i<count;++i){auto* raw=reinterpret_cast<RawDirEntry*>(gSector+i*sizeof(RawDirEntry));if(raw->name[0]==0x00||raw->name[0]==0xE5){slot={first+s,static_cast<uint32_t>(i*sizeof(RawDirEntry)),dir,{},0};return true;}}
        }
        uint32_t next=0;if(!read_fat_entry(v,c,next))return false;if(next==FAT32_BAD||next>=FAT32_EOC)break;c=next;
    }
    uint32_t fresh=0;if(!allocate_cluster(v,fresh))return false;if(!write_fat_entry(v,last,fresh)){free_chain(v,fresh);return false;}
    slot={cluster_lba(v,fresh),0,dir,{},0};return true;
}

static bool list_cluster_chain(Fat32Volume& v,uint32_t dir,Fat32ListCallback cb,void* ctx) {
    if(!cb)return false;uint32_t c=dir;uint16_t lfnUnits[260]{};bool have=false;uint8_t sum=0;
    for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&c>=2&&c<FAT32_EOC;++chain){uint64_t first=cluster_lba(v,c);for(uint32_t s=0;s<v.stats.sectorsPerCluster;++s){if(!block_read(v.device,first+s,1,gSector))return false;uint32_t cnt=v.stats.bytesPerSector/32;for(uint32_t i=0;i<cnt;++i){auto*raw=reinterpret_cast<RawDirEntry*>(gSector+i*32);if(raw->name[0]==0x00)return true;if(raw->name[0]==0xE5){have=false;continue;}if(raw->attr==ATTR_LFN){auto*le=reinterpret_cast<RawLfnEntry*>(raw);uint8_t ord=le->order&0x1F;if(le->order&0x40){for(auto&u:lfnUnits)u=0xFFFF;have=true;sum=le->checksum;}if(have&&ord&&ord<=20&&le->checksum==sum)lfn_copy_piece(*le,lfnUnits);else have=false;continue;}if((raw->attr&ATTR_VOLUME_ID)||is_dot_entry(*raw)){have=false;continue;}Fat32DirEntry e=convert_entry(*raw);if(have&&sum==lfn_checksum(raw->name)){char display[256]{};utf16_to_utf8(lfnUnits,255,display,sizeof(display));if(display[0]){size_t k=0;for(;display[k]&&k<255;++k)e.name[k]=display[k];e.name[k]=0;}}cb(e,ctx);have=false;}}uint32_t next=0;if(!read_fat_entry(v,c,next))return false;if(next==FAT32_BAD||next>=FAT32_EOC)return true;c=next;}return true;
}

static bool write_payload(Fat32Volume& v,uint32_t first,uint32_t offset,const void* data,size_t size) {
    if(size==0)return true;if(first<2||!data)return false;
    const uint32_t clusterBytes=v.stats.bytesPerSector*v.stats.sectorsPerCluster;
    uint32_t skip=offset/clusterBytes;uint32_t inCluster=offset%clusterBytes;uint32_t c=first;
    while(skip--){uint32_t n=0;if(!read_fat_entry(v,c,n)||n>=FAT32_EOC||n==FAT32_BAD)return false;c=n;}
    const auto* src=static_cast<const uint8_t*>(data);size_t done=0;
    while(done<size){const uint64_t firstLba=cluster_lba(v,c);uint32_t sectorIndex=inCluster/v.stats.bytesPerSector;uint32_t inSector=inCluster%v.stats.bytesPerSector;for(uint32_t s=sectorIndex;s<v.stats.sectorsPerCluster&&done<size;++s){if(!block_read(v.device,firstLba+s,1,gSector2))return false;const uint32_t avail=v.stats.bytesPerSector-inSector;const size_t take=(size-done)<avail?(size-done):avail;copy_bytes(gSector2+inSector,src+done,take);if(!block_write(v.device,firstLba+s,1,gSector2))return false;done+=take;inSector=0;}inCluster=0;if(done<size){uint32_t n=0;if(!read_fat_entry(v,c,n)||n>=FAT32_EOC||n==FAT32_BAD)return false;c=n;}}
    return true;
}

static bool directory_empty(Fat32Volume& v,uint32_t dir) {
    uint32_t c=dir;
    for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&c>=2&&c<FAT32_EOC;++chain){const uint64_t first=cluster_lba(v,c);for(uint32_t s=0;s<v.stats.sectorsPerCluster;++s){if(!block_read(v.device,first+s,1,gSector))return false;const uint32_t count=v.stats.bytesPerSector/sizeof(RawDirEntry);for(uint32_t i=0;i<count;++i){auto* raw=reinterpret_cast<RawDirEntry*>(gSector+i*sizeof(RawDirEntry));if(raw->name[0]==0x00)return true;if(raw->name[0]==0xE5||raw->attr==ATTR_LFN||(raw->attr&ATTR_VOLUME_ID)||is_dot_entry(*raw))continue;return false;}}uint32_t n=0;if(!read_fat_entry(v,c,n))return false;if(n>=FAT32_EOC||n==FAT32_BAD)return true;c=n;}return true;
}

static void make_dot_entry(RawDirEntry& e,bool dotdot,uint32_t cluster) {
    zero_bytes(&e,sizeof(e));for(int i=0;i<11;++i)e.name[i]=' ';e.name[0]='.';if(dotdot)e.name[1]='.';e.attr=ATTR_DIRECTORY;set_entry_cluster(e,cluster);
}
}

bool fat32_mount(BlockDevice* device,Fat32Volume& out) {
    out={};invalidate_fat_cache();if(!device||device->sectorSize<512||device->sectorSize>4096)return false;if(!block_read(device,0,1,gSector))return false;if(gSector[510]!=0x55||gSector[511]!=0xAA)return false;
    const auto* bpb=reinterpret_cast<const Fat32Bpb*>(gSector);if(bpb->bytesPerSector!=device->sectorSize)return false;if(!is_power_of_two(bpb->sectorsPerCluster)||!bpb->reservedSectorCount||!bpb->fatCount)return false;if(bpb->fatSize16!=0||bpb->fatSize32==0||bpb->rootCluster<2)return false;
    const uint64_t total=bpb->totalSectors16?bpb->totalSectors16:bpb->totalSectors32;if(!total||total>device->sectorCount)return false;const uint64_t firstData=static_cast<uint64_t>(bpb->reservedSectorCount)+static_cast<uint64_t>(bpb->fatCount)*bpb->fatSize32;if(firstData>=total)return false;
    out.device=device;out.fatStartLba=bpb->reservedSectorCount;out.firstDataLba=firstData;out.allocHint=2;out.stats.mounted=true;out.stats.bytesPerSector=bpb->bytesPerSector;out.stats.sectorsPerCluster=bpb->sectorsPerCluster;out.stats.reservedSectors=bpb->reservedSectorCount;out.stats.fatCount=bpb->fatCount;out.stats.fatSectors=bpb->fatSize32;out.stats.rootCluster=bpb->rootCluster;out.stats.totalSectors=total;out.stats.dataClusters=(total-firstData)/bpb->sectorsPerCluster;return true;
}

bool fat32_list_directory(Fat32Volume& v,const char* path,Fat32ListCallback cb,void* ctx) {
    if(!v.stats.mounted)return false;uint32_t c=v.stats.rootCluster;if(path&&*path&&!((path[0]=='/'||path[0]=='\\')&&path[1]==0)){RawDirEntry e{};if(!resolve_path(v,path,e,true))return false;c=entry_cluster(e);}return list_cluster_chain(v,c,cb,ctx);
}

bool fat32_read_file(Fat32Volume& v,const char* path,void* out,size_t capacity,size_t& bytesRead) {
    bytesRead=0;if(!v.stats.mounted||!path||!out)return false;RawDirEntry e{};if(!resolve_path(v,path,e,false))return false;if(e.fileSize>capacity)return false;if(e.fileSize==0)return true;uint32_t c=entry_cluster(e);if(c<2)return false;auto* dst=static_cast<uint8_t*>(out);uint32_t remain=e.fileSize;
    for(uint32_t chain=0;chain<MAX_CHAIN_STEPS&&remain&&c>=2&&c<FAT32_EOC;++chain){const uint64_t first=cluster_lba(v,c);for(uint32_t s=0;s<v.stats.sectorsPerCluster&&remain;++s){if(!block_read(v.device,first+s,1,gSector))return false;uint32_t take=remain<v.stats.bytesPerSector?remain:v.stats.bytesPerSector;copy_bytes(dst+bytesRead,gSector,take);bytesRead+=take;remain-=take;}if(!remain)break;uint32_t n=0;if(!read_fat_entry(v,c,n)||n==FAT32_BAD||n>=FAT32_EOC)return false;c=n;}return remain==0;
}

bool fat32_create_file(Fat32Volume& v,const char* path) {
    if(!v.stats.mounted||!v.device->write)return false;uint32_t parent=0;char name[256]{};size_t len=0;if(!split_parent_component(v,path,parent,name,len))return false;DirLocation existing{};if(find_in_directory_name_loc(v,parent,name,len,existing))return false;RawDirEntry e{};e.attr=ATTR_ARCHIVE;DirLocation created{};return create_named_entry(v,parent,name,len,e,created)&&block_flush(v.device);
}

bool fat32_write_file(Fat32Volume& v,const char* path,const void* data,size_t size) {
    if(!v.stats.mounted||!v.device->write||(size&& !data)||size>0xFFFFFFFFu)return false;DirLocation loc{};if(!resolve_path_loc(v,path,loc,false))return false;if(loc.entry.attr&ATTR_READ_ONLY)return false;
    uint32_t first=entry_cluster(loc.entry);const uint32_t clusterBytes=v.stats.bytesPerSector*v.stats.sectorsPerCluster;const uint32_t need=size?static_cast<uint32_t>((size+clusterBytes-1)/clusterBytes):0;if(!resize_chain(v,first,need))return false;if(size&&!write_payload(v,first,0,data,size))return false;set_entry_cluster(loc.entry,first);loc.entry.fileSize=static_cast<uint32_t>(size);if(!write_dir_entry(v,loc,loc.entry))return false;return block_flush(v.device);
}

bool fat32_append_file(Fat32Volume& v,const char* path,const void* data,size_t size) {
    if(!v.stats.mounted||!v.device->write||!data||!size)return size==0;DirLocation loc{};if(!resolve_path_loc(v,path,loc,false))return false;if(loc.entry.attr&ATTR_READ_ONLY)return false;const uint64_t newSize=static_cast<uint64_t>(loc.entry.fileSize)+size;if(newSize>0xFFFFFFFFu)return false;uint32_t first=entry_cluster(loc.entry);const uint32_t clusterBytes=v.stats.bytesPerSector*v.stats.sectorsPerCluster;const uint32_t need=static_cast<uint32_t>((newSize+clusterBytes-1)/clusterBytes);if(!resize_chain(v,first,need))return false;if(!write_payload(v,first,loc.entry.fileSize,data,size))return false;set_entry_cluster(loc.entry,first);loc.entry.fileSize=static_cast<uint32_t>(newSize);if(!write_dir_entry(v,loc,loc.entry))return false;return block_flush(v.device);
}

bool fat32_delete_file(Fat32Volume& v,const char* path) {
    if(!v.stats.mounted||!v.device->write)return false;DirLocation loc{};if(!resolve_path_loc(v,path,loc,false))return false;if(loc.entry.attr&ATTR_READ_ONLY)return false;const uint32_t first=entry_cluster(loc.entry);if(first>=2&&!free_chain(v,first))return false;if(!mark_deleted_with_lfn(v,loc))return false;return block_flush(v.device);
}

bool fat32_rename(Fat32Volume& v,const char* oldPath,const char* newPath) {
    if(!v.stats.mounted||!v.device->write)return false;DirLocation old{};
    uint32_t op=0,np=0;char on[256]{},nn[256]{};size_t ol=0,nl=0;if(!split_parent_component(v,oldPath,op,on,ol)||!split_parent_component(v,newPath,np,nn,nl)||op!=np)return false;
    if(!find_in_directory_name_loc(v,op,on,ol,old))return false;DirLocation exists{};if(find_in_directory_name_loc(v,np,nn,nl,exists))return false;RawDirEntry proto=old.entry;DirLocation fresh{};if(!create_named_entry(v,np,nn,nl,proto,fresh))return false;if(!mark_deleted_with_lfn(v,old)){mark_deleted_with_lfn(v,fresh);return false;}return block_flush(v.device);
}

bool fat32_mkdir(Fat32Volume& v,const char* path) {
    if(!v.stats.mounted||!v.device->write)return false;uint32_t parent=0;char name[256]{};size_t len=0;if(!split_parent_component(v,path,parent,name,len))return false;DirLocation existing{};if(find_in_directory_name_loc(v,parent,name,len,existing))return false;uint32_t cluster=0;if(!allocate_cluster(v,cluster))return false;
    RawDirEntry dot{},dotdot{};make_dot_entry(dot,false,cluster);make_dot_entry(dotdot,true,parent);zero_bytes(gSector,v.stats.bytesPerSector);copy_bytes(gSector,&dot,sizeof(dot));copy_bytes(gSector+sizeof(dot),&dotdot,sizeof(dotdot));if(!block_write(v.device,cluster_lba(v,cluster),1,gSector)){free_chain(v,cluster);return false;}
    RawDirEntry e{};e.attr=ATTR_DIRECTORY;set_entry_cluster(e,cluster);DirLocation made{};if(!create_named_entry(v,parent,name,len,e,made)){free_chain(v,cluster);return false;}return block_flush(v.device);
}

bool fat32_rmdir(Fat32Volume& v,const char* path) {
    if(!v.stats.mounted||!v.device->write)return false;DirLocation loc{};if(!resolve_path_loc(v,path,loc,true))return false;const uint32_t cluster=entry_cluster(loc.entry);if(cluster<2||cluster==v.stats.rootCluster)return false;if(!directory_empty(v,cluster))return false;if(!free_chain(v,cluster))return false;if(!mark_deleted_with_lfn(v,loc))return false;return block_flush(v.device);
}


bool fat32_copy_file(Fat32Volume& v,const char* sourcePath,const char* destinationPath) {
    if(!v.stats.mounted||!v.device||!v.device->write||!sourcePath||!destinationPath)return false;
    DirLocation src{};if(!resolve_path_loc(v,sourcePath,src,false))return false;
    uint32_t dp=0;char dn[256]{};size_t dnl=0;if(!split_parent_component(v,destinationPath,dp,dn,dnl))return false;
    DirLocation exists{};if(find_in_directory_name_loc(v,dp,dn,dnl,exists))return false;
    if(!fat32_create_file(v,destinationPath))return false;
    DirLocation dst{};if(!resolve_path_loc(v,destinationPath,dst,false)){fat32_delete_file(v,destinationPath);return false;}
    const uint32_t clusterBytes=v.stats.bytesPerSector*v.stats.sectorsPerCluster;
    const uint32_t need=src.entry.fileSize?static_cast<uint32_t>((src.entry.fileSize+clusterBytes-1)/clusterBytes):0;
    uint32_t dstFirst=entry_cluster(dst.entry);
    if(!resize_chain(v,dstFirst,need)){fat32_delete_file(v,destinationPath);return false;}
    uint32_t srcCluster=entry_cluster(src.entry),dstCluster=dstFirst;
    uint32_t remain=src.entry.fileSize;
    while(remain){
        if(srcCluster<2||dstCluster<2){fat32_delete_file(v,destinationPath);return false;}
        const uint64_t sl=cluster_lba(v,srcCluster),dl=cluster_lba(v,dstCluster);
        for(uint32_t sec=0;sec<v.stats.sectorsPerCluster&&remain;++sec){
            if(!block_read(v.device,sl+sec,1,gSector)){fat32_delete_file(v,destinationPath);return false;}
            if(!block_write(v.device,dl+sec,1,gSector)){fat32_delete_file(v,destinationPath);return false;}
            const uint32_t take=remain<v.stats.bytesPerSector?remain:v.stats.bytesPerSector;remain-=take;
        }
        if(!remain)break;
        uint32_t sn=0,dnxt=0;
        if(!read_fat_entry(v,srcCluster,sn)||!read_fat_entry(v,dstCluster,dnxt)||sn>=FAT32_EOC||dnxt>=FAT32_EOC||sn==FAT32_BAD||dnxt==FAT32_BAD){fat32_delete_file(v,destinationPath);return false;}
        srcCluster=sn;dstCluster=dnxt;
    }
    set_entry_cluster(dst.entry,dstFirst);dst.entry.fileSize=src.entry.fileSize;
    if(!write_dir_entry(v,dst,dst.entry)){fat32_delete_file(v,destinationPath);return false;}
    return block_flush(v.device);
}

bool fat32_move_file(Fat32Volume& v,const char* sourcePath,const char* destinationPath) {
    if(!v.stats.mounted||!sourcePath||!destinationPath)return false;
    uint32_t sp=0,dp=0;char sn[256]{},dn[256]{};size_t sl=0,dl=0;
    if(!split_parent_component(v,sourcePath,sp,sn,sl)||!split_parent_component(v,destinationPath,dp,dn,dl))return false;
    if(sp==dp)return fat32_rename(v,sourcePath,destinationPath);
    if(!fat32_copy_file(v,sourcePath,destinationPath))return false;
    if(!fat32_delete_file(v,sourcePath)){fat32_delete_file(v,destinationPath);return false;}
    return true;
}

uint64_t fat32_free_bytes(Fat32Volume& v) {
    if(!v.stats.mounted)return 0;
    uint64_t freeClusters=0;
    const uint32_t last=max_cluster(v);
    for(uint32_t c=2;c<=last;++c){uint32_t value=0;if(!read_fat_entry(v,c,value))return 0;if(value==0)++freeClusters;}
    return freeClusters*uint64_t(v.stats.bytesPerSector)*uint64_t(v.stats.sectorsPerCluster);
}

namespace {
static bool tree_path_join(char*out,size_t cap,const char*base,const char*name){
    if(!out||cap<2||!base||!name)return false;size_t n=0;while(base[n]&&n+1<cap){out[n]=base[n];++n;}out[n]=0;if(base[n])return false;
    if(n&&out[n-1]!='/'&&out[n-1]!='\\'){if(n+1>=cap)return false;out[n++]='/';out[n]=0;}
    size_t i=0;while(name[i]&&n+1<cap)out[n++]=name[i++];out[n]=0;return name[i]==0;
}
static char tree_up(char c){return (c>='a'&&c<='z')?char(c-'a'+'A'):c;}
static bool tree_prefix_ci(const char*path,const char*prefix){
    if(!path||!prefix)return false;size_t i=0;for(;prefix[i];++i){char a=path[i],b=prefix[i];if(!a)return false;if(a=='\\')a='/';if(b=='\\')b='/';if(tree_up(a)!=tree_up(b))return false;}
    if(path[i]==0)return true;return path[i]=='/'||path[i]=='\\';
}
struct CountCtx{uint32_t count;};
static void count_tree_entry(const Fat32DirEntry&,void*ctx){++static_cast<CountCtx*>(ctx)->count;}
struct PickCtx{uint32_t want;uint32_t index;bool found;Fat32DirEntry entry;};
static void pick_tree_entry(const Fat32DirEntry&e,void*ctx){auto*c=static_cast<PickCtx*>(ctx);if(c->found)return;if(c->index++==c->want){c->entry=e;c->found=true;}}
static bool copy_tree_impl(Fat32Volume&v,const char*src,const char*dst,uint32_t depth){
    if(depth>128)return false;Fat32DirEntry st{};if(!fat32_stat(v,src,st))return false;
    if(!(st.attributes&ATTR_DIRECTORY))return fat32_copy_file(v,src,dst);
    if(tree_prefix_ci(dst,src))return false;
    if(!fat32_mkdir(v,dst))return false;
    CountCtx count{};if(!fat32_list_directory(v,src,count_tree_entry,&count)){fat32_rmdir(v,dst);return false;}
    for(uint32_t i=0;i<count.count;++i){PickCtx pick{i,0,false,{}};if(!fat32_list_directory(v,src,pick_tree_entry,&pick)||!pick.found){fat32_delete_tree(v,dst);return false;}char s[1024]{},d[1024]{};if(!tree_path_join(s,sizeof(s),src,pick.entry.name)||!tree_path_join(d,sizeof(d),dst,pick.entry.name)||!copy_tree_impl(v,s,d,depth+1)){fat32_delete_tree(v,dst);return false;}}
    return block_flush(v.device);
}
static bool delete_tree_impl(Fat32Volume&v,const char*path,uint32_t depth){
    if(depth>128)return false;Fat32DirEntry st{};if(!fat32_stat(v,path,st))return false;if(!(st.attributes&ATTR_DIRECTORY))return fat32_delete_file(v,path);
    if((path[0]=='/'||path[0]=='\\')&&path[1]==0)return false;
    for(;;){PickCtx pick{0,0,false,{}};if(!fat32_list_directory(v,path,pick_tree_entry,&pick))return false;if(!pick.found)break;char child[1024]{};if(!tree_path_join(child,sizeof(child),path,pick.entry.name)||!delete_tree_impl(v,child,depth+1))return false;}
    return fat32_rmdir(v,path);
}
}

bool fat32_stat(Fat32Volume& v,const char*path,Fat32DirEntry&out){
    out={};if(!v.stats.mounted||!path||!*path)return false;
    if((path[0]=='/'||path[0]=='\\')&&path[1]==0){out.attributes=ATTR_DIRECTORY;out.firstCluster=v.stats.rootCluster;out.name[0]='/';out.name[1]=0;return true;}
    DirLocation loc{};uint32_t parent=0;char name[256]{};size_t len=0;if(!split_parent_component(v,path,parent,name,len)||!find_in_directory_name_loc(v,parent,name,len,loc,out.name))return false;Fat32DirEntry tmp=convert_entry(loc.entry);tmp.attributes=loc.entry.attr;tmp.firstCluster=entry_cluster(loc.entry);tmp.size=loc.entry.fileSize;tmp.createDate=loc.entry.createDate;tmp.createTime=loc.entry.createTime;tmp.writeDate=loc.entry.writeDate;tmp.writeTime=loc.entry.writeTime;tmp.accessDate=loc.entry.accessDate;char keep[256]{};size_t k=0;while(out.name[k]&&k<255){keep[k]=out.name[k];++k;}keep[k]=0;out=tmp;for(k=0;keep[k]&&k<255;++k)out.name[k]=keep[k];out.name[k]=0;return true;
}

bool fat32_copy_tree(Fat32Volume&v,const char*sourcePath,const char*destinationPath){if(!sourcePath||!destinationPath)return false;return copy_tree_impl(v,sourcePath,destinationPath,0);}
bool fat32_move_tree(Fat32Volume&v,const char*sourcePath,const char*destinationPath){
    if(!sourcePath||!destinationPath)return false;uint32_t sp=0,dp=0;char sn[256]{},dn[256]{};size_t sl=0,dl=0;if(!split_parent_component(v,sourcePath,sp,sn,sl)||!split_parent_component(v,destinationPath,dp,dn,dl))return false;if(sp==dp)return fat32_rename(v,sourcePath,destinationPath);if(!fat32_copy_tree(v,sourcePath,destinationPath))return false;if(!fat32_delete_tree(v,sourcePath)){fat32_delete_tree(v,destinationPath);return false;}return true;
}
bool fat32_delete_tree(Fat32Volume&v,const char*path){return path&&delete_tree_impl(v,path,0);}


bool fat32_write_file_from_block(Fat32Volume& v,const char* path,BlockDevice* source,uint64_t bytes) {
    if(!v.stats.mounted||!v.device||!v.device->write||!source||!source->read||!path)return false;
    if(source->sectorSize==0||bytes>0xFFFFFFFFULL||bytes>source->sectorCount*uint64_t(source->sectorSize))return false;
    if(v.stats.bytesPerSector!=source->sectorSize)return false;
    DirLocation loc{};
    if(!resolve_path_loc(v,path,loc,false)) {
        if(!fat32_create_file(v,path)||!resolve_path_loc(v,path,loc,false))return false;
    }
    if(loc.entry.attr&ATTR_READ_ONLY)return false;
    const uint32_t clusterBytes=v.stats.bytesPerSector*v.stats.sectorsPerCluster;
    const uint32_t need=bytes?static_cast<uint32_t>((bytes+clusterBytes-1)/clusterBytes):0;
    uint32_t first=entry_cluster(loc.entry);
    // Every byte that becomes part of the file is overwritten below, so avoid
    // zeroing each newly allocated cluster first.  This halves installer writes.
    if(!resize_chain(v,first,need,false))return false;

    alignas(4096) static uint8_t stream[4096];
    uint64_t done=0,sourceLba=0;
    uint32_t cluster=first;
    while(done<bytes){
        if(cluster<2||cluster>=FAT32_EOC||cluster==FAT32_BAD)return false;
        const uint64_t dstLba=cluster_lba(v,cluster);
        for(uint32_t sec=0;sec<v.stats.sectorsPerCluster&&done<bytes;++sec){
            const uint64_t left=bytes-done;
            const uint32_t takeBytes=left<v.stats.bytesPerSector?static_cast<uint32_t>(left):v.stats.bytesPerSector;
            if(takeBytes==v.stats.bytesPerSector){
                if(!block_read(source,sourceLba,1,stream))return false;
            }else{
                zero_bytes(stream,v.stats.bytesPerSector);
                if(!block_read(source,sourceLba,1,stream))return false;
            }
            if(!block_write(v.device,dstLba+sec,1,stream))return false;
            done+=takeBytes;
            ++sourceLba;
        }
        if(done<bytes){uint32_t next=0;if(!read_fat_entry(v,cluster,next)||next<2||next>=FAT32_EOC||next==FAT32_BAD)return false;cluster=next;}
    }
    set_entry_cluster(loc.entry,first);loc.entry.fileSize=static_cast<uint32_t>(bytes);
    if(!write_dir_entry(v,loc,loc.entry))return false;
    return block_flush(v.device);
}

bool fat32_check_volume(Fat32Volume& v,Fat32CheckStats& out) {
    out={};if(!v.stats.mounted||!v.device||!v.device->read)return false;
    const uint32_t last=max_cluster(v);

    // First pass: validate every FAT link and collect capacity statistics. With
    // the FAT-sector cache this is one media read per FAT sector rather than one
    // read per cluster.
    for(uint32_t c=2;c<=last;++c){
        uint32_t n=0;if(!read_fat_entry(v,c,n))return false;++out.scannedClusters;
        if(n==0){++out.freeClusters;continue;}
        ++out.allocatedClusters;
        if(n==FAT32_BAD){++out.badClusters;continue;}
        if(n>=FAT32_EOC)continue;
        if(n<2||n>last){++out.invalidLinks;continue;}
        if(n==c)++out.loops;
    }

    // Full loop detection without the previous O(n^2) "Floyd from every
    // cluster" behavior. A one-bit processed map means each valid chain is
    // traversed only a small constant number of times. 16M clusters covers
    // practical FAT32 volumes up to tens/hundreds of GiB depending on cluster
    // size while using only 2 MiB of BSS.
    constexpr uint32_t TRACKED_CLUSTERS = 16u*1024u*1024u;
    constexpr uint32_t TRACKED_BYTES = TRACKED_CLUSTERS/8u;
    static uint8_t processed[TRACKED_BYTES];
    const uint32_t trackLast=last<TRACKED_CLUSTERS-1?last:TRACKED_CLUSTERS-1;
    const uint32_t bytesToClear=(trackLast+8u)/8u;
    for(uint32_t i=0;i<bytesToClear;++i)processed[i]=0;
    auto is_done=[&](uint32_t c)->bool{return c<=trackLast&&(processed[c>>3]&(1u<<(c&7)))!=0;};
    auto set_done=[&](uint32_t c){if(c<=trackLast)processed[c>>3]|=static_cast<uint8_t>(1u<<(c&7));};
    auto valid_next=[&](uint32_t n)->bool{return n>=2&&n<=last&&n<FAT32_EOC&&n!=FAT32_BAD;};

    for(uint32_t c=2;c<=trackLast;++c){
        if(is_done(c))continue;
        uint32_t firstNext=0;if(!read_fat_entry(v,c,firstNext))return false;
        if(!valid_next(firstNext)){set_done(c);continue;}

        // Brent cycle detection. Stop immediately if this path joins a chain
        // already verified by an earlier start node.
        uint32_t tortoise=c,hare=firstNext,power=1,lam=1;
        bool cycle=false;
        for(uint32_t steps=0;steps<MAX_CHAIN_STEPS;++steps){
            if(hare==tortoise){cycle=true;break;}
            if(is_done(hare))break;
            uint32_t hn=0;if(!read_fat_entry(v,hare,hn))return false;
            if(!valid_next(hn))break;
            if(power==lam){tortoise=hare;power<<=1;lam=0;if(power==0)break;}
            hare=hn;++lam;
        }
        if(cycle && firstNext!=c)++out.loops; // self-loop already counted above.

        // Mark this path. Cycles terminate naturally once they return to a bit
        // set earlier in this walk.
        uint32_t cur=c;
        for(uint32_t steps=0;steps<MAX_CHAIN_STEPS&&cur>=2&&cur<=trackLast;++steps){
            if(is_done(cur))break;
            set_done(cur);
            uint32_t n=0;if(!read_fat_entry(v,cur,n))return false;
            if(!valid_next(n)||n>trackLast)break;
            cur=n;
        }
    }

    out.healthy=out.invalidLinks==0&&out.loops==0;
    return true;
}

