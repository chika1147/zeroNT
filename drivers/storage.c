#include "ntzero/drivers.h"

static int pci_is_ahci(const NTZ_PCI_DEVICE *d) {
    return d != (const NTZ_PCI_DEVICE *)0 && d->class_code == 0x01u && d->subclass == 0x06u && d->prog_if == 0x01u;
}

static int pci_is_nvme(const NTZ_PCI_DEVICE *d) {
    return d != (const NTZ_PCI_DEVICE *)0 && d->class_code == 0x01u && d->subclass == 0x08u && d->prog_if == 0x02u;
}

static u64 pci_mmio_bar(const NTZ_PCI_DEVICE *d, u32 index) {
    u32 low;
    u64 address;
    if (d == (const NTZ_PCI_DEVICE *)0 || index >= 6u) return 0u;
    low = d->bar[index];
    if ((low & 1u) != 0u) return 0u;
    address = (u64)(low & ~0xFu);
    if ((low & 0x06u) == 0x04u && index + 1u < 6u) {
        address |= (u64)d->bar[index + 1u] << 32;
    }
    return address;
}

NTZ_DRIVER_STATE AhciProbe(const NTZ_PCI_DEVICE *device) {
    u64 abar;
    if (!pci_is_ahci(device)) return NtzDriverAbsent;
    abar = pci_mmio_bar(device, 5u);
    if (abar == 0u) return NtzDriverUnsupported;
    return NtzDriverNeedsDma;
}

NTZ_DRIVER_STATE NvmeProbe(const NTZ_PCI_DEVICE *device) {
    u64 bar0;
    if (!pci_is_nvme(device)) return NtzDriverAbsent;
    bar0 = pci_mmio_bar(device, 0u);
    if (bar0 == 0u) return NtzDriverUnsupported;
    return NtzDriverNeedsDma;
}
