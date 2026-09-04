# NTZero

> A from-scratch NT-inspired x86_64 operating system project.

![status](https://img.shields.io/badge/status-Alpha%200.1-orange)
![arch](https://img.shields.io/badge/arch-x86__64-blue)
![boot](https://img.shields.io/badge/boot-UEFI-blue)
![target](https://img.shields.io/badge/target-QEMU-purple)

## NTZero Alpha 0.1

**NTZero Alpha 0.1** is the first public developer preview of NTZero.

The public Alpha tree now contains a buildable x86-64 UEFI loader, a PE32+ NT Native kernel entry path, COM1 serial diagnostics and an in-kernel ALPC transport core.

> [!WARNING]
> Alpha 0.1 is development software. QEMU is the primary runtime target. Do not use it for important data or production systems.

## What is in the public Alpha source

- x86-64 UEFI `BOOTX64.EFI` loader source
- manual PE32+ kernel mapping
- final UEFI memory-map handoff and `ExitBootServices()`
- `ntoskrnl.exe` linked as NT Native, preferred ImageBase `0x10000000`
- `KiSystemStartup`
- COM1 serial diagnostics
- ALPC connection ports
- ALPC client/server communication ports
- ALPC connect/accept
- message IDs and local send/receive queue
- disconnect path
- host-side ALPC transport test

The scheduler-backed blocking wait/wake path for ALPC is the next kernel integration step. When no message is queued, the current public Alpha transport returns `STATUS_PENDING` rather than sleeping the caller.

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
ALPC: kernel transport initialized
NTZero Alpha 0.1: READY
```

## Repository layout

```text
zeroNT/
├─ boot/uefi/          UEFI boot loader
├─ kernel/             native kernel and ALPC core
├─ include/ntzero/     public/internal kernel headers
├─ tests/              host-side subsystem tests
├─ scripts/            build and QEMU launch scripts
├─ docs/               build, validation and history
├─ build/              generated outputs (ignored)
├─ VERSION
└─ README.md
```

## Validation

The public Alpha source has been compile/link validated with Clang/LLD 17 using `-Wall -Wextra -Werror`. The ALPC host transport test passed for named port creation, connect, accept and heartbeat send/receive.

QEMU runtime boot was **not** executed in the artifact build environment because QEMU was unavailable there, so compile success is not being presented as proof of a completed runtime boot test.

See [docs/BUILD.md](docs/BUILD.md) and [docs/VALIDATION-ALPHA-0.1.md](docs/VALIDATION-ALPHA-0.1.md).

## Development direction

NTZero is being developed toward memory management, exceptions/interrupts, APIC/SMP, preemptive scheduling, Ring 3, native processes/threads, system calls, fuller LPC/ALPC semantics, VFS/storage, USB HID, networking and a native GUI/desktop.

Historical v4.0-v5.0 development notes are preserved in [docs/HISTORY-v4-v5.md](docs/HISTORY-v4-v5.md). They are historical milestones and should not be read as a claim that every earlier subsystem is already restored in the public Alpha 0.1 tree.

## License

MIT License. See [LICENSE](LICENSE).
