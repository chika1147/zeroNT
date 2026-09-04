#include "ntzero/drivers.h"

static int is_e1000(const NTZ_PCI_DEVICE *d) {
    if (d->vendor_id != 0x8086u) return 0;
    switch (d->device_id) {
        case 0x100Eu:
        case 0x100Fu:
        case 0x1010u:
        case 0x10D3u:
            return 1;
        default:
            return 0;
    }
}

static int is_rtl8139(const NTZ_PCI_DEVICE *d) {
    return d->vendor_id == 0x10ECu && d->device_id == 0x8139u;
}

NTZ_DRIVER_STATE NetworkProbe(const NTZ_PCI_DEVICE *device) {
    if (device == (const NTZ_PCI_DEVICE *)0) return NtzDriverAbsent;
    if (device->class_code != 0x02u) return NtzDriverAbsent;
    if (device->vendor_id == 0x1AF4u) return VirtioProbe(device);
    if (is_e1000(device)) return NtzDriverNeedsDma;
    if (is_rtl8139(device)) return NtzDriverNeedsDma;
    return NtzDriverDetected;
}
