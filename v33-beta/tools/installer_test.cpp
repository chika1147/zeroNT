#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <vector>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"
#include "../kernel/include/installer.hpp"

struct MemCtx { uint8_t* p; uint64_t bytes; };
static bool mread(BlockDevice*d,uint64_t lba,uint32_t count,void*out){auto*c=(MemCtx*)d->context;uint64_t o=lba*d->sectorSize,n=(uint64_t)count*d->sectorSize;if(!c||!out||o>c->bytes||n>c->bytes-o)return false;auto*q=(uint8_t*)out;for(uint64_t i=0;i<n;++i)q[i]=c->p[o+i];return true;}
static bool mwrite(BlockDevice*d,uint64_t lba,uint32_t count,const void*in){auto*c=(MemCtx*)d->context;uint64_t o=lba*d->sectorSize,n=(uint64_t)count*d->sectorSize;if(!c||!in||o>c->bytes||n>c->bytes-o)return false;auto*q=(const uint8_t*)in;for(uint64_t i=0;i<n;++i)c->p[o+i]=q[i];return true;}
static bool mflush(BlockDevice*){return true;}
static bool ok(bool v,const char*what){std::printf("%-34s : %s\n",what,v?"OK":"FAIL");std::fflush(stdout);return v;}
static void progress(uint64_t done,uint64_t total,void*){if(done==0||done==total){std::printf("copy progress %llu/%llu\n",(unsigned long long)done,(unsigned long long)total);std::fflush(stdout);}}
int main(int argc,char**argv){
    if(argc!=2){std::fprintf(stderr,"usage: installer_test rootfs.img\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary);if(!in)return 3;in.seekg(0,std::ios::end);size_t n=(size_t)in.tellg();in.seekg(0);std::vector<uint8_t>srcMem(n);in.read((char*)srcMem.data(),n);
    BlockDevice src{};if(!ramdisk_init(src,srcMem.data(),srcMem.size(),512))return 4;
    constexpr uint64_t TARGET_BYTES=256ULL*1024*1024;std::vector<uint8_t> targetMem((size_t)TARGET_BYTES,0);MemCtx mc{targetMem.data(),TARGET_BYTES};
    BlockDevice target{"installer test disk",512,TARGET_BYTES/512,&mc,mread,mwrite,mflush};
    bool all=true;all&=ok(installer_init(&src,&target),"installer init");all&=ok(installer_probe_target()==InstallerTargetState::Blank,"blank target detected");
    all&=ok(installer_install(progress,nullptr),"GPT/FAT32/system install");all&=ok(installer_verify_target(),"verify installed target");
    Fat32Volume sys{};all&=ok(installer_mount_target(sys),"mount installed system");Fat32DirEntry st{};all&=ok(fat32_stat(sys,"/SYSTEM/INSTALL.OK",st),"installation marker");
    all&=ok(installer_repair(),"repair ESP boot payload");all&=ok(installer_upgrade(progress,nullptr),"upgrade preserves system data");
    auto s=installer_stats();all&=ok(s.gptCreated&&s.espFormatted&&s.bootFilesInstalled,"installer state flags");return all?0:1;
}
