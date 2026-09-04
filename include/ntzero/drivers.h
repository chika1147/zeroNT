#ifndef NTZERO_DRIVERS_H
#define NTZERO_DRIVERS_H

#include "types.h"
#include "boot.h"

typedef enum NTZ_DRIVER_STATE {
    NtzDriverAbsent = 0,
    NtzDriverDetected = 1,
    NtzDriverReady = 2,
    NtzDriverNeedsDma = 3,
    NtzDriverNeedsInterrupts = 4,
    NtzDriverUnsupported = 5
} NTZ_DRIVER_STATE;

typedef struct NTZ_PCI_DEVICE {
    u8 bus;
    u8 device;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision;
    u8 header_type;
    u8 irq_line;
    u32 bar[6];
} NTZ_PCI_DEVICE;

typedef void (*NTZ_PCI_VISITOR)(const NTZ_PCI_DEVICE *device, void *context);

typedef struct NTZ_DRIVER_SUMMARY {
    u32 pci_devices;
    u32 storage_controllers;
    u32 usb_controllers;
    u32 network_controllers;
    u32 wireless_controllers;
    u32 audio_controllers;
    u32 virtio_devices;
    u32 input_controllers;
    u32 timers;
    u32 display_devices;
} NTZ_DRIVER_SUMMARY;

typedef struct NTZ_FRAMEBUFFER {
    volatile u32 *base;
    u32 width;
    u32 height;
    u32 pixels_per_scanline;
} NTZ_FRAMEBUFFER;

u32 PciConfigRead32(u8 bus, u8 device, u8 function, u8 offset);
void PciConfigWrite32(u8 bus, u8 device, u8 function, u8 offset, u32 value);
u32 PciEnumerate(NTZ_PCI_VISITOR visitor, void *context);

NTSTATUS AcpiInitialize(void *rsdp);
void *AcpiFindTable(const char signature[4]);

NTSTATUS PitInitialize(u32 hz);
u8 RtcReadRegister(u8 reg);
NTSTATUS Ps2Initialize(void);
int Ps2PollKeyboard(u8 *scan_code);
int Ps2PollMouse(u8 packet[3]);

void LapicConfigure(u64 base);
void LapicEnable(u8 spurious_vector);
void LapicEoi(void);
void IoApicConfigure(u64 base);
void IoApicRoute(u8 irq, u8 vector, u8 destination_apic_id, int masked);

NTZ_DRIVER_STATE AhciProbe(const NTZ_PCI_DEVICE *device);
NTZ_DRIVER_STATE NvmeProbe(const NTZ_PCI_DEVICE *device);
NTZ_DRIVER_STATE XhciProbe(const NTZ_PCI_DEVICE *device);
NTZ_DRIVER_STATE VirtioProbe(const NTZ_PCI_DEVICE *device);
NTZ_DRIVER_STATE NetworkProbe(const NTZ_PCI_DEVICE *device);
NTZ_DRIVER_STATE AudioProbe(const NTZ_PCI_DEVICE *device);
NTZ_DRIVER_STATE WirelessProbe(const NTZ_PCI_DEVICE *device);

NTSTATUS FramebufferConfigure(const NTZ_FRAMEBUFFER *framebuffer);
void FramebufferPutPixel(u32 x, u32 y, u32 argb);
void FramebufferClear(u32 argb);

void DriversInitializeAll(const NTZ_BOOT_INFO *boot, NTZ_DRIVER_SUMMARY *summary);

#endif
