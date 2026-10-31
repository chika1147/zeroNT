param(
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if ($Rebuild) {
    & .\build.ps1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# v33.0 Beta is an ELF64-kernel OS. Old kernel.bin paths belonged to earlier layouts.
$required = @(
    'build\BOOTX64.EFI',
    'build\kernel.elf',
    'disk\EFI\BOOT\BOOTX64.EFI',
    'disk\System\KERNEL.ELF',
    'disk\System\rootfs.img',
    'disk\boot-media.img',
    'build.ps1',
    'run.ps1'
)
$failed = $false
foreach ($f in $required) {
    if (Test-Path -LiteralPath $f -PathType Leaf) {
        $i = Get-Item -LiteralPath $f
        Write-Host "PASS $f ($($i.Length) bytes)" -ForegroundColor Green
    } else {
        Write-Host "FAIL missing: $f" -ForegroundColor Red
        $failed = $true
    }
}
if ($failed) { exit 1 }

Write-Host ''
Write-Host 'SHA256 (current build artifacts):'
Get-FileHash -Path @(
    'build\BOOTX64.EFI',
    'build\kernel.elf',
    'disk\System\rootfs.img',
    'disk\boot-media.img'
) -Algorithm SHA256 | Format-Table Path, Hash -AutoSize

Write-Host ''
Write-Host 'Runtime beta checks inside zeroNT CMD (manual; NOT verified by this script):'
Write-Host '  HEALTH   BETA   EVENTS   FSCK'
Write-Host '  IPCONFIG PING   TCPTEST'
Write-Host '  NSLOOKUP example.com'
Write-Host ''
Write-Host 'PASS: build artifacts exist; hashes computed. QEMU/VMware boot not tested.' -ForegroundColor Green
exit 0
