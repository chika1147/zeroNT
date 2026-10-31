#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>
#include <cstring>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"

struct Find { const char* wanted; bool found; };
static void cb(const Fat32DirEntry& e,void* ctx){auto*c=(Find*)ctx;if(std::strcmp(e.name,c->wanted)==0)c->found=true;}
int main(int argc,char**argv){
    if(argc!=2){std::fprintf(stderr,"usage: fat32_lfn_test rootfs.img\n");return 2;}
    std::ifstream f(argv[1],std::ios::binary);if(!f)return 3;f.seekg(0,std::ios::end);auto n=f.tellg();f.seekg(0);std::vector<uint8_t> img((size_t)n);f.read((char*)img.data(),n);
    BlockDevice d{};if(!ramdisk_init(d,img.data(),img.size(),512))return 4;Fat32Volume v{};if(!fat32_mount(&d,v))return 5;
    const char* dir="/Long Folder 日本語";const char* file="/Long Folder 日本語/これは長いファイル名です zeroNT public beta.txt";
    fat32_delete_tree(v,dir);
    if(!fat32_mkdir(v,dir)){std::puts("mkdir LFN FAIL");return 10;}
    if(!fat32_create_file(v,file)){std::puts("create LFN FAIL");return 11;}
    const char msg[]="LFN+UTF8+deep-path OK";if(!fat32_write_file(v,file,msg,sizeof(msg)-1)){std::puts("write LFN FAIL");return 12;}
    char out[64]{};size_t got=0;if(!fat32_read_file(v,file,out,sizeof(out),got)||got!=sizeof(msg)-1||std::memcmp(out,msg,got)){std::puts("read LFN FAIL");return 13;}
    Find find{"これは長いファイル名です zeroNT public beta.txt",false};if(!fat32_list_directory(v,dir,cb,&find)||!find.found){std::puts("list LFN FAIL");return 14;}
    Fat32DirEntry st{};if(!fat32_stat(v,file,st)||std::strcmp(st.name,find.wanted)!=0){std::puts("stat LFN FAIL");return 15;}
    Fat32CheckStats check{};if(!fat32_check_volume(v,check)||!check.healthy){std::puts("FAT check FAIL");return 16;}
    if(!fat32_delete_tree(v,dir)){std::puts("recursive delete LFN FAIL");return 17;}
    std::printf("LFN PASS free=%llu allocated=%llu\n",(unsigned long long)check.freeClusters,(unsigned long long)check.allocatedClusters);
    return 0;
}
