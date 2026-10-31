#include "include/pci.hpp"
#include "include/arch.hpp"

static uint32_t pci_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return 0x80000000u | (static_cast<uint32_t>(bus) << 16) |
           (static_cast<uint32_t>(device) << 11) | (static_cast<uint32_t>(function) << 8) |
           (offset & 0xFCu);
}

uint32_t pci_read32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset){outl(0xCF8,pci_address(bus,device,function,offset));return inl(0xCFC);}
uint16_t pci_read16(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset){uint32_t v=pci_read32(bus,device,function,offset);return static_cast<uint16_t>((v>>((offset&2u)*8u))&0xFFFFu);}
uint8_t pci_read8(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset){uint32_t v=pci_read32(bus,device,function,offset);return static_cast<uint8_t>((v>>((offset&3u)*8u))&0xFFu);}
void pci_write32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset,uint32_t value){outl(0xCF8,pci_address(bus,device,function,offset));outl(0xCFC,value);}
void pci_write16(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset,uint16_t value){const uint8_t a=offset&0xFCu;uint32_t old=pci_read32(bus,device,function,a);const uint32_t shift=(offset&2u)*8u;old=(old&~(0xFFFFu<<shift))|(static_cast<uint32_t>(value)<<shift);pci_write32(bus,device,function,a,old);}

static bool fill_device(uint8_t bus,uint8_t dev,uint8_t fn,PciDeviceInfo&out){
    const uint32_t vd=pci_read32(bus,dev,fn,0x00);const uint16_t vendor=static_cast<uint16_t>(vd&0xFFFFu);if(vendor==0xFFFFu)return false;
    const uint32_t cls=pci_read32(bus,dev,fn,0x08),hdr=pci_read32(bus,dev,fn,0x0C);out={};out.valid=true;out.bus=bus;out.device=dev;out.function=fn;out.vendorId=vendor;out.deviceId=static_cast<uint16_t>(vd>>16);out.revision=static_cast<uint8_t>(cls&0xFFu);out.progIf=static_cast<uint8_t>((cls>>8)&0xFFu);out.subclass=static_cast<uint8_t>((cls>>16)&0xFFu);out.classCode=static_cast<uint8_t>((cls>>24)&0xFFu);out.headerType=static_cast<uint8_t>((hdr>>16)&0xFFu);for(uint8_t i=0;i<6;++i)out.bar[i]=pci_read32(bus,dev,fn,static_cast<uint8_t>(0x10+i*4));return true;
}

bool pci_find_ahci(PciDeviceInfo&out){
    struct C{PciDeviceInfo*out;bool found;};C c{&out,false};
    pci_enumerate([](const PciDeviceInfo&i,void*x){auto*c=static_cast<C*>(x);if(c->found)return;if(i.classCode==0x01&&i.subclass==0x06&&i.progIf==0x01){*c->out=i;c->found=true;}},&c);
    if(!c.found)out={};return c.found;
}

void pci_enable_mmio_busmaster(const PciDeviceInfo&dev){if(!dev.valid)return;uint16_t command=pci_read16(dev.bus,dev.device,dev.function,0x04);command|=(1u<<1)|(1u<<2);pci_write16(dev.bus,dev.device,dev.function,0x04,command);}
void pci_enable_io_busmaster(const PciDeviceInfo&dev){if(!dev.valid)return;uint16_t command=pci_read16(dev.bus,dev.device,dev.function,0x04);command|=(1u<<0)|(1u<<2);pci_write16(dev.bus,dev.device,dev.function,0x04,command);}

bool pci_get_capabilities(const PciDeviceInfo&dev,PciCapabilityInfo&out){
    out={};if(!dev.valid)return false;const uint16_t status=pci_read16(dev.bus,dev.device,dev.function,0x06);if(!(status&(1u<<4)))return true;out.capabilityList=true;
    uint8_t ptr=static_cast<uint8_t>(pci_read8(dev.bus,dev.device,dev.function,0x34)&~3u);uint64_t seen=0;
    for(uint32_t hops=0;ptr>=0x40&&hops<48;++hops){
        const uint8_t slot=static_cast<uint8_t>(ptr>>2);if(slot<64){const uint64_t bit=1ULL<<slot;if(seen&bit){++out.malformedLists;break;}seen|=bit;}
        const uint8_t id=pci_read8(dev.bus,dev.device,dev.function,ptr),next=static_cast<uint8_t>(pci_read8(dev.bus,dev.device,dev.function,static_cast<uint8_t>(ptr+1))&~3u);
        if(id==0x05){out.msi=true;out.msiOffset=ptr;}else if(id==0x11){out.msix=true;out.msixOffset=ptr;const uint16_t ctl=pci_read16(dev.bus,dev.device,dev.function,static_cast<uint8_t>(ptr+2));out.msixTableSize=static_cast<uint16_t>((ctl&0x07FFu)+1u);}
        if(!next)break;if(next==ptr||next<0x40){++out.malformedLists;break;}ptr=next;
    }
    return true;
}

bool pci_configure_msi(const PciDeviceInfo&dev,uint8_t vector,uint8_t destinationApicId){
    PciCapabilityInfo caps{};if(!pci_get_capabilities(dev,caps)||!caps.msi||vector<32)return false;const uint8_t cap=caps.msiOffset;uint16_t ctl=pci_read16(dev.bus,dev.device,dev.function,static_cast<uint8_t>(cap+2));const bool addr64=(ctl&(1u<<7))!=0;const uint32_t address=0xFEE00000u|(static_cast<uint32_t>(destinationApicId)<<12);pci_write32(dev.bus,dev.device,dev.function,static_cast<uint8_t>(cap+4),address);uint8_t dataOff=static_cast<uint8_t>(cap+(addr64?12:8));if(addr64)pci_write32(dev.bus,dev.device,dev.function,static_cast<uint8_t>(cap+8),0);pci_write16(dev.bus,dev.device,dev.function,dataOff,vector);ctl|=1u;ctl&=static_cast<uint16_t>(~(7u<<4));pci_write16(dev.bus,dev.device,dev.function,static_cast<uint8_t>(cap+2),ctl);return true;
}

namespace {
static void enumerate_bus(uint8_t bus,PciEnumerateCallback callback,void*context,bool visited[256]){
    if(visited[bus])return;visited[bus]=true;
    for(uint8_t dev=0;dev<32;++dev){const uint32_t first=pci_read32(bus,dev,0,0x00);if((first&0xFFFFu)==0xFFFFu)continue;const uint8_t header=static_cast<uint8_t>((pci_read32(bus,dev,0,0x0C)>>16)&0xFFu);const uint8_t fns=(header&0x80u)?8:1;for(uint8_t fn=0;fn<fns;++fn){PciDeviceInfo info{};if(!fill_device(bus,dev,fn,info))continue;callback(info,context);if(info.classCode==0x06&&info.subclass==0x04){const uint32_t buses=pci_read32(bus,dev,fn,0x18);const uint8_t secondary=static_cast<uint8_t>((buses>>8)&0xFFu);if(secondary&&secondary!=bus)enumerate_bus(secondary,callback,context,visited);}}}
}
}
void pci_enumerate(PciEnumerateCallback callback,void*context){if(!callback)return;bool visited[256]{};enumerate_bus(0,callback,context,visited);}
