#!/usr/bin/env bash
# scripts/qemu/run.sh — Launch Cortex-A-OS on QEMU realview-pb-a8
#
# Requires: qemu-system-arm, kernel ELF in build/
# Usage: bash scripts/qemu/run.sh [--gdb]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

KERNEL="$PROJECT_ROOT/build/qemu/kernel.elf"
MACHINE="realview-pb-a8"
CPU="cortex-a8"
MEM="128M"
UART_LOG="-serial mon:vc"

if [ ! -f "$KERNEL" ]; then
    echo "[ERROR] Kernel ELF not found: $KERNEL"
    echo "        Run 'make PLATFORM=qemu' first."
    exit 1
fi

GDB_FLAGS=""
if [[ "${1:-}" == "--gdb" ]]; then
    GDB_FLAGS="-S -gdb tcp::1234"
    echo "[INFO] GDB server listening on :1234 — connect with:"
    echo "       arm-none-eabi-gdb $KERNEL"
    echo "       (gdb) target remote :1234"
fi

echo "[INFO] Launching QEMU..."
echo "       Machine : $MACHINE"
echo "       Kernel  : $KERNEL"
echo "       Exit    : Press Ctrl+A then 'x' to quit"

exec qemu-system-arm \
    -M "$MACHINE" \
    -cpu "$CPU" \
    -m "$MEM" \
    $UART_LOG \
    -kernel "$KERNEL" \
    $GDB_FLAGS
