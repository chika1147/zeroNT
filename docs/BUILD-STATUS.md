# Build and validation status (2026-10-31)

## Public root: `0.1.0-alpha`

- Current GitHub `main` root source: NT Native PE32+ kernel.
- GitHub Actions: Clang/LLD compilation, ALPC host test, HID/USB Mass Storage host test, PMM/DMA host test, and Python boot-log/inventory host tests: **PASS**.
- Latest merged test gate: PR #2.
- QEMU boot in CI: **NOT EXECUTED**.
- End-to-end storage, GUI and installation: **NOT VERIFIED**.

## Separately received v33.0 Beta Stage 1–4 source package

- Full source supplied in a nested ZIP; now published as [the `v33-beta/` source subtree](../v33-beta/) (without generated images).
- Fresh Linux/Clang 17 `make -j2 all`: **PASS**, producing EFI application, ELF64 PIE kernel and 96 MiB FAT32 boot media.
- Host FAT32 LFN: **PASS**.
- Host recursive FAT32 tree copy/move/delete: **PASS**.
- Installer logic simulated on a disposable image: **PASS**.
- Rootfs verification of 5 ZEX and 13 SYSTEM32 EXE entries: **PASS**.
- QEMU or VMware *runtime* boot and GUI: **NOT VERIFIED**.
- Installed HDD-only reboot, persistent UEFI boot entry, xHCI/NVMe hardware transfer: **NOT VERIFIED**.
- Complete source-only v33 snapshot on GitHub: **131 files under `v33-beta/` — IMPORTED**.
- Independent GitHub Actions v33 build: **PASS** for the EFI loader and ELF64 kernel; this does not test runtime boot.

Details and artifact hashes: [v33 audit](V33-STAGE1234-SOURCE-AUDIT-20261031.md).

**Never use build PASS as shorthand for runtime PASS.** Root `VERSION` remains `0.1.0-alpha` until the root build is intentionally replaced and verified.
