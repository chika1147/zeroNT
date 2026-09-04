# NTZero Kernel

The Alpha 0.1 public kernel is a freestanding x86-64 PE32+ NT Native image.

Current source:

- `entry.c` — `KiSystemStartup` and boot handoff checks
- `serial.c` — COM1 serial diagnostics
- `alpc.c` — native ALPC transport core

The next integration step is scheduler-backed blocking wait/wake for ALPC and then broader kernel subsystems such as memory management, interrupts/APIC, processes/threads and user mode.
