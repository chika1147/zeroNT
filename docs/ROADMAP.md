# NTZero Roadmap

## Alpha 0.1 public baseline

- x86-64 UEFI loader
- PE32+ NT Native kernel image
- `KiSystemStartup`
- serial boot diagnostics
- native ALPC transport core
- reproducible Windows build script
- no-argument QEMU launcher

## Next Alpha work

1. Scheduler-backed ALPC blocking wait/wake
2. Kernel wait objects and timer wakeups
3. IDT/exception foundation
4. APIC and SMP integration
5. Physical/virtual memory management
6. process/thread objects
7. Ring 3 and native syscalls
8. user-mode service process path
9. VFS/storage
10. USB/input/network/GUI expansion

## Later

A Beta release should only follow after repeatable QEMU runtime tests, stronger subsystem integration tests and a clearly documented supported-hardware/runtime matrix.
