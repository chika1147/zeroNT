# zeroNT source map

## Currently available in public `main`

```text
boot/uefi/boot.c           UEFI PE32+ loader for the Alpha kernel
kernel/entry.c             Alpha KiSystemStartup
kernel/interrupts.c        IDT and exception-vector foundation
kernel/memory.c            PMM / contiguous DMA foundation
kernel/alpc.c              ALPC transport core
drivers/nvme.c             Polled NVMe Identify, no block I/O
drivers/pci_msi.c          PCI MSI capability/configuration foundation
include/ntzero/            Public Alpha headers
scripts/                  Alpha build/QEMU, serial checker, v33 archive inventory
tests/                    Host-side tests
```

## v33.0 Beta Stage 1–4 source mapping (separate package, **import pending**)

```text
v33-beta/bootloader/main.cpp   UEFI PE application + ELF64 handoff
v33-beta/bootloader/efi.hpp    UEFI interfaces
v33-beta/kernel/kernel.cpp     v33 kernel entry/runtime
v33-beta/kernel/classic_ui.cpp Classic desktop UI
v33-beta/kernel/fat32.cpp      FAT32 storage tree functions
v33-beta/kernel/installer.cpp  Install/repair/upgrade logic
v33-beta/kernel/usb.cpp        xHCI foundation
v33-beta/kernel/network.cpp    Network foundation
v33-beta/kernel/nvme.cpp       NVMe engine
v33-beta/build.ps1             Windows toolchain build
v33-beta/Makefile              Linux/Clang build
```

These `v33-beta/` paths describe the reviewed source package and will become GitHub paths only after its source-only import PR is merged. Do not overwrite Alpha's `boot/`, `kernel/`, or `VERSION` merely to change the README badge.
