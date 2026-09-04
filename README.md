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
- AHCI and NVMe controller detection
- xHCI controller detection
- VirtIO PCI device detection
- Intel e1000 / RTL8139 network-controller detection
- linear framebuffer pixel/clear API
- GitHub Actions compile/link validation

## Driver status

The driver tree is intentionally split between **implemented**, **detected**, and **waiting for kernel infrastructure**.

| Area | Current Alpha 0.1 state |
|---|---|
| PCI | configuration read/write + full bus/device/function enumeration |
| PS/2 | controller init + keyboard/mouse polling APIs; IRQ path waits for IDT routing |
| PIT / RTC | low-level timer/CMOS access implemented |
| LAPIC / I/O APIC | MMIO enable/EOI/routing APIs implemented |
| ACPI | RSDP/XSDT checksum parser and table lookup implemented; boot RSDP handoff is next |
| AHCI | PCI class/BAR detection; DMA command-list/FIS engine is next |
| NVMe | PCI class/BAR detection; admin/I/O queue engine is next |
| xHCI | PCI class/BAR detection; command/event/transfer rings are next |
| VirtIO | block/net/console/GPU PCI identification; virtqueues are next |
| e1000 / RTL8139 | controller identification; descriptor/DMA rings are next |
| Framebuffer | native put-pixel/clear API; UEFI GOP handoff is next |

`DriversInitializeAll()` currently performs safe PCI discovery at kernel startup and prints controller counts to COM1. Hardware requiring a finished IDT, interrupt routing, physical-memory allocator or DMA allocator is compiled but deliberately not fully activated yet.

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

The public Alpha source is continuously compile/link validated by GitHub Actions with Clang/LLD and `-Wall -Wextra -Werror`. The ALPC host transport test also runs in CI.

QEMU runtime boot still needs validation on an environment with QEMU/OVMF, so compile success is not presented as proof that every hardware driver is runtime-complete.

See [docs/BUILD.md](docs/BUILD.md) and [docs/VALIDATION-ALPHA-0.1.md](docs/VALIDATION-ALPHA-0.1.md).

## Development direction

Next kernel work is the infrastructure needed to turn the detected controllers into complete runtime drivers: IDT/IRQ delivery, APIC routing, physical/virtual memory management, DMA allocation, scheduler waits, Ring 3 and native syscalls. After that the AHCI/NVMe/xHCI/VirtIO/network queue engines can be fully activated.

Historical v4.0-v5.0 development notes are preserved in [docs/HISTORY-v4-v5.md](docs/HISTORY-v4-v5.md). They are historical milestones and should not be read as a claim that every earlier subsystem is already restored in the public Alpha 0.1 tree.

## License

MIT License. See [LICENSE](LICENSE).
