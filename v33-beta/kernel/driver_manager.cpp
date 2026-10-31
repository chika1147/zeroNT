#include "include/driver_manager.hpp"
#include "include/pci.hpp"
#include "include/usb.hpp"
#include "include/platform_hardware.hpp"

namespace {
static DriverManagerStats g{};
static uint64_t gLastPollTick=0;
static uint64_t gLastUsbHotplug=0;
static uint32_t gLastPciCount=0;

static void count(const PciDeviceInfo&i,void*) {
    ++g.pciDevices;
    if(i.classCode==0x01) ++g.storageControllers;
    if(i.classCode==0x0C && i.subclass==0x03) ++g.usbControllers;
    if(i.classCode==0x02) ++g.networkControllers;
    if(i.classCode==0x04) ++g.audioControllers;
    if(i.classCode==0x01 && i.subclass==0x06 && i.progIf==0x01) g.ahci=true;
    PciCapabilityInfo c{};if(pci_get_capabilities(i,c)){if(c.msi)++g.msiDevices;if(c.msix)++g.msixDevices;g.malformedCapabilityLists+=c.malformedLists;}
}
static uint32_t scan_count(){uint32_t n=0;pci_enumerate([](const PciDeviceInfo&,void*x){++*static_cast<uint32_t*>(x);},&n);return n;}
}

void driver_manager_init_pci() {
    const bool keepNvme=g.nvme,keepXhci=g.xhci,keepAcpi=g.acpi;
    const uint64_t hot=g.hotplugEvents,topo=g.pciTopologyChanges,polls=g.pollCount;
    g={};g.nvme=keepNvme;g.xhci=keepXhci;g.acpi=keepAcpi;g.hotplugEvents=hot;g.pciTopologyChanges=topo;g.pollCount=polls;
    pci_enumerate(count,nullptr);gLastPciCount=g.pciDevices;
    auto p=platform_hardware_stats();g.localApic=p.localApicMapped;g.hpet=p.hpetRunning;
}

void driver_manager_update_optional(bool nvme, bool xhci, bool acpi) {
    g.nvme = nvme; g.xhci = xhci; g.acpi = acpi;
    auto p=platform_hardware_stats();g.localApic=p.localApicMapped;g.hpet=p.hpetRunning;
    auto x=xhci_stats();gLastUsbHotplug=x.hotplugEvents;
}

void driver_manager_init(const ZeroNTBootInfo* boot) {driver_manager_init_pci();driver_manager_update_optional(false,false,boot&&boot->acpiRsdp);}

void driver_manager_poll(uint64_t ticks){
    if(ticks-gLastPollTick<500)return;gLastPollTick=ticks;++g.pollCount;
    auto x=xhci_stats();if(x.hotplugEvents>gLastUsbHotplug){g.hotplugEvents+=x.hotplugEvents-gLastUsbHotplug;gLastUsbHotplug=x.hotplugEvents;}
    const uint32_t now=scan_count();if(now!=gLastPciCount){++g.pciTopologyChanges;gLastPciCount=now;g.pciDevices=now;}
    auto p=platform_hardware_stats();g.localApic=p.localApicMapped;g.hpet=p.hpetRunning;
}
DriverManagerStats driver_manager_stats(){return g;}
