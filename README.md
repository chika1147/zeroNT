# zeroNT

**自作 x86-64 オペレーティングシステム / From-scratch x86-64 operating system**

![v33 Beta](https://img.shields.io/badge/development-v33.0%20Beta-orange)
![Public main](https://img.shields.io/badge/public%20baseline-Alpha%200.1-blue)
![Arch](https://img.shields.io/badge/arch-x86__64-informational)
![Boot](https://img.shields.io/badge/boot-UEFI-informational)
![License](https://img.shields.io/badge/license-MIT-green)

> **重要 / Important:** zeroNT has **two distinct code lines**. The repository root still builds the public **0.1.0-alpha** PE32+ NT Native kernel. A separate **v33.0 Beta Stage 1–4** C++/ELF64 source package has been inspected and host-tested, but **its complete source files are not yet in `main`**. Do not confuse source compilation with a successful OS boot.

## 現在の開発状況 / Project status

| 項目 / Track | 内容 / Status | GitHub 状況 |
|---|---|---|
| **v33.0 Beta (active development)** | 独自UEFI/ELF64カーネル、クラシックGUI、FAT32/SafeFS、インストーラー、ドライバー群 | Source-only import is being prepared; **not a release** |
| **0.1.0-alpha (public baseline)** | UEFI PE32+ loader + NT Native kernel + IDT/PMM/DMA/PCI/NVMe foundations + ALPC | **Available and CI-tested under repository root** |
| **公開安定版 / Stable release** | End-to-end QEMU, persistent install/reboot and hardware validation | **Not available** |

The repository root `VERSION` remains `0.1.0-alpha` because the root build scripts still build the Alpha kernel. **v33.0 Beta is the development-track name, not the version of the root build**.

## v33.0 Beta — 開発中の機能

The uploaded v33.0 Beta Stage 1–4 source package contains code for:

- **UEFI + ELF64 kernel:** `BOOTX64.EFI` loader and `KERNEL.ELF` kernel, distinct from the Alpha `ntoskrnl.exe`.
- **Classic desktop:** windows, Explorer, taskbar, Start menu, keyboard/mouse input and display settings.
- **Storage and recovery:** FAT32, SafeFS journal/repair components, block-device APIs, and installer code.
- **Hardware foundations:** PS/2, PCI, NVMe, AHCI, xHCI, display, audio and networking.
- **Applications:** user-mode ZEX test apps and SYSTEM32 applications.

These are **source features**, not claims of end-to-end driver or hardware completion. The v33 package's current status is recorded in [v33 source audit](docs/V33-STAGE1234-SOURCE-AUDIT-20261031.md). The [integration plan](docs/V33-INTEGRATION-STAGE1.md) keeps its ELF64 architecture separate from Alpha's PE32+ architecture.

## 検証結果 / Validation

| 検証項目 | v33.0 Beta Stage 1–4 |
|---|---|
| UEFI EFI build and ELF64 kernel link (Clang/LLD 17) | **PASS (host build)** |
| FAT32 LFN regression | **PASS (host test)** |
| FAT32 recursive copy/move/delete | **PASS (host test)** |
| Installer GPT/FAT32 logic simulation | **PASS (host test)** |
| Rootfs: 5 ZEX + 13 SYSTEM32 EXEs | **PASS (host verification)** |
| QEMU/VMware boot, GUI input, real storage, installed-HDD reboot | **NOT VERIFIED** |

**No QEMU/VMware runtime pass is claimed for the v33 source package.** The public Alpha GitHub Actions pipeline checks compilation and host tests; it does not prove v33 runtime support. See [build status](docs/BUILD-STATUS.md).

## ソースコードの構成 / Repository layout

```text
zeroNT/
├── boot/                  Public Alpha UEFI loader
├── kernel/                Public Alpha NT Native kernel
├── drivers/               Public Alpha hardware foundations
├── include/               Public Alpha headers
├── scripts/               Alpha build, QEMU startup and v33 audit helpers
├── tests/                 Public Alpha host tests
├── docs/                  Development, validation, and source-import reports
├── VERSION                Current public root build: 0.1.0-alpha
└── v33-beta/              Separate C++/ELF64 source subtree (import pending)
```

**Do not overwrite `boot/` or `kernel/` with the v33 code.** Import v33 under `v33-beta/` on a dedicated branch first. After the source import, v33 build instructions will be in `v33-beta/README.md`.

## ビルド / Build

### Public Alpha 0.1 (current repository root, Windows PowerShell)

Requirements: LLVM/Clang, LLD, QEMU x86-64 and OVMF/EDK2.

```powershell
.\scripts\build.ps1
.\scripts\run-qemu.ps1
```

Optional serial-log smoke test (does **not** validate GUI or installation):

```powershell
.\scripts\test-qemu-boot.bat --timeout 120
```

### v33.0 Beta (separate **local source package**)

Build the separately provided full v33 source package; these commands are **not** for the current repository root:

```powershell
.\build.ps1
.\verify-beta.ps1
```

On Linux with Clang/LLD:

```bash
make -j2 all
```

The source-only ZIP intentionally excludes generated images, including the FAT32 rootfs image required by some packaging and runtime tests. Use the separate verified buildable source package for complete local builds until this dependency is reproducibly generated from source.

To upload all v33 sources, follow [GitHub source import guide](docs/V33-SOURCE-IMPORT.md). The import requires an authenticated Git push; updating this README alone does not publish the v33 code.

## 今後の目標 / Next steps

1. Publish the reviewed **source-only** `v33-beta/` tree in a separate PR without images, ISO, or credentials.
2. CI-build the EFI loader and ELF64 kernel with Clang/LLD, retaining Alpha CI.
3. Run and record QEMU/OVMF boot logs, GUI/input and installed virtual-disk reboot tests.
4. Fix remaining device, installation, and recovery regressions before any release or stable badge.

See [ROADMAP](docs/ROADMAP.md), [CHANGELOG](CHANGELOG.md), and [Security](SECURITY.md).

## License

MIT. See [LICENSE](LICENSE).

---

**English summary:** zeroNT is a from-scratch NT-inspired hobby OS. The GitHub repository currently holds a CI-tested Alpha baseline and documentation about the separately built v33.0 Beta source package. The v33 source itself will be published under its own subtree, and no production-ready claim is made.
