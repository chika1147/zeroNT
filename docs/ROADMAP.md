# zeroNT Roadmap

## Where the project stands (2026-10-31)

Two distinct architectures now coexist **in the same repository**, not as a unified build:

| Track | Architecture | Validation |
|---|---|---|
| Public Alpha 0.1 | UEFI PE32+ loader -> PE32+ NT Native `ntoskrnl.exe` | GitHub Actions compile / host-test PASS |
| v33.0 Beta Stage 1–4 | UEFI PE32+ loader -> ELF64 `KERNEL.ELF` | Source package Clang 17 build + FAT32/installer host tests PASS; runtime unverified |

## Stage A: source publication — completed

1. **Done:** import all 131 reviewed v33 source/config files into `v33-beta/` in a separate commit.
2. Preserve existing public Alpha C files, build scripts and `VERSION` until the default build architecture is intentionally migrated.
3. Exclude generated ISO/IMG, build outputs, private files and credentials.
4. **Done (compile/link):** independent v33 C++/ELF64 CI builds loader and kernel; Alpha CI remains separate.

## Stage B: reproducible boot

1. Verify `BOOTX64.EFI` loading the ELF64 kernel under QEMU/OVMF and save the serial log.
2. Inspect ExitBootServices handoff and halt reasons, especially previous 75% / 85% boot hangs.
3. Verify mouse, keyboard, framebuffer and desktop only after a consistent kernel entry.
4. Attach exact QEMU/OVMF versions and reproduction steps to tests.

## Stage C: storage and installer

1. Re-test FAT32 LFN and recursive file operations against disposable images.
2. Exercise GPT/ESP, filesystem formatting, file copy and installer recovery on *throwaway QEMU disks*.
3. Boot from the installed virtual HDD after detaching the install image.
4. Verify persistent firmware boot entries or document a fallback UEFI path.

## Stage D: release readiness

- Test NVMe/AHCI, xHCI/USB HID and input on QEMU, VMware, then real hardware only when safe.
- Add logs for all startup failures and crash recovery.
- Keep Beta designation until reliable end-to-end boot and installation succeed.
- Do not publish a "stable", "Windows compatible", or production-safe claim based only on source compilation.

## Earlier public Alpha roadmap

The PE32+ Alpha line still needs real device IRQ activation, complete DMA/I/O queues, user-mode scheduling and persistent filesystem integration. See `docs/BUILD.md` and the [Alpha validation record](VALIDATION-ALPHA-0.1.md).
