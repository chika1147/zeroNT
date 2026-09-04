# Changelog

## [0.1.0-alpha] - 2026-09-04

### Added

- First public NTZero developer-preview repository baseline
- Buildable x86-64 UEFI `BOOTX64.EFI` loader source
- Manual PE32+ mapping for the embedded NT Native kernel
- `ExitBootServices()` memory-map handoff to `KiSystemStartup`
- PE32+ x86-64 `ntoskrnl.exe` build at ImageBase `0x10000000`
- COM1 serial diagnostics
- Native kernel ALPC transport core
- Named ALPC connection ports and client/server communication ports
- ALPC connect, accept, message queue, message IDs and disconnect path
- Host-side ALPC transport test
- No-argument Windows build and QEMU scripts
- Build/validation documentation and historical v4-v5 notes

### Current boundary

The public Alpha 0.1 ALPC transport does not yet wire `STATUS_PENDING` into scheduler-backed blocking wait/wake. QEMU runtime boot still requires validation on a host with QEMU/OVMF installed.
