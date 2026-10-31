# zeroNT v33.0 Beta - Dynamic Resolution / GOP Auto Mode

## Goal
Remove the old 640x480-style fixed display assumption and make the desktop/UI use the actual UEFI GOP framebuffer resolution.

## Implemented
- UEFI GOP mode enumeration before ExitBootServices.
- `video=auto` boot setting. Auto mode selects the largest direct-framebuffer GOP mode up to 1920x1200 for the current software renderer.
- `video=WIDTHxHEIGHT` exact GOP mode request with safe fallback.
- `video=current` keeps the firmware-selected mode.
- BootInfo always carries the selected width, height, pitch (`PixelsPerScanLine`) and pixel format.
- Renderer continues to use framebuffer pitch correctly instead of assuming width == pitch.
- Classic UI backbuffer moved out of the 16 MiB kernel heap and onto PMM pages so higher resolutions do not consume most of the heap.
- Classic shell default windows now size from the actual work area instead of 720x520 constants.
- Native Ring-3 GUI windows are capped against the actual work area rather than 760x520.
- New `SysGuiScreenSize` syscall lets Native EXE applications read the active display size.
- Native EXE v2 applications choose their initial window size from the active screen and repaint to their actual client size.
- Existing mouse pointer clipping already uses BootInfo width/height and therefore follows the selected resolution.
- Command Prompt `MODE` command reports the active display size and framebuffer pitch.

## Boot configuration
Default:

```text
video=auto
```

Optional examples:

```text
video=1024x768
video=1280x800
video=1920x1080
video=current
```

If an exact requested mode is not offered by the firmware, zeroNT safely keeps the firmware mode instead of aborting boot.

## Important limitation
This upgrade follows the GOP mode selected **at boot**. After `ExitBootServices`, VMware/QEMU live host-window resizing cannot change the GOP framebuffer by itself. True live resize after boot needs a native display driver (for example VMware SVGA II or a QEMU display device driver), which is a separate later driver task.
