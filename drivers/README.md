# NTZero Drivers — Alpha 0.1

The public driver tree is a real kernel-side hardware baseline rather than empty placeholders.

## Compiled into `ntoskrnl.exe`

| Driver area | File | Alpha 0.1 status |
|---|---|---|
| PCI configuration + enumeration | `pci.c` | implemented |
| PS/2 keyboard/mouse controller APIs | `legacy.c` | implemented, IRQ activation deferred |
| PIT timer + RTC CMOS reads | `legacy.c` | implemented, activation deferred |
| Local APIC / I/O APIC MMIO APIs | `apic.c` | implemented, routing deferred |
| ACPI RSDP/XSDT table parser | `acpi.c` | implemented; UEFI RSDP handoff not wired yet |
| AHCI | `storage.c` | PCI/BAR detection; DMA/FIS/command-list engine next |
| NVMe | `storage.c` | PCI/BAR detection; admin + I/O queue engine next |
| xHCI | `usb.c` | PCI/BAR detection; command/event/transfer rings next |
| USB HID keyboard/mouse protocol | `hid.c` | boot-report decode implemented and host-tested |
| USB Mass Storage BOT/SCSI | `usb_mass.c` | CBW/CSW, INQUIRY, READ CAPACITY, READ(10), WRITE(10) builders implemented and host-tested |
| VirtIO PCI | `virtio.c` | net/block/console/GPU identification; virtqueues next |
| Intel e1000 / RTL8139 | `net.c` | controller detection; descriptor/DMA rings next |
| Wi-Fi controller families | `audio_wireless.c` | Intel/Realtek/Atheros/Broadcom/Qualcomm PCI detection; firmware/802.11 stack next |
| HD Audio | `audio_wireless.c` | PCI/MMIO detection; CORB/RIRB + stream DMA next |
| Linear framebuffer | `video.c` | put-pixel/clear implemented; UEFI GOP handoff next |

## Safe early-boot behavior

`DriversInitializeAll()` performs PCI discovery and reports detected controller classes over COM1 serial. It does **not** start DMA engines or enable device interrupts before the kernel has a finished physical-memory allocator, DMA allocator, IDT and IRQ-routing layer.

That separation is intentional. It keeps Alpha 0.1 bootable while still compiling real driver/protocol code into `ntoskrnl.exe`.

## Runtime activation dependencies

```text
IDT / exception handlers
        +
APIC / MSI / MSI-X routing
        +
physical + virtual memory manager
        +
DMA allocator / mapping
        ↓
AHCI command engine
NVMe admin + I/O queues
xHCI command/event/transfer rings
VirtIO virtqueues
NIC TX/RX rings
HDA audio streams
        ↓
USB HID / USB Mass Storage transports
Networking / Wi-Fi
Audio
```

## CI validation

GitHub Actions builds the complete kernel with warnings-as-errors and runs host-side protocol tests for:

- ALPC create/connect/accept/message transport
- USB HID keyboard/mouse report decoding
- USB Mass Storage BOT/SCSI packet construction and CSW validation

Hardware runtime support is only marked complete after QEMU or real-machine validation.
