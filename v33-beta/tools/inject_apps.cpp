#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include "../kernel/include/block_device.hpp"
#include "../kernel/include/fat32.hpp"

#pragma pack(push,1)
struct H {
    char magic[4];
    uint16_t version;
    uint16_t flags;
    uint32_t entryOffset;
    uint32_t codeSize;
    uint32_t reserved;
};
#pragma pack(pop)

static bool put_app(Fat32Volume& v,const char* path,const uint8_t* code,size_t codeSize) {
    H h{{'Z','E','X','E'},1,0,0,static_cast<uint32_t>(codeSize),0};
    std::vector<uint8_t> file(sizeof(h)+codeSize);
    for(size_t i=0;i<sizeof(h);++i)file[i]=reinterpret_cast<uint8_t*>(&h)[i];
    for(size_t i=0;i<codeSize;++i)file[sizeof(h)+i]=code[i];

    // Rebuilding rootfs should be idempotent.
    fat32_delete_file(v,path);
    if(!fat32_create_file(v,path))return false;
    return fat32_write_file(v,path,file.data(),file.size());
}

static bool read_binary(const char* path,std::vector<uint8_t>& out) {
    std::ifstream f(path,std::ios::binary);
    if(!f)return false;
    f.seekg(0,std::ios::end);const auto n=f.tellg();f.seekg(0);
    if(n<=0)return false;
    out.resize(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(out.data()),n);
    return !!f;
}

static bool put_compiled_app(Fat32Volume& v,const char* imageDir,const char* binaryName,const char* targetPath) {
    char hostPath[512]{};
    std::snprintf(hostPath,sizeof(hostPath),"%s/%s",imageDir,binaryName);
    std::vector<uint8_t> code;
    if(!read_binary(hostPath,code)) {
        std::fprintf(stderr,"could not read user app: %s\n",hostPath);
        return false;
    }
    return put_app(v,targetPath,code.data(),code.size());
}


static bool put_raw_file(Fat32Volume& v,const char* target,const char* host) {
    std::vector<uint8_t> data;
    if(!read_binary(host,data)) { std::fprintf(stderr,"could not read install payload: %s\n",host); return false; }
    fat32_delete_file(v,target);
    if(!fat32_create_file(v,target)) return false;
    return fat32_write_file(v,target,data.data(),data.size());
}

