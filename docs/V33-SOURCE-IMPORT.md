# Publish all reviewed zeroNT v33.0 Beta sources to GitHub

The full source-only ZIP has been prepared as `zeroNT_v33_0_BETA_GITHUB_SOURCE_ONLY.zip` (SHA-256: `d6ea406d948bb42517fba77637ac382edad8a1bb23b122a990712f3d63b286d0`). It contains **131 text/source/config entries** under `v33-beta/`; no ISO, disk image, UEFI binary or local object file.

## Upload using Git on Windows 11

Download the source-only ZIP and the prepared publish package, unzip locally, then run `PUBLISH_TO_GITHUB.ps1` with PowerShell:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\PUBLISH_TO_GITHUB.ps1 -Publish
```

Prerequisites: Git for Windows installed, and GitHub authentication configured (Git Credential Manager or GitHub CLI). The script:

1. Clones or updates `https://github.com/chika1147/zeroNT.git`.
2. Checks out a new `import/v33-beta-source-20261031` branch from `main`.
3. Copies the staged **text/source/config** files into `v33-beta/`, checks that no `.img`, `.iso`, `.exe`, `.efi`, or other generated binaries are staged, and commits them.
4. Pushes the branch to GitHub and offers a PR creation link. It never rewrites `main`, overwrites Alpha sources or formats disks.

The upload cannot be completed by editing the GitHub README alone. The branch becomes part of `main` only after the import PR is reviewed and merged.

## Validation after import

- Keep Alpha GitHub CI green.
- Build v33's EFI loader and ELF64 kernel independently with Clang/LLD.
- Rootfs boot-media packaging requires a separate 64 MiB seed image that is **not in the source-only GitHub package**.
- Record QEMU/OVMF serial and GUI results before any Beta Release/tag.

See the [audit](V33-STAGE1234-SOURCE-AUDIT-20261031.md) and [roadmap](ROADMAP.md). Do not claim runtime support from host build tests alone.
