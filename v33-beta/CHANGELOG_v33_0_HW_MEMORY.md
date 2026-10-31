# zeroNT v33.0 Beta - Hardware + Memory upgrade

This update implements the next two Beta priorities on top of Native EXE v2:

1. Driver / Hardware stabilization
2. Memory-management stabilization

## 1. Driver / Hardware

### ACPI
- validates the RSDP checksum
- validates ACPI SDT checksums before using tables
- discovers FADT, MADT, HPET and MCFG
- records Local APIC / I/O APIC information from MADT
- records HPET MMIO address
- records PCIe MCFG presence
- records ACPI reset-register information

### PCI / interrupt capabilities
- adds 8/16-bit PCI configuration access helpers
- walks PCI capability lists safely
- discovers MSI and MSI-X capability blocks
- rejects malformed capability loops/lists
- provides an MSI programming API for future device migration

### Platform timer / APIC
- detects Local APIC support with CPUID
- enables the Local APIC software-enable bit when available
- maps required MMIO ranges
- initializes the HPET main counter when ACPI exposes HPET
- exposes a monotonic hardware counter
- keeps the existing PIT scheduler path as the conservative Beta fallback

### AHCI
- tracks command-list / command-table physical pages
- cleans up DMA pages when a port fails initialization
- cleans up partial state when IDENTIFY fails
- preserves timeout/retry handling rather than hanging forever

### NVMe
- retains Admin Queue + I/O Queue initialization and namespace discovery
- READ / WRITE / FLUSH use bounded retry loops
- records retry statistics
- failed commands return cleanly instead of retrying forever

### xHCI / USB host tracking
- preserves the existing xHCI reset/run and ring setup
- root-port connection state is tracked as a bitmap
- connect/disconnect changes increment a hot-plug event counter
- Driver Manager receives hot-plug state

### Driver Manager / Device Manager
- tracks PCI device count changes
- tracks MSI/MSI-X-capable devices
- tracks malformed PCI capability lists
- tracks Local APIC and HPET status
- tracks xHCI hot-plug events
- Device Manager and CMD diagnostics show the new hardware state

## 2. Memory

### Physical Memory Manager
- adds multi-page contiguous allocations
- adds page-range reservation
- detects invalid frees and double frees
- records allocation failures / OOM events
- records current and peak page use
- adds PMM consistency validation

### Kernel heap
- validates heap block metadata with magic values
- detects double free / invalid free
- records OOM failures and peak use
- recalculates largest free block
- adds `krealloc`
- adds heap consistency validation

### User address spaces
- introduces a software `PageOwned` PTE bit
- user-owned physical pages are reclaimed when an address space is destroyed
- page-table destruction recursively frees lower page-table pages
- statistics track created/destroyed address spaces and reclaimed pages
- adds address-space-aware page queries

### Ring 3 safety
- user code and user stacks are marked as owned pages
- every user stack has an unmapped low guard page
- every user stack has an unmapped high guard page
- partial allocation failures unwind safely
- process/user-context teardown reclaims owned memory

### Kernel-thread stack safety
- scheduler stacks use a dedicated guarded virtual-address arena
- every stack has an unmapped guard page below and above it
- terminated thread stacks are reclaimed only after they are no longer current
- scheduler tracks guarded stacks, reaped stacks and allocation failures

### Diagnostics
- CMD `MEM` shows PMM free pages, PMM OOM count, heap free bytes,
  heap allocation failures, reclaimed user pages and guarded kernel stacks
- `MEM` also runs PMM + heap consistency checks

## Build / regression status

Validated in the artifact build environment:

- clean `make -j2 all`: PASS
- compiler warnings: 0
- compiler errors: 0
- EFI loader link: PASS
- kernel link: PASS
- 13 `/SYSTEM32/*.EXE` images: PASS
- 5 `/APPS/*.ZEX` test images: PASS
- recursive FAT32 mkdir/copy/move/delete test: PASS
- 96 MiB boot media generation: PASS

## Important Beta limitations

This is a real source-level upgrade, but it does **not** mean every hardware item
is finished yet.

Still remaining before the public release:

- xHCI USB device enumeration/class drivers (HID keyboard, HID mouse, mass storage)
- migration of external IRQ routing from the legacy PIC fallback to full I/O APIC/GSI routing
- switching individual drivers to MSI/MSI-X by default after runtime validation
- broader PCIe ECAM usage and real-machine chipset quirks
- true user `mmap` / shared-memory APIs and optional demand paging / copy-on-write
- VMware/QEMU and real-hardware runtime/soak validation

The scheduler intentionally remains on the existing PIT path until HPET/APIC
runtime behavior has been verified on the target virtual and physical machines.
