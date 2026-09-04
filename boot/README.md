# NTZero Boot

Alpha 0.1 boots through a self-contained x86-64 UEFI application.

```text
UEFI firmware
  -> EFI/BOOT/BOOTX64.EFI
  -> embedded PE32+ ntoskrnl.exe mapped at ImageBase 0x10000000
  -> final UEFI memory map
  -> ExitBootServices()
  -> KiSystemStartup(NTZ_BOOT_INFO*)
```

The boot loader source is in `boot/uefi/boot.c`.
