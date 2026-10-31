# zeroNT v33.0 Beta - PHASE 1-10 ALL UPGRADE

This package is the v33.0 Beta source tree after integrating the next Phase 1-10
upgrade pass.  It remains **Beta**.  The next product stage is the zeroNT public
release after the remaining hardware/runtime validation work is finished.

The classic zeroNT desktop is preserved: quiet backbuffer rendering, stable PS/2
input, installer/user system, SafeFS, recovery/event logging, external Ring-3 ZEX
applications, audio and networking.

## Phase 1 - Stability / Beta health
- clean freestanding build with `-Wall -Wextra`
- existing user-fault isolation, event log and SafeFS checks preserved
- Beta monitor now watches AHCI, NVMe, xHCI, network and security health
- process reaping prevents repeated ZEX launches from exhausting the process table
- Ring-3 paging U/S propagation bug fixed
- device/service/task/control panels expose more real driver/runtime state

## Phase 2 - NVMe / Storage
- real PCI NVMe controller initialization
- Admin SQ/CQ creation
- Identify Controller + Namespace
- I/O SQ/CQ creation
- namespace exposed through the common `BlockDevice` API
- polling READ / WRITE / FLUSH
- DMA-safe one-page bounce buffer
- command error / timeout statistics
- boot storage now tries AHCI first, then NVMe

The current driver uses one logical block per command and polling I/O.  Scatter /
gather, MSI-X and queue-per-CPU are public-release-era improvements.

## Phase 3 - USB / xHCI core
- xHCI PCI discovery and MMIO enable
- controller stop/reset/run sequence
- page-size validation
- DCBAA + scratchpad allocation
- command ring
- event ring + ERST
- MaxSlots configuration
- root-port connected-device polling
- xHCI timeout/health integration

This is a real host-controller core, but USB class enumeration is not yet complete.
HID and USB Mass Storage remain on the public-release checklist.

## Phase 4 - FAT32 / SafeFS
- FAT32 streaming file copy
- cross-directory move
- free-space reporting
- recursive directory copy
- recursive directory move
- recursive directory delete
- FAT32 stat API
- SafeFS tree-copy/tree-move/tree-delete wrappers and counters
- Explorer Copy/Cut/Paste/Recycle Bin now work on folder trees
- host-side recursive FAT32 regression test added

Current write namespace is still FAT 8.3.  LFN create/write remains pending.

## Phase 5 - Process / Ring 3
- ZEX PID lifecycle + automatic reaping
- each external launch receives fresh code and stack pages
- **private CR3/page-table root per external Ring-3 process**
- kernel supervisor mappings are shared readably by the kernel, while user PML4
  subtrees are private
- page tables and physical code/stack pages are released after process exit
- user pointer validation follows the currently active address space
- `SysProcessId` added
- Task Manager shows process/reap/Ring-3 information

External applications are still run synchronously by the current shell.  Fully
asynchronous user processes/threads and IPC remain future work.

## Phase 6 - External applications
The build now injects five independent `.ZEX` Ring-3 applications:

- `/APPS/HELLO.ZEX`
- `/APPS/WINDOW.ZEX`
- `/APPS/AUDIO.ZEX`
- `/APPS/NET.ZEX`
- `/APPS/USER.ZEX`

Run them from Command Prompt:

    RUN HELLO
    RUN WINDOW
    RUN AUDIO
    RUN NET
    RUN USER

They go through the ZEX loader, private Ring-3 address space and syscall path.
Explorer/Notepad/CMD themselves are still kernel shell components and have not
been falsely relabeled as external apps.

## Phase 7 - Network
Existing E1000, Ethernet, ARP, IPv4, ICMP, UDP, DHCP and DNS are preserved.

New in this pass:
- real one-client TCP state machine
- SYN -> SYN/ACK -> ACK handshake
- TCP sequence and acknowledgement tracking
- payload send/receive
- FIN close path
- TCP byte/connection statistics
- TCP connect/send/receive/close syscalls
- HTTP/1.0 GET helper over the TCP client
- `TCPSTAT` and `HTTPGET <host>` commands

This is intentionally a small polling TCP client, not a complete multi-socket
POSIX/BSD networking stack yet.

## Phase 8 - Audio
- AC97 PCM DMA path preserved
- runtime master/PCM volume control added
- `SysAudioSetVolume`
- `VOL 0..100` and `TONE <Hz>` commands
- `/APPS/AUDIO.ZEX` exercises the Ring-3 audio syscall path
- Control Panel volume buttons

## Phase 9 - Desktop / GUI
- original classic desktop retained
- Device Manager now reports active NVMe and xHCI state
- Services shows filesystem/NVMe/xHCI/network/TCP/audio health
- Task Manager shows process/reap/syscall state and a process list
- Control Panel shows volume/TCP/SafeFS/Beta status
- clickable volume, network ping and filesystem check controls
- existing drag, resize, minimize, maximize, taskbar, Start menu, context menu,
  selection and backbuffer behavior preserved

## Phase 10 - Explorer / CMD / Settings
Explorer:
- recursive folder Copy/Cut/Paste
- recursive Recycle Bin move/restore/delete
- existing Back/Forward/Up/Rename/Properties/Refresh behavior preserved

Command Prompt additions:
- `APPS`
- `PS`
- `FREE`
- `MKDIR`
- `RMDIR`
- `DEL`
- `COPY`
- `MOVE`
- `DRIVERS`
- `HEALTH`
- `FSCK`
- `NVME`
- `USB`
- `IPCONFIG`
- `PING`
- `NSLOOKUP`
- `TCPTEST`
- `TCPSTAT`
- `HTTPGET`
- `AUDIO`
- `VOL`
- `TONE`
- `RUN`

Settings/Control Panel:
- audio volume controls
- gateway ping test
- SafeFS check/recovery action
- richer service/device status

## Build

Windows PowerShell:

    .\build.ps1

QEMU without resetting installed user data:

    .\run.ps1

Fresh install test:

    .\run.ps1 -ResetDisk

Linux/Clang host build used for this package:

    make -j2 all
    make verify-rootfs
    make test-fat32-tree

## Validation status

Verified in the artifact build environment:
- all zeroNT kernel C++/assembly translation units compile
- BOOTX64.EFI links as x86-64 EFI application
- KERNEL.ELF links as ELF64 static PIE
- all five Ring-3 ZEX files are present and have valid ZEXE headers
- FAT32 recursive copy/move/delete regression test passes
- 96 MiB FAT32 boot media is generated successfully

QEMU/VMware is not installed in the artifact environment, so real boot/device
runtime behavior still has to be tested on your normal zeroNT QEMU/VMware setup.

## Remaining before public release

The Phase 1-10 Beta upgrade does not pretend that every hardware feature is
finished.  The biggest remaining public-release items are:

- xHCI USB device enumeration + HID + Mass Storage
- FAT Long File Name create/write
- asynchronous multi-process execution + IPC
- move the remaining shell applications from kernel components to user-mode ZEX
- broader hardware drivers beyond the current QEMU-friendly set
- full multi-socket/retransmission/window TCP behavior and TLS
- long soak tests, installer/recovery testing and real-hardware testing

See `PUBLIC_RELEASE_REMAINING.md`.
