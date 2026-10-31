# v33.0 Beta Stage 1–4 source verification (2026-10-31)

## Provenance

Received user-provided `zeroNT_v33_0_BETA_STAGE1234_FIX_ALL.zip`, which contains a nested `zeroNT_v33_0_BETA_STAGE1234_FIX_FULL_SOURCE.zip` in addition to prebuilt disk and ISO images.

- Outer ALL.zip SHA-256: `868df921ee285fc59f6870c6d271974b3cc61d6d00184095ef0a5a6e19c36d77`
- Nested FULL_SOURCE.zip SHA-256: `8c94d2704f3394a4097081475a91bc6b922c7ef352109e2255de0ad9466b438e`
- Nested archive contains 254 ZIP entries, including generated binaries/build logs. Source-level import must exclude those files and storage images.

These checksums identify the specific uploaded input. **The full v33 source tree has not been committed to this GitHub branch.** The latest public `main` is still Alpha `0.1.0-alpha`, with a different PE32+ kernel/loader architecture.

## Fresh build and host checks

Source was unpacked into an isolated build workspace; the uploaded ZIP was left unchanged. Tests were executed with Clang 17 and GNU Make on Linux:

| Check | Result |
|---|---|
| `make -j2 all` (UEFI PE32+ loader, ELF64 PIE kernel, SYSTEM32 user applications, 96 MiB FAT32 boot media) | PASS |
| Native `fat32_lfn_test` | PASS |
| Native `fat32_tree_test` (recursive copy/move/delete) | PASS |
| Native `installer_test` (GPT/FAT32/system install mock, repair, upgrade preservation) | PASS |
| Native `rootfs_verify` (5 ZEX apps + 13 SYSTEM32 EXEs) | PASS |

The tests above used a working copy of the 64 MiB rootfs image. **No QEMU/VMware boot was executed here, and no real disk was formatted.** Persistent UEFI NVRAM, USB, NVMe device I/O, GUI runtime, and complete installed-disk reboot are unverified.

## Source issues found and portable package corrections

- Source package's legacy `verify-beta.ps1` still checks `disk/System/kernel.bin` even though the v33 build generates `build/kernel.elf` and `disk/System/KERNEL.ELF`. Corrected in a separate sanitized source package prepared for local testing; not yet ported into public Alpha.
- The uploaded `ALL.zip` contains the full source **as a nested ZIP**. `scripts/inventory-v33-source.py` now recognizes that layout and reports the nested archive checksum.
- A source-only package and local staging utility were prepared that keep generated images, `build/`, logs and backup copies out of Git. The full source requires a separate migration review; do not overwrite the public Alpha `boot/uefi/boot.c` with the v33 ELF64 loader.

## Recommended next gate

1. Import audited v33 C++/header/build scripts under a separate `v33-beta/` subtree on a dedicated branch (no generated disk images).
2. Provide the 64 MiB FAT32 rootfs separately for local rebuild, not as a Git blob.
3. Make Windows Clang and Linux Clang builds reproducible, preserving public Alpha CI.
4. Capture QEMU serial output and graphical runtime, and verify installer + reboot using a disposable virtual disk.
5. Only after these tests pass should v33 be considered for a release branch or version bump.

Until then, the latest published Alpha build and v33 Beta development tree are separate.
