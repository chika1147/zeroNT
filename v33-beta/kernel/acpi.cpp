#include "include/acpi.hpp"
#include "include/arch.hpp"
#include <stdint.h>
#include <stddef.h>

namespace {
#pragma pack(push,1)
struct Rsdp { char sig[8]; uint8_t checksum; char oem[6]; uint8_t rev; uint32_t rsdt; uint32_t len; uint64_t xsdt; uint8_t extChecksum; uint8_t reserved[3]; };
struct Sdt { char sig[4]; uint32_t len; uint8_t rev,checksum; char oem[6]; char table[8]; uint32_t oemRev,creatorId,creatorRev; };
struct Gas { uint8_t spaceId,bitWidth,bitOffset,accessSize; uint64_t address; };
struct MadtHead { Sdt h; uint32_t lapicAddress; uint32_t flags; };
struct HpetTable { Sdt h; uint8_t hwRev; uint8_t comparatorInfo; uint16_t pciVendor; Gas address; uint8_t number; uint16_t minTick; uint8_t pageProtection; };
#pragma pack(pop)
static AcpiStats g{};

static bool sig4(const char*a,const char*b){return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3];}
static bool sig8(const char*a,const char*b){for(int i=0;i<8;++i)if(a[i]!=b[i])return false;return true;}
static bool checksum(const void*p,uint32_t n){if(!p||!n)return false;const uint8_t*b=static_cast<const uint8_t*>(p);uint8_t s=0;for(uint32_t i=0;i<n;++i)s=static_cast<uint8_t>(s+b[i]);return s==0;}

static bool valid_sdt(const Sdt* h){return h&&h->len>=sizeof(Sdt)&&h->len<(16u*1024u*1024u)&&checksum(h,h->len);}

static void parse_fadt(Sdt* h){
    if(!valid_sdt(h)||h->len<116)return;
    g.fadt=true;
    const uint8_t*b=reinterpret_cast<const uint8_t*>(h);
    if(h->len>=129){
        const Gas* reset=reinterpret_cast<const Gas*>(b+116);
        g.resetSpaceId=reset->spaceId;
        g.resetAddress=reset->address;
        g.resetValue=b[128];
    }
}

static void parse_madt(Sdt* base){
    if(!valid_sdt(base)||base->len<sizeof(MadtHead))return;
    auto* h=reinterpret_cast<MadtHead*>(base);
    g.madt=true;
    g.localApicAddress=h->lapicAddress;
    const uint8_t* p=reinterpret_cast<const uint8_t*>(h)+sizeof(MadtHead);
    const uint8_t* end=reinterpret_cast<const uint8_t*>(h)+h->h.len;
    while(p+2<=end){
        const uint8_t type=p[0],len=p[1];
        if(len<2||p+len>end)break;
        if(type==0&&len>=8){
            const uint32_t flags=*reinterpret_cast<const uint32_t*>(p+4);
            if(flags&1u)++g.enabledCpuCount;
        } else if(type==1&&len>=12){
            if(!g.ioApicAddress){g.ioApicAddress=*reinterpret_cast<const uint32_t*>(p+4);g.ioApicGsiBase=*reinterpret_cast<const uint32_t*>(p+8);}
        } else if(type==2&&len>=10){
            ++g.interruptOverrides;
        } else if(type==5&&len>=12){
            g.localApicAddress=*reinterpret_cast<const uint64_t*>(p+4);
        }
        p+=len;
    }
}

static void parse_hpet(Sdt* base){
    if(!valid_sdt(base)||base->len<sizeof(HpetTable))return;
    auto*h=reinterpret_cast<HpetTable*>(base);
    if(h->address.address){g.hpet=true;g.hpetAddress=h->address.address;}
}
}

bool acpi_init(uint64_t addr){
    g={};g.rsdpAddress=addr;if(!addr)return false;
    auto*r=reinterpret_cast<Rsdp*>(static_cast<uintptr_t>(addr));
    if(!sig8(r->sig,"RSD PTR ")||!checksum(r,20))return false;
    if(r->rev>=2&&r->len>=sizeof(Rsdp)&&!checksum(r,r->len))return false;
    g.rsdp=true;
    Sdt*root=nullptr;bool useXsdt=r->rev>=2&&r->xsdt;
    if(useXsdt){root=reinterpret_cast<Sdt*>(static_cast<uintptr_t>(r->xsdt));g.xsdt=true;g.rootAddress=r->xsdt;}
    else{root=reinterpret_cast<Sdt*>(static_cast<uintptr_t>(r->rsdt));g.rootAddress=r->rsdt;}
    if(!valid_sdt(root))return true;
    const uint32_t entSize=useXsdt?8u:4u;
    const uint32_t count=(root->len-sizeof(Sdt))/entSize;g.tableCount=count;
    const uint8_t*base=reinterpret_cast<const uint8_t*>(root)+sizeof(Sdt);
    for(uint32_t i=0;i<count;++i){
        const uint64_t a=useXsdt?reinterpret_cast<const uint64_t*>(base)[i]:reinterpret_cast<const uint32_t*>(base)[i];
        auto*h=reinterpret_cast<Sdt*>(static_cast<uintptr_t>(a));
        if(!valid_sdt(h))continue;
        if(sig4(h->sig,"FACP"))parse_fadt(h);
        else if(sig4(h->sig,"APIC"))parse_madt(h);
        else if(sig4(h->sig,"HPET"))parse_hpet(h);
        else if(sig4(h->sig,"MCFG")){g.mcfg=true;g.mcfgAddress=a;}
    }
    return true;
}

AcpiStats acpi_stats(){return g;}
uint64_t acpi_local_apic_address(){return g.localApicAddress;}
uint64_t acpi_io_apic_address(){return g.ioApicAddress;}
uint64_t acpi_hpet_address(){return g.hpetAddress;}

[[noreturn]] void acpi_reboot(){
    if(g.resetAddress&&g.resetSpaceId==1&&g.resetAddress<=0xFFFFu)outb(static_cast<uint16_t>(g.resetAddress),g.resetValue);
    outb(0x64,0xFE);
    for(;;)cpu_halt();
}

[[noreturn]] void acpi_poweroff(){
    // QEMU/Bochs/VMware-compatible fallbacks.  Full AML S5 evaluation is a
    // separate ACPI interpreter task; flushing is performed before this call.
    outw(0x604,0x2000);outw(0xB004,0x2000);
    for(;;)cpu_halt();
}
