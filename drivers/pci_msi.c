#include "ntzero/drivers.h"

#define PCI_STATUS_CAP_LIST 0x0010u
#define PCI_CAP_ID_MSI  0x05u
#define PCI_CAP_ID_MSIX 0x11u

static u16 cfg_read16(const NTZ_PCI_DEVICE *d, u8 offset) {
    u32 value = PciConfigRead32(d->bus, d->device, d->function, (u8)(offset & 0xFCu));
    u32 shift = (u32)(offset & 2u) * 8u;
    return (u16)((value >> shift) & 0xFFFFu);
}

static u8 cfg_read8(const NTZ_PCI_DEVICE *d, u8 offset) {
    u32 value = PciConfigRead32(d->bus, d->device, d->function, (u8)(offset & 0xFCu));
    u32 shift = (u32)(offset & 3u) * 8u;
    return (u8)((value >> shift) & 0xFFu);
}

static void cfg_write16(const NTZ_PCI_DEVICE *d, u8 offset, u16 value) {
    u8 aligned = (u8)(offset & 0xFCu);
    u32 old = PciConfigRead32(d->bus, d->device, d->function, aligned);
    u32 shift = (u32)(offset & 2u) * 8u;
    u32 mask = 0xFFFFu << shift;
    u32 next = (old & ~mask) | ((u32)value << shift);
    PciConfigWrite32(d->bus, d->device, d->function, aligned, next);
}

u8 PciFindCapability(const NTZ_PCI_DEVICE *device, u8 capability_id) {
    u16 status;
    u8 pointer;
    u32 guard = 0u;
    if (device == (const NTZ_PCI_DEVICE *)0) return 0u;
    status = cfg_read16(device, 0x06u);
    if ((status & PCI_STATUS_CAP_LIST) == 0u) return 0u;
    pointer = (u8)(cfg_read8(device, 0x34u) & 0xFCu);
    while (pointer >= 0x40u && guard++ < 48u) {
        u8 id = cfg_read8(device, pointer);
        if (id == capability_id) return pointer;
        pointer = (u8)(cfg_read8(device, (u8)(pointer + 1u)) & 0xFCu);
    }
    return 0u;
}

int PciHasMsi(const NTZ_PCI_DEVICE *device) {
    return PciFindCapability(device, PCI_CAP_ID_MSI) != 0u;
}

int PciHasMsix(const NTZ_PCI_DEVICE *device) {
    return PciFindCapability(device, PCI_CAP_ID_MSIX) != 0u;
}

NTSTATUS PciEnableMsi(const NTZ_PCI_DEVICE *device, u8 vector, u8 destination_apic_id) {
    u8 cap;
    u16 control;
    u32 message_address;
    u16 message_data;
    if (device == (const NTZ_PCI_DEVICE *)0 || vector < 0x20u) return STATUS_INVALID_PARAMETER;
    cap = PciFindCapability(device, PCI_CAP_ID_MSI);
    if (cap == 0u) return STATUS_NOT_FOUND;

    control = cfg_read16(device, (u8)(cap + 2u));
    control &= (u16)~(7u << 4); /* one message */
    message_address = 0xFEE00000u | ((u32)destination_apic_id << 12);
    message_data = (u16)vector;

    PciConfigWrite32(device->bus, device->device, device->function, (u8)(cap + 4u), message_address);
    if ((control & (1u << 7)) != 0u) {
        PciConfigWrite32(device->bus, device->device, device->function, (u8)(cap + 8u), 0u);
        cfg_write16(device, (u8)(cap + 12u), message_data);
    } else {
        cfg_write16(device, (u8)(cap + 8u), message_data);
    }
    control |= 1u;
    cfg_write16(device, (u8)(cap + 2u), control);
    return STATUS_SUCCESS;
}
