# zeroNT v33.0 Beta → public Alpha: Stage 1 integration gate

## Purpose and boundaries

The public GitHub \`main\` still represents \`0.1.0-alpha\`, with a PE32+ NT Native \`ntoskrnl.exe\` booted by an x86-64 UEFI PE loader. Past v33.0 Beta development uses a different ELF64 kernel and installer/GUI/filesystem pipeline. **Do not rename this Alpha tree as v33 or copy binaries into it without comparing the actual sources.**

The v33.0 Beta source ZIP was **not available** when Stage 1 was prepared. No v33 files have been merged by this change. Zero64OS and ZeroDrivers are separate projects and must not be used as substitutes.

## Deliverables in this stage

1. \`scripts/test-qemu-boot.py\` — uses QEMU+OVMF on Windows/Linux, launches the existing Alpha ESP and a disposable local 64 MiB virtual NVMe test disk, captures a fresh serial log, and checks ordered kernel entry / \`ExitBootServices\` handoff / READY markers. It kills the QEMU subprocess after success, crash, or timeout.
2. \`scripts/test-qemu-boot.bat\` — Windows launch shortcut (Python 3 required).
3. \`scripts/inventory-v33-source.py\` — read-only directory/ZIP inventory, computes ZIP SHA-256 and lists integration candidate source paths, **does not extract, execute, or publish the archive**.
4. \`tests/test_qemu_boot_smoke.py\` — CI host-side tests of marker detection and source inventory behavior.

## Windows 11 commands

From the repository root in Command Prompt:

\`\`\`bat
scripts\build.ps1
scripts\test-qemu-boot.bat --timeout 120
\`\`\`

Use PowerShell to build instead of Command Prompt if the first line is not available there:

\`\`\`powershell
.\scripts\build.ps1
.\scripts\test-qemu-boot.bat --timeout 120
\`\`\`

If firmware or emulator paths are not discovered, specify:

\`\`\`powershell
.\scripts\test-qemu-boot.bat --qemu "C:\Program Files\qemu\qemu-system-x86_64.exe" --ovmf "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
\`\`\`

Expected generated serial log: \`build/qemu-boot-serial.log\`. A PASS proves only the three **kernel startup markers** were seen in order; it does **not** establish successful GUI use, file writes, device DMA, USB HID transfer, installation, or persistence. A FAIL/timeout requires inspection of the fresh log.

Inventory a real zeroNT v33.0 Beta source ZIP, when available:

\`\`\`powershell
python .\scripts\inventory-v33-source.py "C:\path\to\zeroNT_v33_0_BETA_STAGE1234_FIX_FULL_SOURCE.zip" --output .\build\v33-source-inventory.json
\`\`\`

This produces candidate file names and a checksum; review the archive contents and license/security issues before any source upload. Do not upload large ISO/IMG files or local build folders to GitHub automatically. The ZIP path above is an example **from previous development records**, not a claim that the file is on the current machine.

## Acceptance gates for the *next* actual source integration

- Obtain the real full v33 source tree, not screenshots, hashes alone, or a different OS project.
- Preserve the latest working Alpha baseline and successful host CI.
- Compare its UEFI/ELF64 loader ABI to Alpha's PE32+ entry; select one architecture for a dedicated migration branch.
- First make UEFI → kernel entry → serial READY reproducibly boot in QEMU; capture firmware/QEMU version and logs.
- Then integrate FAT32 LFN and installer components in separate patches, with non-destructive emulator-disk tests **before** testing any real storage.
- Move GUI/input later, after a reliable boot and filesystem, and only promote to a Beta tag after end-to-end tests pass.

## CI scope

The GitHub workflow runs the **host parser and inventory tests**, not QEMU itself. Real boot must be executed with \`scripts/test-qemu-boot.bat\` in an environment with QEMU/OVMF and the built Alpha ESP.
