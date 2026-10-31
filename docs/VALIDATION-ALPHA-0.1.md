# NTZero Alpha 0.1 — validation record

This document covers the public `0.1.0-alpha` source tree; it does **not** verify zeroNT v33.0 Beta.

## Historical artifact build

An earlier artifact environment used Clang/LLD 17 with `-Wall -Wextra -Werror` and reported:

```text
ntoskrnl.exe  : PE32+ x86-64, Native subsystem, ImageBase 0x10000000
BOOTX64.EFI   : PE32+ x86-64, EFI application
ALPC host test: PASS (create/connect/accept/heartbeat send/receive)
```

QEMU was unavailable in that artifact environment, so no runtime boot result was recorded there.

## GitHub Actions at latest public commit

- Commit: [`339b30b06cba42d717f502c78ddaefa106179d2b`](https://github.com/chika1147/zeroNT/commit/339b30b06cba42d717f502c78ddaefa106179d2b)
- Workflow: [NTZero Alpha CI run 33864792682](https://github.com/chika1147/zeroNT/actions/runs/33864792682)
- Run date: **2026-09-04** (UTC)
- Conclusion reported by GitHub: **success**
- Verified CI steps: UEFI and kernel build, ALPC host build/test, HID + USB Mass Storage host build/test, PMM + DMA host build/test, expected artifact checks and artifact upload.
- The CI workflow compiles with warnings-as-errors; its scripts do **not** invoke QEMU.

## Not validated by this CI result

- End-to-end UEFI to `KiSystemStartup` boot under QEMU/OVMF on the latest commit
- Full NVMe block I/O (the source's `NvmeReadBlocks()` deliberately returns `STATUS_PENDING`)
- xHCI transfers, device IRQ delivery, AHCI/VirtIO/NIC/HDA DMA engines
- Desktop GUI, FAT32 filesystem, GPT installer, persistent boot, and v33.0 Beta integration

To reproduce a QEMU boot test on Windows with QEMU and OVMF installed, first run `./scripts/build.ps1`, then `./scripts/run-qemu.ps1` from PowerShell. Save the full serial output and list the QEMU/OVMF versions before claiming a boot pass.

A successful CI run is a **compile/host-test pass**, not a hardware runtime acceptance test.
