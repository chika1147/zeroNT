#include "ntzero/drivers.h"

NTZ_DRIVER_STATE VirtioProbe(const NTZ_PCI_DEVICE *device) {
    if (device == (const NTZ_PCI_DEVICE *)0 || device->vendor_id != 0x1AF4u) {
        return NtzDriverAbsent;
    }

    switch (device->device_id) {
        case 0x1000u: /* legacy net */
        case 0x1041u: /* modern net */
        case 0x1001u: /* legacy block */
        case 0x1042u: /* modern block */
        case 0x1003u: /* console */
        case 0x1043u: /* modern console */
        case 0x1050u: /* GPU */
            return NtzDriverNeedsDma;
        default:
            if (device->device_id >= 0x1040u && device->device_id <= 0x107Fu) {
                return NtzDriverDetected;
            }
            return NtzDriverUnsupported;
    }
}
