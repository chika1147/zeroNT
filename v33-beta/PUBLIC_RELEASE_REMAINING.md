# zeroNT - remaining work before the public release

Current branch: **v33.0 Beta**

The next product stage after Beta is the **public release**. The items below are
remaining engineering work inside Beta, not extra release labels.


## Display / UI status

Completed in the Dynamic Resolution upgrade:
- removed the 640x480-style fixed boot assumption
- GOP auto resolution selection
- actual framebuffer width/height/pitch propagation
- resolution-aware classic shell/window defaults
- Ring-3 screen-size syscall and resolution-aware Native EXE sizing

Remaining for true live resize:
- VMware SVGA II / QEMU-native display mode-change driver
- runtime mode-change notification and relayout event

## 1. Driver / hardware remaining after the v33.0 HW+Memory upgrade

Already added in this upgrade:
- ACPI checksum validation and MADT/HPET/MCFG discovery
- Local APIC detection/software enable-state
- PCI MSI/MSI-X capability discovery
- bounded NVMe READ/WRITE/FLUSH retries
- AHCI failure-path DMA cleanup
- xHCI root-port hot-plug tracking

Still remaining:

### USB class layer
1. Enable Slot command
2. Address Device command
3. endpoint/context setup
4. descriptor requests
5. HID boot keyboard
6. HID boot mouse
7. Bulk-Only Transport / USB Mass Storage
8. unplug/replug cleanup and timeout recovery

### Interrupt / PCIe migration
1. full I/O APIC/GSI routing migration
2. runtime-tested MSI/MSI-X migration for individual drivers
3. broader PCIe ECAM usage
4. chipset/device quirks and physical-machine validation

## 2. Memory remaining

Already added in this upgrade:
- PMM OOM accounting
- invalid/double-free detection
- contiguous physical-page allocation and reservation
- PMM/heap consistency validation
- guarded Ring-3 user stacks
- guarded kernel-thread stacks
- owned user-page reclamation
- recursive user page-table reclamation

Remaining before/after publication depending on scope:
1. user-facing mmap/unmap API
2. shared-memory objects / IPC mapping
3. stronger page-fault diagnostics and optional demand allocation
4. optional copy-on-write
5. SMP-safe memory locking when multi-core scheduling is enabled

## 3. Filesystem naming and repair
1. FAT32 LFN create/write/rename
2. LFN checksum validation
3. stronger FAT-chain repair
4. interrupted directory-tree operation recovery
5. disk-full and bad-chain stress tests

## 4. User-process model
Private CR3 address spaces are now implemented. Remaining:
1. asynchronous process scheduling
2. user threads
3. wait/kill handles
4. IPC/message channels
5. user-mode file APIs
6. remaining user-mode shell/app migration

## 5. Application migration
Continue moving the classic apps from kernel shell components into independent
Native EXE processes while keeping the current UI behavior.

## 6. Network publication quality
The current TCP client is intentionally small. Remaining:
1. multiple simultaneous sockets
2. retransmission timers
3. receive-window handling
4. out-of-order queue
5. MSS/options
6. TCP server/listen path
7. socket handle lifecycle
8. TLS

## 7. Hardware coverage
Expand beyond the current QEMU/VMware-oriented devices:
- more NICs
- broader audio hardware
- additional AHCI/NVMe quirks
- USB controllers/devices
- Intel and AMD UEFI PCs

## 8. Installer / recovery
Before publication:
- fresh-disk install test
- existing-user/data-preservation test
- failed-install recovery
- power-loss simulation
- SafeFS recovery
- boot recovery
- clean-reset/reinstall path

## 9. Final Beta tests
- repeated clean builds
- 100+ boot loop
- long idle/soak test
- large file copy/move/delete
- disk-full behavior
- repeated Native EXE launch/exit
- network loss/reconnect
- USB plug/unplug
- installer reset/reinstall
- real-hardware boot

Only after these are verified should the same Beta branch be packaged as the
zeroNT public release.
