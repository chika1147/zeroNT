# zeroNT v33.0 Beta - Phase 1-10 upgrade changelog

## Stability
- extended Beta health monitoring to NVMe/xHCI/network/security
- fixed Ring-3 parent-page-table U/S propagation
- removed the xHCI unused helper warning
- process table now reaps exited external applications

## Storage
- added polling NVMe controller/namespace driver
- Admin + I/O queues
- Identify Controller/Namespace
- READ/WRITE/FLUSH through BlockDevice
- AHCI -> NVMe boot-storage fallback

## USB
- xHCI controller stop/reset/run
- DCBAA/scratchpads
- command/event rings and ERST
- root-port connection polling

## Filesystem
- FAT32 stat + recursive copy/move/delete
- SafeFS recursive wrappers/counters
- Explorer folder clipboard/recycle operations use recursive tree API
- added host recursive tree regression test

## Process / user mode
- private CR3/page-table root for each external ZEX launch
- fresh code + stack pages
- user address-space cleanup after exit
- current address-space-aware pointer validation
- SysProcessId

## External apps
- HELLO.ZEX
- WINDOW.ZEX
- AUDIO.ZEX
- NET.ZEX
- USER.ZEX

## Network
- one-client TCP connection state machine
- connect/send/receive/close APIs
- sequence/ack tracking
- HTTP/1.0 GET helper
- new TCP syscalls
- TCPSTAT / HTTPGET shell commands

## Audio
- AC97 volume control
- volume syscall
- VOL and TONE commands
- AUDIO.ZEX

## Desktop / GUI
- richer Device Manager
- richer Services
- process list in Task Manager
- Control Panel runtime controls for volume, ping and filesystem check

## Explorer / CMD / Settings
- recursive folder Copy/Cut/Paste
- recursive Recycle Bin behavior
- expanded command set for storage, process, network, USB, NVMe, audio and filesystem health

## Build validation
- clean warning-free `make -j2 all`
- five ZEX images verified
- recursive FAT32 tests pass
- boot-media.img generated

## Still Beta
USB class drivers, LFN create/write, asynchronous processes/IPC, full shell-app
migration, complete multi-socket TCP/TLS and real hardware/VM soak validation
remain before the public release.
