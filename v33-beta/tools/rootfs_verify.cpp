#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"

static bool verify_app(Fat32Volume&v,const char*path){
    uint8_t buf[8192]{};size_t got=0;
    bool ok=fat32_read_file(v,path,buf,sizeof(buf),got)&&got>=20&&
            buf[0]=='Z'&&buf[1]=='E'&&buf[2]=='X'&&buf[3]=='E';
    std::printf("%-22s : %s (%zu bytes)\n",path,ok?"OK":"FAIL",got);
    return ok;
}

int main(int argc,char**argv){
    if(argc!=2){std::fprintf(stderr,"usage: rootfs_verify rootfs.img\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary);if(!in)return 3;
    in.seekg(0,std::ios::end);auto n=in.tellg();in.seekg(0);
    std::vector<uint8_t> image((size_t)n);in.read((char*)image.data(),n);
    BlockDevice d{};if(!ramdisk_init(d,image.data(),image.size(),512))return 4;
    Fat32Volume v{};if(!fat32_mount(&d,v))return 5;
    bool ok=true;
    ok&=verify_app(v,"/APPS/HELLO.ZEX");
    ok&=verify_app(v,"/APPS/WINDOW.ZEX");
    ok&=verify_app(v,"/APPS/AUDIO.ZEX");
    ok&=verify_app(v,"/APPS/NET.ZEX");
    ok&=verify_app(v,"/APPS/USER.ZEX");
    const char* systemExes[]={
        "/SYSTEM32/EXPLORER.EXE","/SYSTEM32/NOTEPAD.EXE","/SYSTEM32/CMD.EXE",
        "/SYSTEM32/CONTROL.EXE","/SYSTEM32/TASKMGR.EXE","/SYSTEM32/DEVMGMT.EXE",
        "/SYSTEM32/RECYCLE.EXE","/SYSTEM32/SERVICES.EXE","/SYSTEM32/EVENTVWR.EXE",
        "/SYSTEM32/MYCOMP.EXE","/SYSTEM32/NETWORK.EXE","/SYSTEM32/MYDOCS.EXE",
        "/SYSTEM32/PRINTERS.EXE"
    };
    for(const char* p:systemExes) ok&=verify_app(v,p);
    Fat32DirEntry st{};
    bool readme=fat32_stat(v,"/README.TXT",st);
    std::printf("%-22s : %s\n","/README.TXT",readme?"OK":"FAIL");
    ok&=readme;
    return ok?0:1;
}
