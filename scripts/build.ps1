$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build"
$Obj = Join-Path $Build "obj"
$Gen = Join-Path $Build "generated"
$Esp = Join-Path $Build "esp"
$EfiBoot = Join-Path $Esp "EFI\BOOT"
$EfiNt = Join-Path $Esp "NTZERO"
$Include = Join-Path $Root "include"

function Find-Tool([string]$Name, [string]$Fallback) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    if (Test-Path $Fallback) { return $Fallback }
    throw "$Name was not found. Install LLVM/Clang and add it to PATH."
}

$Clang = Find-Tool "clang.exe" "C:\Program Files\LLVM\bin\clang.exe"
$Lld = Find-Tool "lld-link.exe" "C:\Program Files\LLVM\bin\lld-link.exe"

New-Item -ItemType Directory -Force $Obj, $Gen, $EfiBoot, $EfiNt | Out-Null
Get-ChildItem $Obj -File -ErrorAction SilentlyContinue | Remove-Item -Force

$Common = @(
    "-target", "x86_64-pc-windows-msvc",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-mno-red-zone",
    "-Wall", "-Wextra", "-Werror",
    "-I$Include"
)

$KernelSources = @(
    "kernel\entry.c",
    "kernel\serial.c",
    "kernel\alpc.c",
    "kernel\memory.c",
    "kernel\interrupts.c",
    "drivers\core.c",
    "drivers\pci.c",
    "drivers\pci_msi.c",
    "drivers\legacy.c",
    "drivers\apic.c",
    "drivers\acpi.c",
    "drivers\storage.c",
    "drivers\nvme.c",
    "drivers\usb.c",
    "drivers\virtio.c",
    "drivers\net.c",
    "drivers\video.c",
    "drivers\audio_wireless.c",
    "drivers\hid.c",
    "drivers\usb_mass.c"
)

$KernelObjects = @()
Write-Host "[1/5] Building ntoskrnl.exe + kernel + drivers" -ForegroundColor Cyan
foreach ($relative in $KernelSources) {
    $name = ([System.IO.Path]::GetFileNameWithoutExtension($relative)) + ".obj"
    if ($relative.StartsWith("drivers\")) { $name = "drv-" + $name }
    $source = Join-Path $Root $relative
    $object = Join-Path $Obj $name
    & $Clang @Common "-mgeneral-regs-only" "-c" $source "-o" $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $relative" }
    $KernelObjects += $object
}

$Kernel = Join-Path $Build "ntoskrnl.exe"
$LinkArgs = @(
    "/subsystem:native",
    "/entry:KiSystemStartup",
    "/nodefaultlib",
    "/machine:x64",
    "/base:0x10000000",
    "/fixed",
    "/out:$Kernel"
) + $KernelObjects
& $Lld @LinkArgs
if ($LASTEXITCODE -ne 0) { throw "ntoskrnl.exe link failed" }

Write-Host "[2/5] Embedding kernel into UEFI loader" -ForegroundColor Cyan
$KernelBytes = [System.IO.File]::ReadAllBytes($Kernel)
$BlobPath = Join-Path $Gen "kernel_blob.c"
$Builder = [System.Text.StringBuilder]::new()
[void]$Builder.AppendLine("typedef unsigned char UINT8;")
[void]$Builder.AppendLine("typedef unsigned long long UINT64;")
[void]$Builder.AppendLine("const UINT8 g_ntzero_kernel_blob[] = {")
for ($i = 0; $i -lt $KernelBytes.Length; $i += 16) {
    $end = [Math]::Min($i + 15, $KernelBytes.Length - 1)
    $items = for ($j = $i; $j -le $end; $j++) { "0x{0:X2}" -f $KernelBytes[$j] }
    [void]$Builder.AppendLine("  " + ($items -join ", ") + ",")
}
[void]$Builder.AppendLine("};")
[void]$Builder.AppendLine("const UINT64 g_ntzero_kernel_blob_size = sizeof(g_ntzero_kernel_blob);")
[System.IO.File]::WriteAllText($BlobPath, $Builder.ToString(), [System.Text.Encoding]::ASCII)

Write-Host "[3/5] Building BOOTX64.EFI" -ForegroundColor Cyan
& $Clang @Common "-fshort-wchar" "-c" (Join-Path $Root "boot\uefi\boot.c") "-o" (Join-Path $Obj "boot.obj")
if ($LASTEXITCODE -ne 0) { throw "UEFI loader compile failed" }
& $Clang @Common "-c" $BlobPath "-o" (Join-Path $Obj "kernel_blob.obj")
if ($LASTEXITCODE -ne 0) { throw "Kernel blob compile failed" }

$BootEfi = Join-Path $EfiBoot "BOOTX64.EFI"
& $Lld "/subsystem:efi_application" "/entry:EfiMain" "/nodefaultlib" "/machine:x64" "/out:$BootEfi" `
    (Join-Path $Obj "boot.obj") (Join-Path $Obj "kernel_blob.obj")
if ($LASTEXITCODE -ne 0) { throw "BOOTX64.EFI link failed" }

Write-Host "[4/5] Preparing ESP" -ForegroundColor Cyan
Copy-Item $Kernel (Join-Path $EfiNt "ntoskrnl.exe") -Force
Set-Content -Path (Join-Path $EfiNt "VERSION.TXT") -Value "NTZero Alpha 0.1`r`n0.1.0-alpha" -Encoding ASCII

Write-Host "[5/5] Build complete" -ForegroundColor Green
Write-Host "  Kernel : $Kernel"
Write-Host "  UEFI   : $BootEfi"
Write-Host "  Sources: $($KernelSources.Count) kernel/driver modules compiled"
Write-Host "  ESP    : $Esp"
