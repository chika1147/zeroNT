#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="$ROOT/build/ntzero.img"
DEBUG_MODE=0
MEMORY_MB="${NTZERO_MEMORY_MB:-2048}"
CPU_COUNT="${NTZERO_CPU_COUNT:-4}"

if [[ "${1:-}" == "--debug" ]]; then
    DEBUG_MODE=1
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "NTZero Alpha 0.1"
    echo "ERROR: qemu-system-x86_64 was not found."
    exit 1
fi

if [[ ! -f "$IMAGE" ]]; then
    echo "NTZero Alpha 0.1"
    echo "ERROR: Disk image not found: $IMAGE"
    exit 1
fi

ARGS=(
    -machine q35
    -m "$MEMORY_MB"
    -smp "$CPU_COUNT"
    -drive "file=$IMAGE,format=raw"
    -serial stdio
    -no-reboot
)

if [[ "$DEBUG_MODE" -eq 1 ]]; then
    ARGS+=(
        -d int,cpu_reset,guest_errors
        -D "$ROOT/qemu-debug.log"
        -s
        -S
    )
    echo "Debug mode enabled. GDB server: localhost:1234"
fi

echo "NTZero Alpha 0.1"
echo "Image : $IMAGE"
echo "RAM   : $MEMORY_MB MB"
echo "CPUs  : $CPU_COUNT"

exec qemu-system-x86_64 "${ARGS[@]}"
