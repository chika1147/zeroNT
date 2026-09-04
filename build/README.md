# NTZero Build Output

Generated build files are intentionally ignored by Git.

Run:

```powershell
.\scripts\build.ps1
```

The build creates:

```text
build/ntoskrnl.exe
build/esp/EFI/BOOT/BOOTX64.EFI
build/esp/NTZERO/ntoskrnl.exe
```

Then start QEMU with:

```powershell
.\scripts\run-qemu.ps1
```
