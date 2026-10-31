# zeroNT v33.0 Beta — Native EXE v2 + Double-Click Fix

## Native Ring-3 EXE architecture

The previous SYSTEM32 EXE package used small Ring-3 launchers that handed execution back to the kernel shell. This update changes the normal application path so SYSTEM32 applications execute their own code in Ring 3.

Implemented in this update:

- private per-process CR3/address space for native EXEs
- private user stack and code pages
- cooperative Ring-3 yield/resume (`SysYield`)
- saved/restored full syscall frame
- process lifetime/reaping for EXE tasks
- user-owned GUI windows and event queues
- window title, begin-paint and text drawing syscalls
- keyboard/mouse routing to the active user-mode window
- process-owned GUI window cleanup on exit
- 13 separately built `/SYSTEM32/*.EXE` applications

Native applications in this build:

- EXPLORER.EXE
- NOTEPAD.EXE
- CMD.EXE
- CONTROL.EXE
- TASKMGR.EXE
- DEVMGMT.EXE
- RECYCLE.EXE
- SERVICES.EXE
- EVENTVWR.EXE
- MYCOMP.EXE
- NETWORK.EXE
- MYDOCS.EXE
- PRINTERS.EXE

The legacy kernel-hosted implementations remain as Beta compatibility/fallback code, but the normal SYSTEM32 EXE launch path no longer uses the old `SysShellOpenApp` launcher bridge.

## Double-click behavior fix

A regression had caused desktop and shell items to open on a single left click. The shell now uses one shared double-click tracker.

- first click: select only
- second click on the same item: open
- double-click interval: 500 ms (50 PIT ticks at 100 Hz)
- pointer movement tolerance: 16 px
- desktop icons: double-click
- Explorer items: double-click
- My Computer items: double-click
- My Documents pane: double-click
- Start menu: intentionally remains single-click, matching classic Windows behavior

The selected desktop item is also visually highlighted.

## Beta note

This update completes the native execution model for the 13 SYSTEM32 applications, but feature parity is still a Beta task. Some new Ring-3 applications currently expose a smaller feature set than the older kernel-hosted versions. In particular, full Explorer file-management parity and full Notepad Open/Save persistence still need to be ported into user-mode application APIs before the public release.
