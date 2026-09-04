#include "ntzero/drivers.h"
#include "ntzero/hw.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

static u32 pci_address(u8 bus, u8 device, u8 function, u8 offset) {
    return 0x80000000u |
           ((u32)bus << 16) |
           ((u32)(device & 0x1Fu) << 11) |
           ((u32)(function & 0x07u) << 8) |
           ((u32)offset & 0xFCu);
}

u32 PciConfigRead32(u8 bus, u8 device, u8 function, u8 offset) {
    IoOut32(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    return IoIn32(PCI_CONFIG_DATA);
}

void PciConfigWrite32(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    IoOut32(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    IoOut32(PCI_CONFIG_DATA, value);
}

static u16 pci_read16(u8 bus, u8 device, u8 function, u8 offset) {
    u32 value = PciConfigRead32(bus, device, function, (u8)(offset & 0xFCu));
    u32 shift = (u32)(offset & 2u) * 8u;
    return (u16)((value >> shift) & 0xFFFFu);
}

static u8 pci_read8(u8 bus, u8 device, u8 function, u8 offset) {
    u32 value = PciConfigRead32(bus, device, function, (u8)(offset & 0xFCu));
    u32 shift = (u32)(offset & 3u) * 8u;
    return (u8)((value >> shift) & 0xFFu);
}

static void pci_fill_device(NTZ_PCI_DEVICE *out, u8 bus, u8 dev, u8 fn) {
    u32 id = PciConfigRead32(bus, dev, fn, 0x00u);
    u32 class_info = PciConfigRead32(bus, dev, fn, 0x08u);
    u32 i;

    out->bus = bus;
    out->device = dev;
    out->function = fn;
    out->vendor_id = (u16)(id & 0xFFFFu);
    out->device_id = (u16)(id >> 16);
    out->revision = (u8)(class_info & 0xFFu);
    out->prog_if = (u8)((class_info >> 8) & 0xFFu);
    out->subclass = (u8)((class_info >> 16) & 0xFFu);
    out->class_code = (u8)((class_info >> 24) & 0xFFu);
    out->header_type = pci_read8(bus, dev, fn, 0x0Eu);
    out->irq_line = pci_read8(bus, dev, fn, 0x3Cu);
    for (i = 0u; i < 6u; ++i) {
        out->bar[i] = PciConfigRead32(bus, dev, fn, (u8)(0x10u + i * 4u));
    }
}

u32 PciEnumerate(NTZ_PCI_VISITOR visitor, void *context) {
    u32 count = 0u;
    u32 bus;
    u32 dev;
    u32 fn;

    for (bus = 0u; bus < 256u; ++bus) {
        for (dev = 0u; dev < 32u; ++dev) {
            u16 vendor0 = pci_read16((u8)bus, (u8)dev, 0u, 0x00u);
            u8 functions;
            if (vendor0 == 0xFFFFu) {
                continue;
            }
            functions = (pci_read8((u8)bus, (u8)dev, 0u, 0x0Eu) & 0x80u) != 0u ? 8u : 1u;
            for (fn = 0u; fn < functions; ++fn) {
                NTZ_PCI_DEVICE info;
                if (pci_read16((u8)bus, (u8)dev, (u8)fn, 0x00u) == 0xFFFFu) {
                    continue;
                }
                pci_fill_device(&info, (u8)bus, (u8)dev, (u8)fn);
                ++count;
                if (visitor != (NTZ_PCI_VISITOR)0) {
                    visitor(&info, context);
                }
            }
        }
    }
    return count;
}
