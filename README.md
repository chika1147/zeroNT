# NTZero

> A from-scratch NT-inspired x86_64 operating system project.

![status](https://img.shields.io/badge/status-Alpha%200.1-orange)
![arch](https://img.shields.io/badge/arch-x86__64-blue)
![boot](https://img.shields.io/badge/boot-UEFI-blue)
![target](https://img.shields.io/badge/target-QEMU-purple)

## NTZero Alpha 0.1

**NTZero Alpha 0.1** is the first public developer preview of NTZero.

NTZero is an experimental operating-system project built to explore a native NT-style kernel architecture from scratch.

> [!WARNING]
> Alpha 0.1 is development software. QEMU is the primary target.
> Do not use it for important data or production systems.

## Current target

- Architecture: x86_64
- Firmware: UEFI
- Primary emulator: QEMU
- Kernel design: native / from scratch
- Development hosts: Windows, macOS, Linux

## Development direction

NTZero is being developed toward:

- UEFI boot
- x86_64 long mode
- Physical and virtual memory management
- Exceptions and interrupts
- APIC / SMP
- Preemptive scheduling
- Ring 3 user mode
- System calls
- Native process and thread model
- LPC / ALPC-style IPC
- VFS and storage
- USB HID
- Networking
- Native GUI / desktop

Not every item above is complete in Alpha 0.1. This list describes the project direction.

## Repository layout

```text
zeroNT/
├─ boot/
├─ kernel/
├─ drivers/
├─ include/
├─ docs/
├─ scripts/
├─ build/
├─ VERSION
├─ CHANGELOG.md
└─ README.md
```

## Running the QEMU image

Put the built disk image at:

```text
build/ntzero.img
```

### Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1
```

### macOS / Linux

```bash
chmod +x scripts/run-qemu.sh
./scripts/run-qemu.sh
```

## Version

```text
NTZero Alpha 0.1
0.1.0-alpha
```

## Release status

Alpha releases are intended for developers and testers. Kernel interfaces, ABI, file formats, drivers and boot behavior may change without compatibility guarantees.

## Contributing

Bug reports, documentation fixes, QEMU testing and hardware testing are welcome.

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT License. See [LICENSE](LICENSE).
