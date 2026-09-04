param(
    [switch]$DebugMode,
    [int]$MemoryMB = 2048,
    [int]$CpuCount = 4
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Image = Join-Path $RepoRoot "build\ntzero.img"

$qemu = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
if (-not $qemu) {
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
}

if (-not $qemu) {
    Write-Host ""
    Write-Host "NTZero Alpha 0.1" -ForegroundColor Cyan
    Write-Host "ERROR: qemu-system-x86_64 was not found." -ForegroundColor Red
    Write-Host "Install QEMU and make sure it is in PATH."
    exit 1
}

if (-not (Test-Path $Image)) {
    Write-Host ""
    Write-Host "NTZero Alpha 0.1" -ForegroundColor Cyan
    Write-Host "ERROR: Disk image not found:" -ForegroundColor Red
    Write-Host "  $Image"
    Write-Host ""
    Write-Host "Build or copy the NTZero image to build\ntzero.img first."
    exit 1
}

$args = @(
    "-machine", "q35",
    "-m", "$MemoryMB",
    "-smp", "$CpuCount",
    "-drive", "file=$Image,format=raw",
    "-serial", "stdio",
    "-no-reboot"
)

if ($DebugMode) {
    $log = Join-Path $RepoRoot "qemu-debug.log"
    $args += @(
        "-d", "int,cpu_reset,guest_errors",
        "-D", $log,
        "-s",
        "-S"
    )
    Write-Host "Debug mode enabled." -ForegroundColor Yellow
    Write-Host "GDB server: localhost:1234"
    Write-Host "QEMU log: $log"
}

Write-Host ""
Write-Host "NTZero Alpha 0.1" -ForegroundColor Cyan
Write-Host "Image : $Image"
Write-Host "RAM   : $MemoryMB MB"
Write-Host "CPUs  : $CpuCount"
Write-Host ""

& $qemu.Source @args
exit $LASTEXITCODE
