$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Esp = Join-Path $Root "build\esp"
$BootEfi = Join-Path $Esp "EFI\BOOT\BOOTX64.EFI"

if (-not (Test-Path $BootEfi)) {
    Write-Host "NTZero has not been built yet. Running build.ps1..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "build.ps1")
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

& $Qemu.Source `
    -machine q35 `
    -m 2048 `
    -smp 4 `
    -bios $Ovmf `
    -drive "format=raw,file=fat:rw:$Esp" `
    -serial stdio `
    -no-reboot

exit $LASTEXITCODE
