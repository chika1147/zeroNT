#include "ntzero/drivers.h"
#include "ntzero/serial.h"

static void clear_summary(NTZ_DRIVER_SUMMARY *summary) {
    u8 *p = (u8 *)summary;
    usize i;
    for (i = 0u; i < sizeof(*summary); ++i) p[i] = 0u;
}

static void driver_pci_visitor(const NTZ_PCI_DEVICE *device, void *context) {
    NTZ_DRIVER_SUMMARY *summary = (NTZ_DRIVER_SUMMARY *)context;
    NTZ_DRIVER_STATE state;

    ++summary->pci_devices;

    state = AhciProbe(device);
    if (state != NtzDriverAbsent) ++summary->storage_controllers;
    state = NvmeProbe(device);
    if (state != NtzDriverAbsent) ++summary->storage_controllers;

    state = XhciProbe(device);
    if (state != NtzDriverAbsent) ++summary->usb_controllers;

    state = NetworkProbe(device);
    if (state != NtzDriverAbsent) ++summary->network_controllers;

    state = WirelessProbe(device);
    if (state != NtzDriverAbsent) ++summary->wireless_controllers;

    state = AudioProbe(device);
    if (state != NtzDriverAbsent) ++summary->audio_controllers;

    state = VirtioProbe(device);
    if (state != NtzDriverAbsent) ++summary->virtio_devices;

    if (device->class_code == 0x03u) ++summary->display_devices;
}

static void print_count(const char *name, u32 value) {
    SerialWrite("DRIVER: ");
    SerialWrite(name);
    SerialWrite(" = ");
    SerialWriteDec(value);
    SerialWrite("\n");
}

void DriversInitializeAll(const NTZ_BOOT_INFO *boot, NTZ_DRIVER_SUMMARY *summary) {
    (void)boot;
    if (summary == (NTZ_DRIVER_SUMMARY *)0) return;

    clear_summary(summary);
    SerialWrite("DRIVER: NTZero driver framework start\n");
    (void)PciEnumerate(driver_pci_visitor, summary);

    print_count("PCI devices", summary->pci_devices);
    print_count("storage controllers", summary->storage_controllers);
    print_count("xHCI controllers", summary->usb_controllers);
    print_count("network controllers", summary->network_controllers);
    print_count("wireless controllers", summary->wireless_controllers);
    print_count("audio controllers", summary->audio_controllers);
    print_count("VirtIO devices", summary->virtio_devices);
    print_count("display devices", summary->display_devices);
    SerialWrite("DRIVER: PS/2, PIT/RTC, APIC, HID and USB Mass Storage protocol APIs compiled\n");
    SerialWrite("DRIVER: DMA/IRQ-dependent engines remain gated until memory + IDT infrastructure is active\n");
}
