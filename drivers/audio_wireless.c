#include "ntzero/drivers.h"

NTZ_DRIVER_STATE AudioProbe(const NTZ_PCI_DEVICE *device) {
    if (device == (const NTZ_PCI_DEVICE *)0) return NtzDriverAbsent;
    if (device->class_code != 0x04u) return NtzDriverAbsent;
    if (device->subclass == 0x03u) {
        if ((device->bar[0] & 1u) == 0u && (device->bar[0] & ~0xFu) != 0u) {
            return NtzDriverNeedsDma; /* Intel/AMD HDA-style controller */
        }
        return NtzDriverUnsupported;
    }
    return NtzDriverDetected;
}

NTZ_DRIVER_STATE WirelessProbe(const NTZ_PCI_DEVICE *device) {
    if (device == (const NTZ_PCI_DEVICE *)0) return NtzDriverAbsent;
    if (device->class_code != 0x02u || device->subclass != 0x80u) return NtzDriverAbsent;

    switch (device->vendor_id) {
        case 0x8086u: /* Intel */
        case 0x10ECu: /* Realtek */
        case 0x168Cu: /* Qualcomm Atheros */
        case 0x14E4u: /* Broadcom */
        case 0x17CBu: /* Qualcomm */
            return NtzDriverNeedsDma;
        default:
            return NtzDriverDetected;
    }
}
