#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include <cstring>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"

static bool put(Fat32Volume& v,const char* path,const char* text){
    fat32_delete_file(v,path);
    if(!fat32_create_file(v,path))return false;
    return fat32_write_file(v,path,text,std::strlen(text));
}

int main(int argc,char**argv){
    if(argc!=2){std::fprintf(stderr,"usage: inject_v32_installer rootfs.img\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary);if(!in)return 3;
    in.seekg(0,std::ios::end);auto n=in.tellg();in.seekg(0);
    std::vector<uint8_t> img((size_t)n);in.read((char*)img.data(),n);
    BlockDevice d{};if(!ramdisk_init(d,img.data(),img.size(),512))return 4;
    Fat32Volume v{};if(!fat32_mount(&d,v))return 5;
    fat32_mkdir(v,"/SYSTEM");fat32_mkdir(v,"/SYSTEM/APPS");fat32_mkdir(v,"/RECOVERY");
    // The template is installation media, not an installed target.
    fat32_delete_file(v,"/SYSTEM/INSTALL.OK");
    fat32_delete_file(v,"/SYSTEM/INSTALL.LOG");
    fat32_delete_file(v,"/SYSTEM/USERS.DAT");
    if(!put(v,"/SYSTEM/VERSION.TXT","zeroNT v32.12 Backup Classic UI\r\n"))return 6;
    if(!put(v,"/SYSTEM/INSTALL.CFG","installer=integrated-kernel\r\nfirst_boot=install-then-user-setup\r\nexternal_install_script=disabled\r\ntarget=ahci-sata\r\nsource=builtin-rootfs-template\r\n"))return 7;
    if(!put(v,"/SYSTEM/BETA.CFG","beta=stabilization\r\nevent_log=/SYSTEM/EVENT.LOG\r\nsoak_monitor=enabled\r\ncrash_isolation=ring3\r\nfilesystem_journal=SafeFS-transaction\r\ninstaller=integrated\r\n"))return 8;
    if(!put(v,"/RECOVERY/README.TXT","zeroNT v32.12 recovery support.\r\nThe installer is built into zeroNT.\r\nSafeFS automatically recovers interrupted file transactions at boot.\r\n"))return 9;
    std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);out.write((const char*)img.data(),img.size());
    std::printf("Injected zeroNT v32.12 integrated-installer metadata.\n");return 0;
}
