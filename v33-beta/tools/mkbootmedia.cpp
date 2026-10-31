#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

static void put16(std::vector<uint8_t>& b, size_t o, uint16_t v){ b[o]=uint8_t(v); b[o+1]=uint8_t(v>>8); }
static void put32(std::vector<uint8_t>& b, size_t o, uint32_t v){ for(int i=0;i<4;i++) b[o+i]=uint8_t(v>>(8*i)); }

struct FileData { std::string path; std::vector<uint8_t> data; };

static std::vector<uint8_t> readAll(const std::string& p){
    std::ifstream f(p, std::ios::binary);
    if(!f){ std::cerr << "Cannot open " << p << "\n"; std::exit(2); }
    f.seekg(0,std::ios::end); auto n=f.tellg(); f.seekg(0,std::ios::beg);
    std::vector<uint8_t> v((size_t)n); if(!v.empty()) f.read((char*)v.data(), n); return v;
}

static uint32_t crc32(const std::vector<uint8_t>& data){
    uint32_t c=0xFFFFFFFFu;
    for(uint8_t x:data){c^=x;for(int b=0;b<8;++b)c=(c>>1)^(0xEDB88320u&(0u-(c&1u)));}
    return ~c;
}
static std::string hex32(uint32_t v){
    static const char h[]="0123456789ABCDEF";std::string s="0x00000000";for(int i=0;i<8;++i)s[2+i]=h[(v>>(28-i*4))&0xF];return s;
}

static std::array<uint8_t,11> sfn(const std::string& name){
    std::array<uint8_t,11> r{}; r.fill(' ');
    // FAT sub-directories require real dot entries.  Treating "." as an
    // extension produced an invalid directory that some UEFI FAT drivers
    // tolerated for BOOTX64.EFI but rejected for later path traversal.
    if(name=="."){ r[0]='.'; return r; }
    if(name==".."){ r[0]='.'; r[1]='.'; return r; }
    auto pos=name.find('.'); std::string a=name.substr(0,pos), e=(pos==std::string::npos?"":name.substr(pos+1));
    auto up=[](char c){ return (c>='a'&&c<='z')? char(c-'a'+'A'):c; };
    for(size_t i=0;i<a.size()&&i<8;i++) r[i]=(uint8_t)up(a[i]);
    for(size_t i=0;i<e.size()&&i<3;i++) r[8+i]=(uint8_t)up(e[i]);
    return r;
}

struct Fat32Image {
    static constexpr uint32_t BPS=512, SPC=1, RESERVED=32, NFATS=2;
    uint32_t totalSectors, fatSectors, dataStart, clusterCount;
    std::vector<uint8_t> img;
    std::vector<uint32_t> fat;
    uint32_t nextCluster=3;

