param(
    [switch]$ResetDisk,
    [switch]$KeepNvram
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

Write-Host '=== zeroNT v33.0 Phase 1-6 Core Upgrade ==='

# Build if the staged EFI/kernel files are missing.
if (-not (Test-Path 'disk\EFI\BOOT\BOOTX64.EFI') -or -not (Test-Path 'disk\System\KERNEL.ELF') -or -not (Test-Path 'disk\boot-media.img')) {
    & "$PSScriptRoot\build.ps1"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$bootFile = Join-Path $PSScriptRoot 'disk\EFI\BOOT\BOOTX64.EFI'
if (-not (Test-Path $bootFile)) {
    throw "zeroNT UEFI loader not found: $bootFile"
}

# boot-media.img is a real FAT32 filesystem containing the UEFI fallback path.
$qemuCmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
if (-not $qemuCmd) { throw 'qemu-system-x86_64.exe not found.' }
$qemu = $qemuCmd.Source
$qemuDir = Split-Path $qemu

$codeCandidates = @(
    (Join-Path $qemuDir 'share\edk2-x86_64-code.fd'),
    (Join-Path $qemuDir 'share\qemu\edk2-x86_64-code.fd'),
    (Join-Path $qemuDir 'pc-bios\edk2-x86_64-code.fd'),
    'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
)
$varsCandidates = @(
    (Join-Path $qemuDir 'share\edk2-i386-vars.fd'),
    (Join-Path $qemuDir 'share\qemu\edk2-i386-vars.fd'),
    (Join-Path $qemuDir 'pc-bios\edk2-i386-vars.fd'),
    'C:\Program Files\qemu\share\edk2-i386-vars.fd'
)

$code = $codeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$varsTemplate = $varsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $code) { throw 'edk2-x86_64-code.fd not found.' }
if (-not $varsTemplate) { throw 'edk2-i386-vars.fd not found.' }

New-Item -ItemType Directory -Force -Path run | Out-Null
$vars = Join-Path $PSScriptRoot 'run\edk2-vars.fd'

# Old OVMF variable stores can remember the UEFI Shell as the first boot target.
# For development AUTOBOOT mode, refresh it unless persistence was explicitly requested.
if (-not $KeepNvram -or -not (Test-Path $vars)) {
    Copy-Item $varsTemplate $vars -Force
    Write-Host 'UEFI NVRAM: fresh template (zeroNT autoboot scan enabled)'
} else {
    Write-Host 'UEFI NVRAM: keeping existing variable store'
}

$bootMediaSource = (Resolve-Path 'disk\boot-media.img').Path
$bootMedia = Join-Path $PSScriptRoot 'run\boot-media-runtime.img'
# IDE hard-disk devices need a writable block backend. Keep the shipped boot image pristine
# by cloning it to the run directory before each launch.
Copy-Item $bootMediaSource $bootMedia -Force
$storage = Join-Path $PSScriptRoot 'run\zeront-sata.img'
if ($ResetDisk -or -not (Test-Path $storage)) {
    # Blank installation target. zeroNT itself installs the system image.
    $template = Get-Item (Join-Path $PSScriptRoot 'disk\System\rootfs.img')
    $f = [System.IO.File]::Create($storage)
    $f.SetLength($template.Length)
    $f.Close()
    Write-Host 'Created blank SATA target. zeroNT Installer will run inside the OS.'
} else {
    Write-Host 'Reusing persistent zeroNT SATA disk (accounts/files preserved).'
}

$nvme = Join-Path $PSScriptRoot 'run\zeront-nvme.img'
if (-not (Test-Path $nvme)) {
    $f = [System.IO.File]::Create($nvme)
    $f.SetLength(64MB)
    $f.Close()
}

Write-Host "QEMU      : $qemu"
Write-Host "UEFI CODE : $code"
Write-Host "UEFI VARS : $vars"
Write-Host "BOOT EFI  : $bootFile"
Write-Host "Boot media source : $bootMediaSource"
Write-Host "Boot media runtime: $bootMedia"
Write-Host "SATA disk : $storage"
Write-Host ''
Write-Host 'Starting zeroNT v33.0 with low-memory QEMU TCG cache...'

& $qemu `
    -machine q35 `
    -accel "tcg,tb-size=64" `
    -m 512M `
    -drive "if=pflash,format=raw,readonly=on,file=$code" `
    -drive "if=pflash,format=raw,file=$vars" `
    -drive "if=none,format=raw,file=$bootMedia,id=espdisk" `
    -device ide-hd,drive=espdisk,bus=ide.1,bootindex=0 `
    -drive "file=$storage,if=none,format=raw,id=zerontdisk" `
    -device ide-hd,drive=zerontdisk,bus=ide.0,bootindex=1 `
    -drive "file=$nvme,if=none,format=raw,id=nvmedisk" `
    -device nvme,serial=ZERONTNVME,drive=nvmedisk `
    -device qemu-xhci,id=xhci `
    -netdev user,id=net0 `
    -device e1000,netdev=net0 `
    -audiodev dsound,id=audio0 `
    -device AC97,audiodev=audio0 `
    -device VGA `
    -serial stdio

exit $LASTEXITCODE
