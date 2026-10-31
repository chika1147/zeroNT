#include "include/platform_hardware.hpp"
#include "include/acpi.hpp"
#include "include/pci.hpp"
#include "include/paging.hpp"
#include <stdint.h>

namespace {
constexpr uint64_t PAGE_SIZE=4096;
static PlatformHardwareStats g{};
static volatile uint8_t* gLapic=nullptr;
static volatile uint8_t* gHpet=nullptr;

static bool cpu_has_apic(){
    uint32_t a=1,b=0,c=0,d=0;
    __asm__ __volatile__("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
    return (d&(1u<<9))!=0;
}
static bool map_mmio(uint64_t address,uint64_t bytes){
    if(!address||!bytes)return false;
    if(address<0x100000000ULL)return true;
    uint64_t s=address&~(PAGE_SIZE-1),e=(address+bytes+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    if(e<address)return false;
    for(uint64_t a=s;a<e;a+=PAGE_SIZE)if(!vmm_map_page(a,a,PagePresent|PageWritable|PageCacheDisable))return false;
    return true;
}
static uint32_t mmio32(volatile uint8_t*b,uint32_t o){return *reinterpret_cast<volatile uint32_t*>(b+o);}
static uint64_t mmio64(volatile uint8_t*b,uint32_t o){return *reinterpret_cast<volatile uint64_t*>(b+o);}
static void mmio64w(volatile uint8_t*b,uint32_t o,uint64_t v){*reinterpret_cast<volatile uint64_t*>(b+o)=v;}

static void count_caps(const PciDeviceInfo&i,void*){
    PciCapabilityInfo c{};if(!pci_get_capabilities(i,c))return;
    if(c.msi)++g.pciMsiDevices;
    if(c.msix)++g.pciMsixDevices;
    g.pciMalformedCapabilityLists+=c.malformedLists;
}
}

bool platform_hardware_init(){
    g={};g.cpuApicSupported=cpu_has_apic();
    const auto acpi=acpi_stats();
    g.localApicAddress=acpi.localApicAddress;
    if(g.cpuApicSupported&&g.localApicAddress){
        g.localApicFound=true;
        if(map_mmio(g.localApicAddress,PAGE_SIZE)){
            g.localApicMapped=true;gLapic=reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(g.localApicAddress));
            g.localApicId=(mmio32(gLapic,0x20)>>24)&0xFFu;g.localApicVersion=mmio32(gLapic,0x30)&0xFFu;
            // Keep legacy PIC routing for Beta compatibility, but ensure the local
            // APIC itself is software-enabled so MSI can be adopted incrementally.
            uint32_t svr=mmio32(gLapic,0xF0);svr|=(1u<<8);svr=(svr&~0xFFu)|0xFFu;*reinterpret_cast<volatile uint32_t*>(gLapic+0xF0)=svr;
        }
    }
    g.hpetAddress=acpi.hpetAddress;
    if(g.hpetAddress){
        g.hpetFound=true;
        if(map_mmio(g.hpetAddress,PAGE_SIZE)){
            gHpet=reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(g.hpetAddress));
            const uint64_t caps=mmio64(gHpet,0x00);const uint64_t periodFs=caps>>32;
            if(periodFs){
                g.hpetFrequencyHz=1000000000000000ULL/periodFs;
                uint64_t cfg=mmio64(gHpet,0x10);cfg&=~1ULL;mmio64w(gHpet,0x10,cfg);mmio64w(gHpet,0xF0,0);mmio64w(gHpet,0x10,cfg|1ULL);g.hpetRunning=true;
            }
        }
    }
    pci_enumerate(count_caps,nullptr);
    platform_hardware_poll();
    return g.localApicMapped||g.hpetRunning||g.pciMsiDevices||g.pciMsixDevices;
}

void platform_hardware_poll(){if(g.hpetRunning&&gHpet)g.hpetCounter=mmio64(gHpet,0xF0);}
PlatformHardwareStats platform_hardware_stats(){platform_hardware_poll();return g;}
uint64_t platform_monotonic_counter(){platform_hardware_poll();return g.hpetRunning?g.hpetCounter:0;}
