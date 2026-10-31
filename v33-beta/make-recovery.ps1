$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
if (-not (Test-Path 'disk\EFI\BOOT\BOOTX64.EFI') -or -not (Test-Path 'disk\System\kernel.bin')) { & .\build.ps1; if($LASTEXITCODE-ne 0){exit $LASTEXITCODE} }
$dst='recovery'
New-Item -ItemType Directory -Force -Path "$dst\EFI\BOOT","$dst\System","$dst\Config" | Out-Null
Copy-Item disk\EFI\BOOT\BOOTX64.EFI "$dst\EFI\BOOT\BOOTX64.EFI" -Force
Copy-Item disk\System\kernel.bin "$dst\System\kernel.bin" -Force
Copy-Item disk\System\rootfs.img "$dst\System\rootfs-factory.img" -Force
Copy-Item disk\Config\* "$dst\Config" -Force
@'
zeroNT v32 Recovery
- EFI/BOOT/BOOTX64.EFI: known-good loader
- System/kernel.bin: known-good kernel
- System/rootfs-factory.img: factory filesystem image

SafeFS also performs interrupted-transaction recovery automatically at boot.
Use these files to restore a damaged installation from another OS/environment.
'@ | Set-Content "$dst\README.txt" -Encoding ASCII
Write-Host 'Recovery set refreshed.'
