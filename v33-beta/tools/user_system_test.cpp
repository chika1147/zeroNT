#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include <cstring>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"
#include "../kernel/include/user_manager.hpp"
#include "../kernel/include/safe_fs.hpp"

static int fail(const char* m){std::fprintf(stderr,"FAIL: %s\n",m);return 1;}
int main(int argc,char**argv){
    if(argc!=2)return fail("usage user_system_test image");
    std::ifstream in(argv[1],std::ios::binary); if(!in)return fail("open image");
    in.seekg(0,std::ios::end);auto n=in.tellg();in.seekg(0);std::vector<uint8_t> img((size_t)n);in.read((char*)img.data(),n);
    BlockDevice d{}; if(!ramdisk_init(d,img.data(),img.size(),512))return fail("ramdisk"); Fat32Volume v{}; if(!fat32_mount(&d,v))return fail("mount"); safe_fs_init(&v); if(!safe_fs_check_volume())return fail("safe fs init");
    if(!user_system_init(v))return fail("user init"); if(!user_system_stats().setupRequired)return fail("first boot setup expected");
    if(!user_setup_create_admin(v,"sora one","Pass1234",0x123456789ULL))return fail("create admin");
    auto s=user_system_stats(); if(!s.loggedIn||s.currentUid!=1000||s.currentRole!=UserRole::Administrator)return fail("admin session");
    if(std::strcmp(s.logicalHome,"C:\\Users\\sora one")!=0)return fail("logical home");
    if(!user_has_permission(UserPermManageUsers|UserPermWriteSystem))return fail("admin permissions");
    char real[128]{}; if(!user_resolve_own_path("C:\\Users\\sora one\\Documents\\NOTE.TXT",real,sizeof(real),true))return fail("resolve path");
    if(std::strcmp(real,"/USERS/U1000/DOCS/NOTE.TXT")!=0)return fail("resolved backing path");
    user_logout(); if(user_login(v,"sora one","bad"))return fail("wrong password accepted"); if(!user_login(v,"SORA ONE","Pass1234"))return fail("case-insensitive login");
    if(!user_change_display_name(v,"daily user"))return fail("rename display name"); user_logout(); if(!user_login(v,"daily user","Pass1234"))return fail("login renamed user");
    if(!user_change_password(v,"Pass1234","NewPass9",0xCAFEBABEULL))return fail("change password"); user_logout(); if(user_login(v,"daily user","Pass1234"))return fail("old password still works"); if(!user_login(v,"daily user","NewPass9"))return fail("new password");
    std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);out.write((const char*)img.data(),img.size());
    std::printf("PASS: setup/login/logout/password/rename/permissions/profile path\n");
    std::printf("logical=%s backing=%s resolved=%s\n",user_system_stats().logicalHome,user_system_stats().backingHome,real);
    return 0;
}
