# NTZero Drivers — Alpha 0.1

The public driver tree contains low-level implementations and host-tested protocol helpers, but not all drivers perform end-to-end hardware I/O. This document describes the source at `main` commit `339b30b`; it is not a feature list for the separate v33.0 Beta development line.

## Compiled into `ntoskrnl.exe`

| Driver area | File | Alpha 0.1 status |
|---|---|---|
| PCI configuration + enumeration | `pci.c` | implemented |
| IDT / exception-vector foundation | `../kernel/interrupts.c` | initialized at startup, interrupts masked; device delivery not validated |
| PMM / contiguous DMA foundation | `../kernel/memory.c` | initialized at startup; identity-mapped baseline, host-tested |
| PCI MSI capability/configuration | `pci_msi.c` | functions implemented; IRQ delivery not validated |
| PS/2 keyboard/mouse controller APIs | `legacy.c` | implemented, IRQ activation deferred |
| PIT timer + RTC CMOS reads | `legacy.c` | implemented, activation deferred |
| Local APIC / I/O APIC MMIO APIs | `apic.c` | implemented, routing deferred |
| ACPI RSDP/XSDT table parser | `acpi.c` | implemented; UEFI RSDP handoff not wired yet |
| AHCI | `storage.c` | PCI/BAR detection; DMA/FIS/command-list engine next |
| NVMe | `storage.c`, `nvme.c` | PCI detection + polled admin queue with Controller/Namespace Identify; I/O queue and block reads/writes not implemented |
| xHCI | `usb.c` | PCI/BAR detection; command/event/transfer rings next |
| USB HID keyboard/mouse protocol | `hid.c` | boot-report decode implemented and host-tested |
| USB Mass Storage BOT/SCSI | `usb_mass.c` | CBW/CSW, INQUIRY, READ CAPACITY, READ(10), WRITE(10) builders implemented and host-tested |
| VirtIO PCI | `virtio.c` | net/block/console/GPU identification; virtqueues next |
| Intel e1000 / RTL8139 | `net.c` | controller detection; descriptor/DMA rings next |
| Wi-Fi controller families | `audio_wireless.c` | Intel/Realtek/Atheros/Broadcom/Qualcomm PCI detection; firmware/802.11 stack next |
| HD Audio | `audio_wireless.c` | PCI/MMIO detection; CORB/RIRB + stream DMA next |
| Linear framebuffer | `video.c` | put-pixel/clear implemented; UEFI GOP handoff next |

## Safe early-boot behavior

`KiSystemStartup()` installs the IDT, initializes the physical-memory manager and the contiguous DMA allocator, and calls `DriversInitializeAll()` for PCI discovery. A detected NVMe controller can be initialized through a **polled admin queue** to issue Identify commands. The kernel keeps interrupts masked at startup; AHCI/xHCI/NIC/HDA data-transfer paths are not enabled by this initialization.

In `drivers/nvme.c`, `NvmeReadBlocks()` prepares a read request but intentionally returns `STATUS_PENDING` instead of submitting a READ command: NVMe I/O queue creation is not implemented. Do not interpret successful Identify or host test results as working disk reads.

## Runtime activation dependencies

```text
In tree (foundations; runtime not fully validated):
  IDT + exception vectors
  physical-page allocator + identity-mapped DMA
  PCI MSI functions, APIC MMIO helpers
  NVMe polled admin Identify
             |
             v
Still needed for full runtime:
  APIC/IRQ delivery + correct device handlers
  virtual memory and robust DMA mapping
  NVMe I/O queues + actual block reads/writes
  AHCI command/FIS, xHCI command/event/transfer rings
  VirtIO virtqueues, NIC TX/RX, HDA audio streams
             |
             v
  End-to-end USB storage/input, networking, audio, GUI
```

## CI validation

GitHub Actions builds the complete kernel with warnings-as-errors and runs host-side protocol tests for:

- ALPC create/connect/accept/message transport
- USB HID keyboard/mouse report decoding
- USB Mass Storage BOT/SCSI packet construction and CSW validation
- physical-page manager and DMA allocator host tests

[The latest CI workflow for `339b30b`](https://github.com/chika1147/zeroNT/actions/runs/33864792682) passed compilation and these host tests. It does not run QEMU; device I/O and complete runtime behavior still require QEMU or real-hardware validation.
