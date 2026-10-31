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
    if(argc!=2){std::fprintf(stderr,"usage: inject_v30_quality rootfs.img\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary);if(!in)return 3;in.seekg(0,std::ios::end);auto n=in.tellg();in.seekg(0);
    std::vector<uint8_t> img((size_t)n);in.read((char*)img.data(),n);BlockDevice d{};if(!ramdisk_init(d,img.data(),img.size(),512))return 4;Fat32Volume v{};if(!fat32_mount(&d,v))return 5;
    fat32_mkdir(v,"/SYSTEM");fat32_mkdir(v,"/SYSTEM/APPS");
    if(!put(v,"/SYSTEM/VERSION.TXT","zeroNT v30 Quality Core Pack 6-10\r\n"))return 6;
    if(!put(v,"/SYSTEM/QUALITY.CFG","crash_isolation=ring3\r\nsafe_write=verify+backup+rename\r\nahci_retries=3\r\nfilesystem_check=boot\r\n"))return 7;
    if(!put(v,"/SYSTEM/SECURITY.CFG","password_min=8\r\npassword_policy=letter+number\r\nkdf=sha256-iterated-8192-development\r\nlogin_failures_before_lockout=5\r\nlockout_seconds=30\r\nsyscall_permissions=enabled\r\n"))return 8;
    if(!put(v,"/SYSTEM/NET.CFG","driver=e1000\r\ndhcp=retry\r\ndns=udp-a-record\r\ntcp=syn-probe-foundation\r\n"))return 9;
    put(v,"/SYSTEM/APPS/CMD.APP","name=Command Prompt\r\ntype=builtin-system-app\r\ncommands=help,whoami,ver,pwd,dir,drivers,health,ipconfig,ping,nslookup,tcptest,audio,cls,echo\r\n");
    std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);out.write((const char*)img.data(),img.size());
    std::printf("Injected zeroNT v30 quality/security/network metadata.\n");return 0;
}
