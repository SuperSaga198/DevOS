#!/bin/sh
set -eu

qemu=$1
iso=$2
log=$(mktemp "${TMPDIR:-/tmp}/devos-user-process.XXXXXX")
trap 'rm -f "$log"' EXIT HUP INT TERM

set +e
{
    sleep 6
    printf 'devuser\rdevhost\r'
    sleep 3
    printf 'run demo\r'
    sleep 2
} | timeout 24s "$qemu" -m 2G -cdrom "$iso" \
    -display none -serial stdio -monitor none -no-reboot -no-shutdown \
    >"$log" 2>&1
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ]; then
    cat "$log"
    echo "QEMU exited before the user-process smoke-test timeout (status $qemu_status)." >&2
    exit 1
fi

if ! grep -a -q 'Ring-3 user-process isolation initialized.' "$log" ||
   ! grep -a -q 'Ring-3 ELF application: isolated process says hello.' "$log" ||
   ! grep -a -q 'Ring-3 process exited cleanly.' "$log" ||
   ! grep -a -q 'ELF app scheduled in isolated user mode' "$log" ||
   [ "$(grep -a -c 'Ring-3 ELF application: isolated process says hello.' "$log")" -lt 2 ]; then
    cat "$log"
    echo "Ring-3 user process did not complete its write/exit syscalls." >&2
    exit 1
fi

echo "QEMU isolated ring-3 process and syscall test passed."