    explicit Fat32Image(uint32_t bytes){
        totalSectors=bytes/BPS;
        fatSectors=1;
        for(int i=0;i<16;i++){
            uint32_t dataSecs=totalSectors-RESERVED-NFATS*fatSectors;
            uint32_t clusters=dataSecs/SPC;
            uint32_t need=(uint32_t)(((uint64_t)(clusters+2)*4 + BPS-1)/BPS);
            if(need==fatSectors) break; fatSectors=need;
        }
        dataStart=RESERVED+NFATS*fatSectors;
        clusterCount=(totalSectors-dataStart)/SPC;
        if(clusterCount<65525){ std::cerr<<"Image too small for FAT32 cluster count\n"; std::exit(3); }
        img.assign((size_t)totalSectors*BPS,0);
        fat.assign(clusterCount+2,0);
        fat[0]=0x0FFFFFF8; fat[1]=0x0FFFFFFF; fat[2]=0x0FFFFFFF;
        writeBoot();
    }
    size_t clusterOff(uint32_t c) const { return (size_t)(dataStart + (c-2)*SPC)*BPS; }
    void writeBoot(){
        auto& b=img; b[0]=0xEB;b[1]=0x58;b[2]=0x90; memcpy(&b[3],"MSWIN4.1",8);
        put16(b,11,BPS); b[13]=SPC; put16(b,14,RESERVED); b[16]=NFATS; put16(b,17,0); put16(b,19,0); b[21]=0xF8; put16(b,22,0);
        put16(b,24,63); put16(b,26,255); put32(b,28,0); put32(b,32,totalSectors); put32(b,36,fatSectors); put16(b,40,0); put16(b,42,0);
        put32(b,44,2); put16(b,48,1); put16(b,50,6); b[64]=0x80; b[66]=0x29; put32(b,67,0x324E545A); memcpy(&b[71],"ZERONTBOOT ",11); memcpy(&b[82],"FAT32   ",8); b[510]=0x55;b[511]=0xAA;
        size_t f=512; put32(b,f+0,0x41615252); put32(b,f+484,0x61417272); put32(b,f+488,0xFFFFFFFF); put32(b,f+492,3); put32(b,f+508,0xAA550000);
        memcpy(&b[6*512], &b[0], 512); memcpy(&b[7*512], &b[512], 512);
    }
    uint32_t allocClusters(size_t bytes){
        uint32_t n=(uint32_t)((bytes + BPS*SPC-1)/(BPS*SPC)); if(n==0)n=1;
        uint32_t first=nextCluster, prev=0;
        for(uint32_t i=0;i<n;i++){
            if(nextCluster>=fat.size()){ std::cerr<<"FAT32 image full\n"; std::exit(4); }
            uint32_t c=nextCluster++;
            if(prev) fat[prev]=c; prev=c;
        }
        fat[prev]=0x0FFFFFFF; return first;
    }
    void writeChain(uint32_t first,const std::vector<uint8_t>& d){
        size_t p=0; uint32_t c=first;
        while(c>=2 && c<fat.size()){
            size_t take=std::min<size_t>(BPS*SPC,d.size()-p); if(take) memcpy(&img[clusterOff(c)],d.data()+p,take); p+=take;
            if(p>=d.size()) break; c=fat[c]&0x0FFFFFFF;
        }
    }
    void dirEnt(uint32_t dirCluster,int index,const std::string& name,uint8_t attr,uint32_t first,uint32_t size){
        size_t o=clusterOff(dirCluster)+index*32; auto n=sfn(name); memcpy(&img[o],n.data(),11); img[o+11]=attr;
        put16(img,o+20,(uint16_t)(first>>16)); put16(img,o+26,(uint16_t)first); put32(img,o+28,size);
    }
    uint32_t makeDir(uint32_t parent,const std::string& name,int parentIndex){
        uint32_t c=allocClusters(1); dirEnt(parent,parentIndex,name,0x10,c,0); dirEnt(c,0,".",0x10,c,0); dirEnt(c,1,"..",0x10,parent,0); return c;
    }
    uint32_t addFile(uint32_t dir,int index,const std::string& name,const std::vector<uint8_t>& data){
        uint32_t c=allocClusters(data.size()); writeChain(c,data); dirEnt(dir,index,name,0x20,c,(uint32_t)data.size()); return c;
    }
    void finish(){
        for(uint32_t copy=0;copy<NFATS;copy++){
            size_t base=(size_t)(RESERVED+copy*fatSectors)*BPS;
            for(size_t i=0;i<fat.size();i++){ uint32_t v=fat[i]; size_t o=base+i*4; if(o+4<=img.size()) {img[o]=v;img[o+1]=v>>8;img[o+2]=v>>16;img[o+3]=v>>24;} }
        }
        uint32_t freec=(uint32_t)(fat.size()-nextCluster); put32(img,512+488,freec); put32(img,512+492,nextCluster); memcpy(&img[7*512],&img[512],512);
    }
};

int main(int argc,char**argv){
    if(argc!=5){
        std::cerr<<"usage: mkbootmedia <out.img> <BOOTX64.EFI> <KERNEL.ELF> <rootfs.img>\n";
        return 1;
    }
    auto efi=readAll(argv[2]);
    auto kernel=readAll(argv[3]);
    auto rootfs=readAll(argv[4]);
    uint64_t need=efi.size()+kernel.size()+rootfs.size()+16*1024*1024ULL;
    uint32_t imageBytes=96u*1024u*1024u; while(imageBytes<need) imageBytes+=32u*1024u*1024u;
    Fat32Image fs(imageBytes);
    uint32_t efiDir=fs.makeDir(2,"EFI",0); uint32_t systemDir=fs.makeDir(2,"SYSTEM",1); uint32_t configDir=fs.makeDir(2,"CONFIG",2); uint32_t bootDir=fs.makeDir(efiDir,"BOOT",2);
    std::vector<uint8_t> nsh={'@','e','c','h','o',' ','-','o','f','f','\r','\n','\\','E','F','I','\\','B','O','O','T','\\','B','O','O','T','X','6','4','.','E','F','I','\r','\n'};
    fs.addFile(2,3,"STARTUP.NSH",nsh);
    fs.addFile(bootDir,2,"BOOTX64.EFI",efi);
    uint32_t kernelCluster=fs.addFile(systemDir,2,"KERNEL.ELF",kernel);
    uint32_t rootfsCluster=fs.addFile(systemDir,3,"ROOTFS.IMG",rootfs);
    // Root aliases remain as a firmware compatibility fallback.
    fs.dirEnt(2,4,"KERNEL.ELF",0x20,kernelCluster,(uint32_t)kernel.size());
    fs.dirEnt(2,5,"ROOTFS.IMG",0x20,rootfsCluster,(uint32_t)rootfs.size());
    std::string cfg="kernel=\\System\\KERNEL.ELF\r\nrootfs=\\System\\ROOTFS.IMG\r\nvideo=auto\r\nmode=normal\r\nkernel_crc32="+hex32(crc32(kernel))+"\r\nrootfs_crc32="+hex32(crc32(rootfs))+"\r\n";
    fs.addFile(configDir,2,"BOOT.CFG",std::vector<uint8_t>(cfg.begin(),cfg.end()));
    fs.finish();
    std::ofstream out(argv[1],std::ios::binary|std::ios::trunc); out.write((char*)fs.img.data(),fs.img.size()); out.close();
    std::cout<<"Created FAT32 ELF boot media: "<<argv[1]<<" ("<<fs.img.size()<<" bytes)\n";
    return 0;
}
