# NTZero Alpha 0.1 Build

Alpha 0.1 contains a real x86-64 UEFI-to-native-kernel boot path.

## Requirements on Windows

- LLVM/Clang (`clang.exe`)
- LLD (`lld-link.exe`)
- QEMU x86-64
- EDK2/OVMF x86-64 firmware

## Build

```powershell
.\scripts\build.ps1
```

Outputs:

```text
build/ntoskrnl.exe
build/esp/EFI/BOOT/BOOTX64.EFI
build/esp/NTZERO/ntoskrnl.exe
```

`ntoskrnl.exe` is linked as PE32+ x86-64 with the Windows NT Native subsystem and preferred ImageBase `0x10000000`.

The UEFI loader embeds the kernel PE image, maps its sections at the preferred image base, retrieves the final UEFI memory map, calls `ExitBootServices()`, then jumps to `KiSystemStartup`.

## Run

No stage number or CPU-count argument is required:

```powershell
.\scripts\run-qemu.ps1
```

The default QEMU configuration uses 2 GiB RAM and 4 virtual CPUs.

Expected serial output begins with:

```text
NTZero Alpha 0.1
KiSystemStartup: x86_64 native kernel entered
BOOT: UEFI ExitBootServices complete
ALPC: kernel transport initialized
NTZero Alpha 0.1: READY
```

## ALPC status

The Alpha 0.1 tree contains a native kernel ALPC transport core with connection ports, client/server communication ports, message IDs, send/receive queues, accept/connect and disconnect operations.

The next kernel step is wiring `STATUS_PENDING` waits into the scheduler's blocking wait/wake path so `NtAlpcSendWaitReceivePort` can sleep and wake threads rather than returning pending when no message is available.

## Validation boundary

The current public source was compiled and linked with Clang/LLD 17 in the build environment. The ALPC host transport test passed. QEMU runtime boot was not executed in that environment because `qemu-system-x86_64` was unavailable there.
