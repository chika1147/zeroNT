# Real hardware checklist

zeroNT v32 is still a beta candidate, not a safe replacement for a main OS.

1. Use a spare machine or removable test disk.
2. Back up all data first.
3. UEFI x86-64 must be enabled; Secure Boot may need to be disabled because zeroNT is unsigned.
4. Start with SATA/AHCI hardware; current AHCI is the mature storage path.
5. PS/2 keyboard/mouse are the mature input path. xHCI/USB is detection/foundation only.
6. NVMe is detection/foundation only; do not rely on it for persistent data yet.
7. GPU support is UEFI GOP/framebuffer, not a native accelerated GPU driver.
8. Wi-Fi/Bluetooth are not implemented.
9. Run `HEALTH`, `BETA`, `EVENTS`, `FSCK` after boot.
10. Never test destructive storage changes on a disk containing important data.