int main(int argc,char**argv){
    if(argc!=5){std::fprintf(stderr,"usage: inject_apps rootfs.img userapps-dir BOOTX64.EFI KERNEL.ELF\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary); if(!in)return 3;
    in.seekg(0,std::ios::end); auto n=in.tellg(); in.seekg(0);
    std::vector<uint8_t> img((size_t)n); in.read((char*)img.data(),n);
    BlockDevice d{}; if(!ramdisk_init(d,img.data(),img.size(),512))return 4;
    Fat32Volume v{}; if(!fat32_mount(&d,v))return 5;

    // The integrated GPT installer needs a copy of the EFI loader and kernel
    // inside its RAM-resident template so it can populate the HDD ESP.
    fat32_mkdir(v,"/INSTALL");
    if(!put_raw_file(v,"/INSTALL/BOOTX64.EFI",argv[3]))return 11;
    if(!put_raw_file(v,"/INSTALL/KERNEL.ELF",argv[4]))return 12;

    // v33.0 Native EXE v2.  These are real compiled Ring-3 applications:
    // each binary owns a private CR3, creates its own GUI window and remains
    // alive by cooperatively yielding back to the kernel event loop.
    fat32_mkdir(v,"/SYSTEM32");
    struct NativeExe { const char* bin; const char* path; };
    const NativeExe nativeExes[]={
      {"EXPLORER.bin","/SYSTEM32/EXPLORER.EXE"},
      {"NOTEPAD.bin","/SYSTEM32/NOTEPAD.EXE"},
      {"CMD.bin","/SYSTEM32/CMD.EXE"},
      {"CONTROL.bin","/SYSTEM32/CONTROL.EXE"},
      {"TASKMGR.bin","/SYSTEM32/TASKMGR.EXE"},
      {"DEVMGMT.bin","/SYSTEM32/DEVMGMT.EXE"},
      {"RECYCLE.bin","/SYSTEM32/RECYCLE.EXE"},
      {"SERVICES.bin","/SYSTEM32/SERVICES.EXE"},
      {"EVENTVWR.bin","/SYSTEM32/EVENTVWR.EXE"},
      {"MYCOMP.bin","/SYSTEM32/MYCOMP.EXE"},
      {"NETWORK.bin","/SYSTEM32/NETWORK.EXE"},
      {"MYDOCS.bin","/SYSTEM32/MYDOCS.EXE"},
      {"PRINTERS.bin","/SYSTEM32/PRINTERS.EXE"}
    };
    for(size_t i=0;i<sizeof(nativeExes)/sizeof(nativeExes[0]);++i)
        if(!put_compiled_app(v,argv[2],nativeExes[i].bin,nativeExes[i].path)) return 20+static_cast<int>(i);

    // HELLO: GET_VERSION, 3x PING, EXIT(23).
    const uint8_t hello[]={
      0xB8,0x02,0,0,0,0xCD,0x80,
      0xB8,0x03,0,0,0,0xCD,0x80,
      0xB8,0x03,0,0,0,0xCD,0x80,
      0xB8,0x03,0,0,0,0xCD,0x80,
      0xBF,0x17,0,0,0,0xB8,0x01,0,0,0,0xCD,0x80,0x0F,0x0B
    };
    if(!put_app(v,"/APPS/HELLO.ZEX",hello,sizeof(hello)))return 6;

    // WINDOW: create 300x180 window, draw two rectangles, EXIT(24).
    const uint8_t windowApp[]={
      0xB8,0x0A,0,0,0, 0xBF,0x2C,0x01,0,0, 0xBE,0xB4,0,0,0, 0xCD,0x80, 0x49,0x89,0xC4,
      0xB8,0x0B,0,0,0, 0x4C,0x89,0xE7, 0xBE,0x0C,0,0,0, 0xBA,0x0C,0,0,0,
      0x41,0xBA,0x14,0x01,0,0, 0x41,0xB8,0x90,0,0,0, 0x41,0xB9,0xE0,0x80,0x40,0, 0xCD,0x80,
      0xB8,0x0B,0,0,0, 0x4C,0x89,0xE7, 0xBE,0x30,0,0,0, 0xBA,0x30,0,0,0,
      0x41,0xBA,0xA0,0,0,0, 0x41,0xB8,0x30,0,0,0, 0x41,0xB9,0x40,0x80,0xE0,0, 0xCD,0x80,
      0xBF,0x18,0,0,0, 0xB8,0x01,0,0,0, 0xCD,0x80, 0x0F,0x0B
    };
    if(!put_app(v,"/APPS/WINDOW.ZEX",windowApp,sizeof(windowApp)))return 7;

    // AUDIO: set volume to 70%, play 880 Hz / 120 ms, EXIT(25).
    const uint8_t audioApp[]={
      0xBF,0x46,0,0,0, 0xB8,0x15,0,0,0, 0xCD,0x80,
      0xBF,0x70,0x03,0,0, 0xBE,0x78,0,0,0, 0xB8,0x14,0,0,0, 0xCD,0x80,
      0xBF,0x19,0,0,0, 0xB8,0x01,0,0,0, 0xCD,0x80, 0x0F,0x0B
    };
    if(!put_app(v,"/APPS/AUDIO.ZEX",audioApp,sizeof(audioApp)))return 8;

    // NET: query status + send a gateway ping, EXIT(26).
    const uint8_t netApp[]={
      0xB8,0x1F,0,0,0,0xCD,0x80,
      0xB8,0x1E,0,0,0,0xCD,0x80,
      0xBF,0x1A,0,0,0,0xB8,0x01,0,0,0,0xCD,0x80,0x0F,0x0B
    };
    if(!put_app(v,"/APPS/NET.ZEX",netApp,sizeof(netApp)))return 9;

    // USER: user-info syscall + get current PID, EXIT(27).
    const uint8_t userApp[]={
      0xB8,0x28,0,0,0,0xCD,0x80,
      0xB8,0x32,0,0,0,0xCD,0x80,
      0xBF,0x1B,0,0,0,0xB8,0x01,0,0,0,0xCD,0x80,0x0F,0x0B
    };
    if(!put_app(v,"/APPS/USER.ZEX",userApp,sizeof(userApp)))return 10;

    std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);
    out.write((const char*)img.data(),img.size());
    std::printf("Injected installer payload + 13 native Ring-3 /SYSTEM32 .EXE apps + 5 /APPS .ZEX tests\n");
    return 0;
}
