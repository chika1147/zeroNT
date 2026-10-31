#pragma once
#include <stdint.h>
#include "pci.hpp"
#include "block_device.hpp"

struct UsbProbeStats {
    uint32_t controllers;
    uint32_t uhci;
    uint32_t ohci;
    uint32_t ehci;
    uint32_t xhci;
    bool xhciFound;
    uint64_t xhciBar;
};

struct XhciStats {
    bool found;
    bool mmioMapped;
    bool resetComplete;
    bool commandRingReady;
    bool eventRingReady;
    bool running;
    uint8_t bus,device,function;
    uint16_t vendorId,deviceId;
    uint64_t bar0;
    uint8_t maxSlots;
    uint8_t maxPorts;
    uint8_t connectedPorts;
    uint32_t pageSize;
    uint32_t scratchpadCount;
    uint64_t resets;
    uint64_t timeouts;
    uint64_t eventsProcessed;
    uint64_t portStatusEvents;
    uint64_t commandCompletions;
    uint64_t hotplugEvents;
    uint64_t connectedBitmap;
    uint32_t enumeratedDevices;
    uint32_t hidKeyboards;
    uint32_t hidMice;
    uint32_t massStorageDevices;
    uint64_t controlTransfers;
    uint64_t interruptTransfers;
    uint64_t bulkTransfers;
};

struct UsbMouseEvent { int16_t dx; int16_t dy; int8_t wheel; uint8_t buttons; };

void usb_probe();
UsbProbeStats usb_probe_stats();

// xHCI host + basic class layer. The implementation supports root-port device
// enumeration, boot-protocol HID keyboard/mouse and USB Mass Storage BOT/SCSI.
bool xhci_init();
void xhci_poll();
XhciStats xhci_stats();

bool usb_keyboard_pop(uint8_t& set1Scancode);
bool usb_mouse_pop(UsbMouseEvent& event);
BlockDevice* usb_mass_storage_device();
