#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include <cstring>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"

static bool put(Fat32Volume& v,const char* path,const char* text){
    fat32_delete_file(v,path);
    if(!fat32_create_file(v,path)) return false;
    return fat32_write_file(v,path,text,std::strlen(text));
}

int main(int argc,char**argv){
    if(argc!=2){std::fprintf(stderr,"usage: inject_v29_quality rootfs.img\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary); if(!in)return 3;
    in.seekg(0,std::ios::end); auto n=in.tellg(); in.seekg(0);
    std::vector<uint8_t> img((size_t)n); in.read((char*)img.data(),n);
    BlockDevice d{}; if(!ramdisk_init(d,img.data(),img.size(),512))return 4;
    Fat32Volume v{}; if(!fat32_mount(&d,v))return 5;
    fat32_mkdir(v,"/SYSTEM");
    fat32_mkdir(v,"/SYSTEM/APPS");
    if(!put(v,"/SYSTEM/VERSION.TXT","zeroNT v29 Quality UI Pack 1-5\r\n"))return 6;
    struct A{const char*path;const char*body;};
    const A apps[]={
      {"/SYSTEM/APPS/EXPLORER.APP","name=Explorer\r\ntype=builtin-system-app\r\nuses=user-filesystem,recycle-bin\r\n"},
      {"/SYSTEM/APPS/NOTEPAD.APP","name=Notepad\r\ntype=builtin-system-app\r\nfile=Documents\\NOTE.TXT\r\n"},
      {"/SYSTEM/APPS/CMD.APP","name=Command Prompt\r\ntype=builtin-system-app\r\ncommands=help,whoami,ver,pwd,dir,drivers,net,audio,cls,echo\r\n"},
      {"/SYSTEM/APPS/CONTROL.APP","name=Control Panel\r\ntype=builtin-system-app\r\nuses=user,permissions,audio,network\r\n"},
      {"/SYSTEM/APPS/TASKMGR.APP","name=Task Manager\r\ntype=builtin-system-app\r\nuses=process,scheduler\r\n"},
      {"/SYSTEM/APPS/DEVMGR.APP","name=Device Manager\r\ntype=builtin-system-app\r\nuses=driver-manager,pci,ahci,nvme,usb,acpi\r\n"},
      {"/SYSTEM/APPS/RECYCLE.APP","name=Recycle Bin\r\ntype=builtin-system-app\r\nper-user=true\r\n"},
    };
    for(const auto&a:apps)if(!put(v,a.path,a.body))return 7;
    std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);out.write((const char*)img.data(),img.size());
    std::printf("Injected zeroNT v29 quality version + 7 system app manifests.\n");
    return 0;
}
