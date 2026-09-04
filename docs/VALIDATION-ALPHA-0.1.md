# NTZero Alpha 0.1 — source validation

Validated in the artifact build environment with Clang/LLD 17.

```text
ntoskrnl.exe : PE32+ x86-64, Native subsystem, ImageBase 0x10000000
BOOTX64.EFI  : PE32+ x86-64, EFI application
ALPC host test: PASS (create/connect/accept/heartbeat send/receive)
```

The build used warnings-as-errors (`-Wall -Wextra -Werror`).

Runtime QEMU boot was not executed in that environment because `qemu-system-x86_64` was unavailable. The included `scripts/run-qemu.ps1` is the runtime test path for Windows with QEMU/OVMF installed.
