# zeroNT v32 Beta Test Plan

## Soak tests
- 1 hour: idle Desktop + mouse/keyboard use
- 6 hours: Explorer/Notepad + repeated saves + audio/network use
- 24 hours: persistent disk, periodic file writes, DHCP/DNS/ping checks

Run `HEALTH`, `BETA`, `EVENTS`, and `FSCK` before and after each test. A good run has no kernel faults, no growing AHCI error count, a healthy filesystem, and no unexplained drop in free-memory low-water marks.

## Filesystem stress
Create/edit/rename/delete files repeatedly, reboot between cycles, and verify old/new contents. SafeFS uses a transaction journal, temporary file, read-back verification, backup, rename, flush, and boot recovery.

## Crash tests
Run Ring-3 fault smoke tests and third-party ZEX apps. User faults should terminate/isolate the app path; kernel faults remain fatal and are recorded in the event log.

## Network tests
DHCP, PING, DNS lookup and TCP response probe. DNS has retries/timeouts; PING/TCPTEST now wait for a response instead of only reporting transmit success.

## Hardware tests
Test QEMU first, then a dedicated non-critical PC/USB. Keep backups. Validate SATA/AHCI, PS/2 input, UEFI GOP, ACPI, audio and E1000-equivalent hardware separately. NVMe and xHCI are still probe/foundation drivers, not general production I/O drivers.
