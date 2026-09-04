$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Esp = Join-Path $Root "build\esp"
$BootEfi = Join-Path $Esp "EFI\BOOT\BOOTX64.EFI"
$NvmeImage = Join-Path $Root "build\nvme-test.img"

if (-not (Test-Path $BootEfi)) {
    Write-Host "NTZero has not been built yet. Running build.ps1..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "build.ps1")
}

if (-not (Test-Path $NvmeImage)) {
    $stream = [System.IO.File]::Open($NvmeImage, [System.IO.FileMode]::Create, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::Read)
    try { $stream.SetLength(64MB) } finally { $stream.Dispose() }
}

$Qemu = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
if (-not $Qemu) { $Qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue }
if (-not $Qemu) { throw "qemu-system-x86_64 was not found in PATH." }

$OvmfCandidates = @(
    $env:NTZERO_OVMF_CODE,
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
    "C:\Program Files\qemu\share\edk2-x86_64-secure-code.fd"
) | Where-Object { $_ -and (Test-Path $_) }

if ($OvmfCandidates.Count -eq 0) {
    throw "OVMF/EDK2 firmware was not found. Set NTZERO_OVMF_CODE to your edk2 x86_64 code.fd path."
}
$Ovmf = $OvmfCandidates[0]

Write-Host "NTZero Alpha 0.1" -ForegroundColor Cyan
Write-Host "QEMU : $($Qemu.Source)"
Write-Host "OVMF : $Ovmf"
Write-Host "ESP  : $Esp"
Write-Host "NVMe : $NvmeImage (64 MiB test namespace)" -ForegroundColor DarkCyan
Write-Host "HW   : q35 + NVMe + xHCI + e1000 + VirtIO Net + VirtIO RNG" -ForegroundColor DarkCyan

& $Qemu.Source `
    -machine q35 `
    -m 2048 `
    -smp 4 `
    -bios $Ovmf `
    -drive "format=raw,file=fat:rw:$Esp" `
    -drive "file=$NvmeImage,if=none,format=raw,id=nvmedisk" `
    -device "nvme,drive=nvmedisk,serial=NTZERO0001" `
    -device "qemu-xhci,id=xhci" `
    -net none `
    -netdev "user,id=e1000net" `
    -device "e1000,netdev=e1000net" `
    -netdev "user,id=virtionet" `
    -device "virtio-net-pci,netdev=virtionet" `
    -device "virtio-rng-pci" `
    -serial stdio `
    -no-reboot

exit $LASTEXITCODE
