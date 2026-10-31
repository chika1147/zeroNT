# Changelog

## [Unreleased / documentation refresh] - 2026-10-31

### Updated

- Reorganized the repository landing page to distinguish the public `0.1.0-alpha` root kernel from the separate v33.0 Beta Stage 1–4 C++/ELF64 development source package.
- Documented the uploaded v33 source package's SHA-256 and independent Clang/LLD host build results.
- Added a source-only import guide. The full v33 source has **not** been committed to the public repository by this documentation change.
- Clarified that passing host compilation/FAT32/installer tests does not establish QEMU/VMware boot or installed-disk reboot.

### Earlier public source updates

- IDT exception vectors, PMM/DMA allocator foundations, and PCI MSI capabilities.
- Polled NVMe admin queue and Identify Controller/Namespace path (not complete block I/O).
- USB HID / Mass Storage protocol helpers and host tests.
- GitHub Actions host tests, QEMU serial smoke runner, and v33 archive inventory.

## [0.1.0-alpha] - 2026-09-04

### Added

- First public NTZero developer-preview repository baseline.
- Buildable x86-64 UEFI `BOOTX64.EFI` loader and PE32+ NT Native `ntoskrnl.exe`.
- `ExitBootServices()` memory-map handoff to `KiSystemStartup`.
- COM1 serial diagnostics, native ALPC core, host ALPC test.
- Windows build / QEMU scripts and initial validation documents.

### Current boundary

The public Alpha ALPC transport does not yet wire scheduler-backed waits, and its CI does not perform an end-to-end QEMU boot. v33.0 Beta is a separate source line, not a replacement for this historical release entry.
