#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"

static bool expect(bool ok,const char*what){
    std::printf("%-34s : %s\n",what,ok?"OK":"FAIL");
    return ok;
}

static bool write_text(Fat32Volume&v,const char*path,const char*text){
    if(!fat32_create_file(v,path))return false;
    size_t n=0;while(text[n])++n;
    return fat32_write_file(v,path,text,n);
}

static bool read_equals(Fat32Volume&v,const char*path,const char*expected){
    char out[128]{};size_t got=0;if(!fat32_read_file(v,path,out,sizeof(out),got))return false;
    size_t n=0;while(expected[n])++n;
    if(got!=n)return false;for(size_t i=0;i<n;++i)if(out[i]!=expected[i])return false;return true;
}

int main(int argc,char**argv){
    if(argc!=2){std::fprintf(stderr,"usage: fat32_tree_test image\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary);if(!in)return 3;
    in.seekg(0,std::ios::end);auto n=in.tellg();in.seekg(0);
    std::vector<uint8_t> image((size_t)n);in.read((char*)image.data(),n);
    BlockDevice dev{};if(!ramdisk_init(dev,image.data(),image.size(),512))return 4;
    Fat32Volume v{};bool all=expect(fat32_mount(&dev,v),"mount");if(!all)return 5;

    fat32_delete_tree(v,"/TREEA");fat32_delete_tree(v,"/TREEB");fat32_delete_tree(v,"/TREEM");
    all&=expect(fat32_mkdir(v,"/TREEA"),"mkdir source");
    all&=expect(fat32_mkdir(v,"/TREEA/SUB"),"mkdir nested");
    all&=expect(write_text(v,"/TREEA/A.TXT","ROOT DATA"),"write root file");
    all&=expect(write_text(v,"/TREEA/SUB/B.TXT","NESTED DATA"),"write nested file");

    Fat32DirEntry st{};
    all&=expect(fat32_stat(v,"/TREEA",st)&&(st.attributes&0x10),"stat directory");
    all&=expect(fat32_copy_tree(v,"/TREEA","/TREEB"),"recursive copy");
    all&=expect(read_equals(v,"/TREEB/A.TXT","ROOT DATA"),"verify copied root");
    all&=expect(read_equals(v,"/TREEB/SUB/B.TXT","NESTED DATA"),"verify copied nested");

    all&=expect(fat32_move_tree(v,"/TREEB","/TREEM"),"recursive move/rename");
    all&=expect(!fat32_stat(v,"/TREEB",st),"old path gone");
    all&=expect(read_equals(v,"/TREEM/SUB/B.TXT","NESTED DATA"),"verify moved nested");

    all&=expect(fat32_delete_tree(v,"/TREEA"),"recursive delete source");
    all&=expect(fat32_delete_tree(v,"/TREEM"),"recursive delete moved");
    all&=expect(!fat32_stat(v,"/TREEA",st)&&!fat32_stat(v,"/TREEM",st),"trees removed");
    std::printf("free bytes after test: %llu\n",(unsigned long long)fat32_free_bytes(v));
    return all?0:1;
}
