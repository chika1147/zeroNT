# zeroNT v33.0 Beta - Apps EXE Upgrade

## What changed

- Added `/SYSTEM32` native executable directory.
- Added 13 system `.EXE` files: Explorer, Notepad, CMD, Control Panel, Task Manager, Device Manager, Recycle Bin, Services, Event Viewer, My Computer, Network, My Documents, and Printers.
- Start menu, desktop icons, and shell navigation now launch through the Ring-3 executable loader instead of calling the app entry directly.
- Added `SysShellOpenApp` syscall (60) for the native EXE -> shell bridge.
- `RUN NAME` now tries `/SYSTEM32/NAME.EXE` first and keeps `/APPS/NAME.ZEX` compatibility.
- `APPS` lists `/SYSTEM32` executables.
- RootFS verification now checks all 13 system EXEs plus the 5 existing ZEX test apps.

## EXE format

For this Beta, `.EXE` means **zeroNT Native EXE v1**. It uses the existing `ZEXE` Ring-3 container and `.EXE` filename extension. It is not a Windows PE executable. This keeps the current loader and memory-isolation path stable while moving system applications out into executable files.

## Beta fallback

If a system EXE is missing or corrupt, the shell temporarily falls back to the internal implementation and shows `EXE LOAD FAILED - INTERNAL FALLBACK USED`. This is intentional for Beta recovery and can be removed for the public release once EXE launching is soak-tested.
