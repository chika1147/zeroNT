# NTZero

> A from-scratch NT-inspired x86_64 operating system project.

![status](https://img.shields.io/badge/status-Alpha%200.1-orange)
![arch](https://img.shields.io/badge/arch-x86__64-blue)
![boot](https://img.shields.io/badge/boot-UEFI-blue)
![target](https://img.shields.io/badge/target-QEMU-purple)

## NTZero Alpha 0.1

**NTZero Alpha 0.1** is the first public developer preview of NTZero.

The public Alpha tree contains a buildable x86-64 UEFI loader, a PE32+ NT Native kernel entry path, COM1 serial diagnostics, an in-kernel ALPC transport core and the first native hardware-driver framework.

> [!WARNING]
> Alpha 0.1 is development software. QEMU is the primary runtime target. Do not use it for important data or production systems.

### Public source vs. later development

The public `main` branch and `VERSION` file describe **0.1.0-alpha**, not zeroNT v33.0 Beta. Later v33.0 Beta work discussed outside this repository (GUI, FAT32, installer, and recovery) is **not verified or published as part of this source tree**. This repository must not be described as a working v33.0 Beta release without a separate source comparison and boot/runtime validation.

## What is in the public Alpha source

- x86-64 UEFI `BOOTX64.EFI` loader source
- manual PE32+ kernel mapping
- final UEFI memory-map handoff and `ExitBootServices()`
- `ntoskrnl.exe` linked as NT Native, preferred ImageBase `0x10000000`
- `KiSystemStartup`
- COM1 serial diagnostics
- ALPC connection ports, client/server ports and message queues
- native hardware-driver framework
- PCI configuration-space enumeration
- PS/2 keyboard/mouse controller APIs
- PIT timer and RTC CMOS access
- Local APIC and I/O APIC MMIO APIs
- ACPI RSDP/XSDT parser
- IDT/exception-vector foundation (device interrupts remain masked at startup)
- physical-page manager and contiguous DMA allocator (identity-mapping baseline)
- PCI MSI capability discovery/configuration foundation
- AHCI and NVMe controller detection
- polled NVMe admin queue with Controller/Namespace Identify path (not NVMe block read/write)
- USB HID report decoding and USB Mass Storage BOT/SCSI protocol helpers
- xHCI controller detection
- VirtIO PCI device detection
- Intel e1000 / RTL8139 network-controller detection
- linear framebuffer pixel/clear API
- GitHub Actions compile/link validation

## Driver status

The tree distinguishes **compiled foundations**, **host-tested protocols**, **controller detection**, and **runtime-complete drivers**. These are not interchangeable.

| Area | Current public Alpha source state |
|---|---|
| PCI | configuration read/write and bus/device/function enumeration |
| IDT / interrupts | exception and vector-table foundation in `kernel/interrupts.c`; device interrupts remain masked at startup |
| PMM / DMA | physical-page allocator and contiguous DMA allocator in `kernel/memory.c`; identity-mapped baseline and host tests, not a complete virtual-memory/DMA subsystem |
| PCI MSI | capability discovery and MSI configuration functions in `drivers/pci_msi.c`; end-to-end interrupt delivery not validated |
| PS/2 | keyboard/mouse controller polling APIs; interrupt-driven input not validated |
| PIT / RTC | low-level timer/CMOS access |
| LAPIC / I/O APIC | MMIO/routing APIs; full IRQ activation not validated |
| ACPI | RSDP/XSDT parser and table lookup; boot RSDP handoff remains to be wired |
| AHCI | PCI class/BAR detection; DMA command engine pending |
| NVMe | PCI detection and polled admin queue for Controller/Namespace Identify; **I/O queue and real block read/write pending** |
| USB HID / USB Mass Storage | report decoding and BOT/SCSI protocol helpers host-tested; xHCI device transfers not complete |
| xHCI | PCI detection; command/event/transfer rings pending |
| VirtIO | device-class identification; virtqueues pending |
| e1000 / RTL8139 | identification; TX/RX descriptor rings pending |
| Framebuffer | put-pixel/clear API; UEFI GOP framebuffer handoff pending |

At kernel startup, `KiSystemStartup()` initializes the IDT and PMM/DMA foundations, then `DriversInitializeAll()` enumerates PCI devices and attempts polled NVMe Identify when a matching controller is found. This does **not** imply working storage I/O, interrupt-driven device engines, or a GUI desktop.

See [drivers/README.md](drivers/README.md) for the detailed driver matrix.

## ALPC status

The scheduler-backed blocking wait/wake path for ALPC is the next IPC integration step. When no message is queued, the current public Alpha transport returns `STATUS_PENDING` rather than sleeping the caller.

## Build on Windows

Requirements:

- LLVM/Clang
- LLD
- QEMU x86-64
- EDK2/OVMF x86-64 firmware

Build:

```powershell
.\scripts\build.ps1
```

Run — no Stage or CPU-count argument is required:

```powershell
.\scripts\run-qemu.ps1
```

The run script defaults to QEMU `q35`, 2 GiB RAM and 4 virtual CPUs.

## Expected serial start

```text
NTZero Alpha 0.1
KiSystemStartup: x86_64 native kernel entered
BOOT: UEFI ExitBootServices complete
IDT: exceptions + IRQ/MSI vector table installed (interrupts masked)
PMM: free pages = ...
DMA: contiguous physical allocator ready (identity-map baseline)
DRIVER: NTZero driver framework start
DRIVER: PCI devices = ...
DRIVER: storage controllers = ...
DRIVER: xHCI controllers = ...
DRIVER: network controllers = ...
DRIVER: VirtIO devices = ...
DRIVER: display devices = ...
ALPC: kernel transport initialized
NTZero Alpha 0.1: READY
```

## Repository layout

```text
zeroNT/
├─ boot/uefi/          UEFI boot loader
├─ kernel/             native kernel and ALPC core
├─ drivers/            PCI/platform/input/storage/USB/net/video driver code
├─ include/ntzero/     public/internal kernel headers
├─ tests/              host-side subsystem tests
├─ scripts/            build and QEMU launch scripts
├─ docs/               build, validation and history
├─ build/              generated outputs (ignored)
├─ VERSION
└─ README.md
```

## Validation

GitHub Actions compiles/links the UEFI loader and NT Native kernel with Clang/LLD and warnings-as-errors (`-Wall -Wextra -Werror`). The workflow also builds and executes host-side tests for ALPC, USB HID/USB Mass Storage protocol helpers, and PMM/DMA. [The workflow for commit `339b30b`](https://github.com/chika1147/zeroNT/actions/runs/33864792682) concluded successfully on 2026-09-04.

**The workflow does not boot QEMU.** Passing compilation and host tests is not proof of successful `ExitBootServices()` on the current build, NVMe transfers, device IRQs, GUI functionality, installation, or v33.0 Beta runtime behavior.

See [docs/BUILD.md](docs/BUILD.md) and [docs/VALIDATION-ALPHA-0.1.md](docs/VALIDATION-ALPHA-0.1.md).

## Development direction

Current IDT, physical-page allocator, identity-mapped DMA allocation, and PCI MSI functions are foundations, not completed end-to-end subsystems. Next work includes validating QEMU boot and memory handoff, interrupt routing and handlers, virtual memory/DMA mappings, NVMe I/O queues and block reads, AHCI/xHCI/VirtIO/network transfer engines, scheduler-backed waits, Ring 3, and native syscalls. The separate v33.0 Beta development line needs its own source and runtime verification before public versioning changes.

Historical v4.0-v5.0 development notes are preserved in [docs/HISTORY-v4-v5.md](docs/HISTORY-v4-v5.md). They are historical milestones and should not be read as a claim that every earlier subsystem is already restored in the public Alpha 0.1 tree.

## License

MIT License. See [LICENSE](LICENSE).
