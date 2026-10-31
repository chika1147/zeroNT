# zeroNT v33.0 Beta - Runtime Display Driver + Settings

## Added

- Runtime display driver abstraction (`kernel/display.cpp` / `kernel/include/display.hpp`).
- VMware SVGA II PCI driver path (vendor `15AD`, SVGA II device IDs).
- QEMU/Bochs VBE DISPI driver path for QEMU stdvga.
- UEFI GOP remains the safe fallback when no runtime-capable display device is found.
- New Ring-3 display syscalls:
  - `SysDisplayCaps` (18)
  - `SysDisplaySetMode` (19)
- `CONTROL.EXE` now contains a Display Settings page.
- Display driver name, current resolution and maximum reported resolution are shown in Settings.
- Runtime selectable presets:
  - 800x600
  - 1024x768
  - 1280x720
  - 1366x768
  - 1600x900
  - 1920x1080
  - 1280x800
  - 1920x1200
- Selected mode is persisted as `/SYSTEM/DISPLAY.CFG` on the installed FAT32 volume.
- Saved runtime resolution is reapplied during later boots when the VMware/QEMU display driver is available.
- Classic UI framebuffer backbuffer can now be rebuilt after a mode change.
- Ring-3 GUI windows are clamped and repainted after a runtime resolution change.
- Internal Control Panel fallback and Device Manager show the active display driver.

## Behavior

- VMware: Settings can request a new SVGA II mode without reboot when the emulated SVGA II device is available.
- QEMU stdvga: Settings can request a new Bochs VBE mode without reboot when the DISPI interface is available.
- Other machines: zeroNT continues using the UEFI GOP mode selected by the bootloader. Runtime mode buttons safely fail instead of corrupting the framebuffer.

## Safety

- Minimum runtime mode: 640x480.
- Mode is rejected when it exceeds the display driver's reported maximum.
- Framebuffer MMIO above 4 GiB is explicitly mapped before use.
- UI backbuffer memory is reallocated for the new screen size.
- Existing Ring-3 windows are brought back inside the new desktop bounds.

## Validation status

The source compiles and links cleanly in the artifact environment and the generated UEFI ISO structure is valid. Actual VMware/QEMU runtime mode switching still needs to be tested by booting the ISO in those hypervisors; compile/ISO validation alone does not prove the emulated hardware path works on every VMware/QEMU version.
