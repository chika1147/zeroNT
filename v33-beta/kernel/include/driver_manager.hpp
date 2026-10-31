#pragma once
#include <stdint.h>
#include "../../shared/bootinfo.hpp"

struct DriverManagerStats {
    uint32_t pciDevices;
    uint32_t storageControllers;
    uint32_t usbControllers;
    uint32_t networkControllers;
    uint32_t audioControllers;
    uint32_t msiDevices;
    uint32_t msixDevices;
    uint32_t malformedCapabilityLists;
    uint64_t hotplugEvents;
    uint64_t pciTopologyChanges;
    uint64_t pollCount;
    bool ahci;
    bool nvme;
    bool acpi;
    bool xhci;
    bool localApic;
    bool hpet;
};
void driver_manager_init(const ZeroNTBootInfo* boot);
void driver_manager_init_pci();
void driver_manager_update_optional(bool nvme, bool xhci, bool acpi);
void driver_manager_poll(uint64_t ticks);
DriverManagerStats driver_manager_stats();
