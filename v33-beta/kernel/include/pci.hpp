#pragma once
#include <stdint.h>

struct PciDeviceInfo {
    bool valid;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendorId;
    uint16_t deviceId;
    uint8_t classCode;
    uint8_t subclass;
    uint8_t progIf;
    uint8_t revision;
    uint8_t headerType;
    uint32_t bar[6];
};

struct PciCapabilityInfo {
    bool capabilityList;
    bool msi;
    bool msix;
    uint8_t msiOffset;
    uint8_t msixOffset;
    uint16_t msixTableSize;
    uint32_t malformedLists;
};

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
bool pci_find_ahci(PciDeviceInfo& out);
void pci_enable_mmio_busmaster(const PciDeviceInfo& dev);
void pci_enable_io_busmaster(const PciDeviceInfo& dev);
bool pci_get_capabilities(const PciDeviceInfo& dev, PciCapabilityInfo& out);
bool pci_configure_msi(const PciDeviceInfo& dev, uint8_t vector, uint8_t destinationApicId);
using PciEnumerateCallback = void (*)(const PciDeviceInfo& info, void* context);
void pci_enumerate(PciEnumerateCallback callback, void* context);
