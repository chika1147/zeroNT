#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"

static bool expect(bool ok, const char* what) {
    std::printf("%-28s : %s\n", what, ok ? "OK" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr,"usage: fat32_write_test image\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) return 3;
    in.seekg(0,std::ios::end); auto n=in.tellg(); in.seekg(0);
    std::vector<uint8_t> image((size_t)n); in.read((char*)image.data(), n);
    BlockDevice dev{};
    if (!ramdisk_init(dev,image.data(),image.size(),512)) return 4;
    Fat32Volume v{};
    bool all = expect(fat32_mount(&dev,v), "mount");
    if (!all) return 5;

    // Clean leftovers if this image has been tested before.
    fat32_delete_file(v,"/V20TEST.TXT");
    fat32_delete_file(v,"/DELETE.TMP");
    fat32_delete_file(v,"/V20DIR/A.TXT");
    fat32_delete_file(v,"/V20DIR/B.TXT");
    fat32_rmdir(v,"/V20DIR");

    all &= expect(fat32_create_file(v,"/V20TEST.TXT"), "create file");
    const char a[]="HELLO ZERO NT V20";
    all &= expect(fat32_write_file(v,"/V20TEST.TXT",a,sizeof(a)-1), "write file");
    const char b[]=" + APPEND";
    all &= expect(fat32_append_file(v,"/V20TEST.TXT",b,sizeof(b)-1), "append file");
    char out[128]{}; size_t got=0;
    all &= expect(fat32_read_file(v,"/V20TEST.TXT",out,sizeof(out),got), "read back");
    std::string expected=std::string(a,sizeof(a)-1)+std::string(b,sizeof(b)-1);
    all &= expect(got==expected.size() && std::string(out,got)==expected, "verify file data");

    all &= expect(fat32_mkdir(v,"/V20DIR"), "mkdir");
    all &= expect(fat32_create_file(v,"/V20DIR/A.TXT"), "create nested file");
    const char c[]="NESTED DATA";
    all &= expect(fat32_write_file(v,"/V20DIR/A.TXT",c,sizeof(c)-1), "write nested file");
    all &= expect(fat32_rename(v,"/V20DIR/A.TXT","/V20DIR/B.TXT"), "rename");
    char nested[64]{}; size_t ngot=0;
    all &= expect(fat32_read_file(v,"/V20DIR/B.TXT",nested,sizeof(nested),ngot), "read renamed file");
    all &= expect(ngot==sizeof(c)-1 && std::string(nested,ngot)==std::string(c,sizeof(c)-1), "verify renamed data");
    all &= expect(fat32_delete_file(v,"/V20DIR/B.TXT"), "delete file");
    all &= expect(fat32_rmdir(v,"/V20DIR"), "rmdir");

    all &= expect(fat32_create_file(v,"/DELETE.TMP"), "create delete temp");
    all &= expect(fat32_delete_file(v,"/DELETE.TMP"), "delete temp");

    std::ofstream save("/tmp/zeront-v20-fat32-test.img",std::ios::binary|std::ios::trunc);
    save.write((const char*)image.data(), image.size());
    std::printf("saved test image: /tmp/zeront-v20-fat32-test.img\n");
    return all ? 0 : 1;
}
