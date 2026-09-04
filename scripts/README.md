# NTZero scripts

Windows build and run entry points:

```powershell
.\scripts\build.ps1
.\scripts\run-qemu.ps1
```

`run-qemu.ps1` intentionally requires no Stage or CPU-count argument. It builds automatically when `BOOTX64.EFI` is missing and defaults to 4 virtual CPUs.
