# zeroNT v32.12 changelog

- Kept the supplied backup classic UI as the visual base.
- Added an off-screen RAM framebuffer to `classic_ui`.
- Added `present()` and `present_rect()`.
- Installer progress now presents only its changed progress region.
- Setup/Login and Desktop publish completed frames instead of drawing visible intermediate states.
- Preserved audio, network, installer, FAT32, driver, event-log, beta-monitor and security processing.
- Expanded the kernel heap from 1 MiB to 16 MiB for the render buffer and normal runtime allocations.
