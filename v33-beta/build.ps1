$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Need($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $c) { throw "$name not found. Install LLVM and reopen PowerShell." }
    return $c.Source
}

$clang   = Need 'clang'
$clangxx = Need 'clang++'
$lldlink = Need 'lld-link'
$ldlld   = Need 'ld.lld'

New-Item -ItemType Directory -Force -Path build | Out-Null
New-Item -ItemType Directory -Force -Path disk\EFI\BOOT | Out-Null
New-Item -ItemType Directory -Force -Path disk\System | Out-Null
if (-not (Test-Path 'disk\System\rootfs.img')) { throw 'disk\System\rootfs.img not found.' }

Write-Host '=== zeroNT v33.0 ELF64 PIE build ==='
& $clangxx -target x86_64-pc-windows-msvc -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fshort-wchar -mno-red-zone -fno-stack-protector -Wall -Wextra -Wno-unused-parameter -Ibootloader -Ishared -c bootloader\main.cpp -o build\bootloader.obj
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $lldlink /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64 /out:build\BOOTX64.EFI build\bootloader.obj
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$kflags = @('--target=x86_64-unknown-none-elf','-std=c++17','-ffreestanding','-fno-exceptions','-fno-rtti','-mno-red-zone','-fno-stack-protector','-fpie','-fno-plt','-nostdlib','-mgeneral-regs-only','-Wall','-Wextra','-Wno-unused-parameter','-Ishared','-Ikernel')
$sources = @(
 'kernel','classic_ui','memory','paging','heap','block_device','pci','ahci','fat32','process','scheduler','usermode','syscall','executable','gui','driver_manager','nvme','usb','acpi','platform_hardware','display','audio','network','user_manager','system_apps','quality','security','safe_fs','event_log','beta','installer','interrupts','runtime'
)
$objs = @()
foreach($n in $sources){
    Write-Host "CXX $n.cpp"
    & $clangxx @kflags -c "kernel\$n.cpp" -o "build\$n.o"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += "build\$n.o"
}
& $clang --target=x86_64-unknown-none-elf -ffreestanding -mno-red-zone -c kernel\arch.S -o build\arch.o
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$objs += 'build\arch.o'

Write-Host 'LINK kernel.elf (static PIE / relocatable)'
& $ldlld -nostdlib -pie --no-dynamic-linker -Bsymbolic -z max-page-size=0x1000 -T kernel\linker.ld @objs -o build\kernel.elf
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'Building native Ring-3 SYSTEM32 applications...'
New-Item -ItemType Directory -Force -Path build\userapps | Out-Null
$userAppFlags = @('--target=x86_64-unknown-none-elf','-std=c++17','-ffreestanding','-fno-exceptions','-fno-rtti','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-red-zone','-fno-stack-protector','-fpie','-fno-plt','-nostdlib','-mgeneral-regs-only','-Os','-ffunction-sections','-fdata-sections','-Iapps')
$userApps = @(
    @{Name='EXPLORER'; Kind=1}, @{Name='NOTEPAD'; Kind=2},
    @{Name='CMD'; Kind=3},      @{Name='CONTROL'; Kind=4},
    @{Name='TASKMGR'; Kind=5},  @{Name='DEVMGMT'; Kind=6},
    @{Name='RECYCLE'; Kind=7},  @{Name='SERVICES'; Kind=8},
    @{Name='EVENTVWR'; Kind=9}, @{Name='MYCOMP'; Kind=10},
    @{Name='NETWORK'; Kind=11}, @{Name='MYDOCS'; Kind=12},
    @{Name='PRINTERS'; Kind=13}
)
foreach($a in $userApps){
    $obj = "build\userapps\$($a.Name).o"
    $bin = "build\userapps\$($a.Name).bin"
    & $clangxx @userAppFlags "-DAPP_KIND=$($a.Kind)" -c apps\system_app.cpp -o $obj
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $ldlld -nostdlib -static --gc-sections -T apps\linker.ld --oformat=binary $obj -o $bin
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host 'Injecting /SYSTEM32 .EXE apps and /APPS .ZEX tests into rootfs...'
& $clangxx -std=c++17 -O2 -Ikernel tools\inject_apps.cpp kernel\block_device.cpp kernel\fat32.cpp -o build\inject_apps.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& build\inject_apps.exe disk\System\rootfs.img build\userapps
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item build\BOOTX64.EFI disk\EFI\BOOT\BOOTX64.EFI -Force
Copy-Item build\kernel.elf disk\System\KERNEL.ELF -Force

Write-Host 'Building FAT32 UEFI boot media...'
& $clangxx -std=c++17 -O2 tools\mkbootmedia.cpp -o build\mkbootmedia.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& build\mkbootmedia.exe disk\boot-media.img disk\EFI\BOOT\BOOTX64.EFI build\kernel.elf disk\System\rootfs.img
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host 'Build complete:'
Write-Host '  disk\EFI\BOOT\BOOTX64.EFI'
Write-Host '  disk\System\KERNEL.ELF'
Write-Host '  disk\System\rootfs.img'
Write-Host '  disk\boot-media.img'
