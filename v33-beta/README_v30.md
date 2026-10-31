# zeroNT v30 — Quality Core Pack 6–10

This is the cumulative ALL-IN-ONE zeroNT build. It keeps the v29 mouse-first desktop, system apps, user system, Ring 3, syscalls, executable loader, drivers, audio/network, AHCI and native FAT32, then adds quality/stability/security work for items 6–10.

## 6. Crash protection
- Ring 3 exception isolation for divide error, invalid opcode, GPF and page fault
- A crashing user app returns a negative exit code instead of intentionally halting the whole kernel
- Kernel faults still stop safely and record diagnostics
- Fault statistics: type, RIP, error code and CR2 for page faults
- Ring 3 invalid-opcode smoke test is included for runtime verification

## 7. Filesystem safety
- SafeFS wrapper for important FAT32 writes
- Write to temporary file, verify readback, keep a backup, then rename into place
- Rollback path on failure and explicit flush
- Boot-time FAT32 sanity check
- User database, profile data, Notepad saves and Explorer copy/paste use the safer path

SafeFS is a best-effort verified/backup/rename layer on FAT32. It is not a journal filesystem and cannot guarantee perfect atomicity through every possible sudden power loss.

## 8. Driver quality
- AHCI read/write/flush operation counters
- Up to 3 command attempts with retry/error/timeout counters
- Controller error/status clearing between retries
- Driver statistics exposed in Device Manager and CMD
- Existing NVMe and USB driver foundations remain available for later full I/O implementation

## 9. Network practicality
- E1000 Ethernet remains the main QEMU NIC
- DHCP retry handling
- DNS server ARP resolution
- UDP DNS A-record lookup
- ICMP ping support
- TCP SYN probe foundation
- CMD: IPCONFIG, PING, NSLOOKUP <host>, TCPTEST
- TX failure and RX drop statistics

The TCP code in v30 is a handshake/probe foundation, not yet a complete reliable TCP stream/socket stack.

## 10. User / Security quality
- User pointer validation for selected syscalls through page-table checks
- GUI window ownership checks
- Permission-denied and invalid-pointer audit counters
- Password policy: 8–47 printable ASCII characters, at least one letter and one digit
- Salted, iterated SHA-256 development KDF increased to 8192 rounds
- 5 failed logins trigger a 30-second temporary lockout
- Login success/failure/lockout statistics
- Password buffers are explicitly overwritten when cleared

The current password KDF is for this development OS. It is not a replacement for a production-grade Argon2/PBKDF2/scrypt implementation.

## Existing cumulative features retained
- UEFI boot loader + separate kernel.bin + ExitBootServices
- PMM, x86-64 paging, kernel heap
- GDT/TSS/IDT, PIC/PIT, PS/2 keyboard/mouse IRQ
- Thread/process scheduler, Ring 3 and syscall layer
- Native FAT32 read/write, PCI and AHCI SATA
- Executable/app loader, GUI API and event queue
- Driver manager, NVMe/USB/ACPI foundations
- AC97 audio and E1000 networking
- User setup/login/permissions and persistent profiles
- Mouse-first Desktop, Explorer, Notepad, CMD, Control Panel, Task Manager, Device Manager and Recycle Bin
- Window drag/resize/minimize/maximize, context menus, double-click, wheel scrolling, clipboard, Undo/Redo, MessageBox/error UI

## Useful CMD commands

```text
HELP
WHOAMI
VER
PWD
DIR
DRIVERS
HEALTH
IPCONFIG
PING
NSLOOKUP example.com
TCPTEST
AUDIO
CLS
ECHO hello
```

## Windows PowerShell build / run

```powershell
.\build.ps1
.\run.ps1
```

Use a fresh disk only when desired:

```powershell
.\run.ps1 -ResetDisk
```

The normal run path keeps `run\zeront-sata.img`, so users and files persist between boots.

## Verification performed for this package
- BOOTX64.EFI compile/link: PASS
- kernel.elf compile/link: PASS
- raw kernel.bin generation: PASS
- FAT32 create/write/append/read/rename/delete/rmdir host test: PASS
- SafeFS verified write/overwrite host test: PASS
- User/password/temporary-lockout policy host test: PASS
- rootfs v30 metadata injection: PASS

The current build environment does not have QEMU/OVMF installed, so real runtime verification of Ring 3 fault recovery, AHCI retries, DHCP/DNS and TCP probing must be performed with `run.ps1` on the Windows QEMU machine.
