# NTZero Drivers — Alpha 0.1

The public driver tree is organized as a real kernel-side hardware baseline rather than empty placeholders.

## Compiled into `ntoskrnl.exe`

| Driver area | File | Alpha 0.1 status |
|---|---|---|
| PCI configuration + enumeration | `pci.c` | implemented |
| PS/2 keyboard/mouse controller APIs | `legacy.c` | implemented, IRQ activation deferred |
| PIT timer + RTC CMOS reads | `legacy.c` | implemented, activation deferred |
| Local APIC / I/O APIC MMIO APIs | `apic.c` | implemented, routing deferred |
| ACPI RSDP/XSDT table parser | `acpi.c` | implemented; UEFI RSDP handoff not wired yet |
| AHCI detection | `storage.c` | PCI/BAR probe; DMA/AHCI command engine next |
| NVMe detection | `storage.c` | PCI/BAR probe; admin/I/O queues next |
| xHCI detection | `usb.c` | PCI/BAR probe; rings/enumeration next |
| VirtIO PCI detection | `virtio.c` | net/block/console/GPU identification; queues next |
| Intel e1000 / RTL8139 detection | `net.c` | controller detection; DMA rings next |
| Linear framebuffer API | `video.c` | pixel/clear implementation; GOP handoff next |

## Important

`DriversInitializeAll()` currently performs safe PCI enumeration and reports detected controller classes over COM1 serial. Drivers that require a working IDT, IRQ routing, DMA allocator or physical-memory manager are compiled but deliberately not fully activated during early boot yet.

That boundary prevents Alpha 0.1 from claiming hardware support that has not been runtime validated.
