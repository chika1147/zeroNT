# Building NTZero

The Alpha 0.1 public repository layout is prepared for the NTZero source tree.

The expected QEMU image is:

```text
build/ntzero.img
```

## Windows

```powershell
.\scripts\run-qemu.ps1
```

## macOS/Linux

```bash
chmod +x scripts/run-qemu.sh
./scripts/run-qemu.sh
```

## Debug mode

```powershell
.\scripts\run-qemu.ps1 -DebugMode
```

```bash
./scripts/run-qemu.sh --debug
```
