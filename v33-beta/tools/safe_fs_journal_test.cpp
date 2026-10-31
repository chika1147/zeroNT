#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"
#include "../kernel/include/safe_fs.hpp"

void* kmalloc(size_t n){return std::malloc(n);} void kfree(void*p){std::free(p);} void quality_record_recoverable_error(){}
static bool put(Fat32Volume&v,const char*p,const char*s){fat32_delete_file(v,p);if(!fat32_create_file(v,p))return false;return fat32_write_file(v,p,s,std::strlen(s));}
static bool get(Fat32Volume&v,const char*p,char*out,size_t cap){size_t n=0;if(!fat32_read_file(v,p,out,cap-1,n))return false;out[n]=0;return true;}
int main(int argc,char**argv){if(argc!=2){std::fprintf(stderr,"usage: safe_fs_journal_test image\n");return 2;}std::ifstream in(argv[1],std::ios::binary);if(!in)return 3;in.seekg(0,std::ios::end);auto sz=in.tellg();in.seekg(0);std::vector<uint8_t>img((size_t)sz);in.read((char*)img.data(),sz);BlockDevice d{};if(!ramdisk_init(d,img.data(),img.size(),512))return 4;Fat32Volume v{};if(!fat32_mount(&d,v))return 5;fat32_mkdir(v,"/SYSTEM");safe_fs_init(&v);if(!safe_fs_check_volume())return 6;
if(!safe_fs_atomic_write("/SYSTEM/BETATST.TXT","OLD",3))return 7;char b[64]{};if(!get(v,"/SYSTEM/BETATST.TXT",b,sizeof(b))||std::strcmp(b,"OLD"))return 8;
// Simulate an interrupted transaction after old target was renamed to backup and temp was fully written.
fat32_delete_file(v,"/SYSTEM/ZNTTMP.TMP");fat32_delete_file(v,"/SYSTEM/ZNTBAK.BAK");if(!put(v,"/SYSTEM/ZNTTMP.TMP","NEW"))return 9;if(!fat32_rename(v,"/SYSTEM/BETATST.TXT","/SYSTEM/ZNTBAK.BAK"))return 10;
const char*j="TARGET=/SYSTEM/BETATST.TXT\r\nTEMP=/SYSTEM/ZNTTMP.TMP\r\nBACKUP=/SYSTEM/ZNTBAK.BAK\r\nSTAGE=2\r\n";if(!put(v,"/SYSTEM/ZNTTXN.TXT",j))return 11;
if(!safe_fs_recover_pending())return 12;if(!get(v,"/SYSTEM/BETATST.TXT",b,sizeof(b))||std::strcmp(b,"OLD"))return 13;auto st=safe_fs_stats();if(st.recoveredTransactions<1)return 14;std::printf("SafeFS journal recovery PASS: restored=%s recovered=%llu\n",b,(unsigned long long)st.recoveredTransactions);return 0;}
