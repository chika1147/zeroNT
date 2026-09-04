#include "ntzero/drivers.h"

NTZ_DRIVER_STATE XhciProbe(const NTZ_PCI_DEVICE *device) {
    u32 bar0;
    if (device == (const NTZ_PCI_DEVICE *)0) return NtzDriverAbsent;
    if (device->class_code != 0x0Cu || device->subclass != 0x03u || device->prog_if != 0x30u) {
        return NtzDriverAbsent;
    }
    bar0 = device->bar[0];
    if ((bar0 & 1u) != 0u || (bar0 & ~0xFu) == 0u) {
        return NtzDriverUnsupported;
    }
    return NtzDriverNeedsDma;
}
