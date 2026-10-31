# zeroNT v32.11 Backup Classic UI

## Goal
Restore the visual language from the supplied backup `main.cpp` while keeping the modern v32.10 kernel and OS features.

## Visual changes
- Backup palette: teal desktop, 192-gray face, navy active title bar.
- Original backup two-level 3D bevel borders.
- Full printable ASCII 5x7 backup font (upper/lower case and symbols).
- Backup-style Start button, taskbar, Start menu and desktop icons.
- Backup-style window title/icon/minimize/maximize/close controls.
- Classic context menus and modal dialogs.
- Explorer, Notepad, Command Prompt and Recycle Bin use sunken client/status areas.
- Installer and account/login screens use the same classic component library.
- Desktop idle repaint reduced to once per second; input/mouse changes remain event-driven.

## Preserved v32 features
Integrated installer, persistent users, password/permissions, FAT32 read/write + SafeFS, AHCI, process/thread/scheduler, Ring 3/syscalls, system apps, audio/network foundations, driver manager, beta/event logging.

## Backup source
The user-supplied visual reference is preserved as `backup/classic-ui-main.cpp`.

## Build status
Clean build: PASS. Compiler warnings: 0. Compiler errors: 0.
