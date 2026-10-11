#!/bin/sh
set -eu

qemu=$1
iso=$2
log=$(mktemp "${TMPDIR:-/tmp}/devos-scheduler.XXXXXX")
trap 'rm -f "$log"' EXIT HUP INT TERM

set +e
{
    sleep 6
    printf 'devuser\rdevhost\r'
    sleep 2
    printf 'ps\r'
    sleep 1
} | timeout 18s "$qemu" -m 2G -cdrom "$iso" \
    -display none -serial stdio -monitor none -no-reboot -no-shutdown \
    >"$log" 2>&1
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ]; then
    cat "$log"
    echo "QEMU exited before the scheduler smoke-test timeout (status $qemu_status)." >&2
    exit 1
fi

if ! grep -a -q 'Preemptive kernel-task scheduler ready; service monitor started.' "$log" ||
   ! grep -a -q 'Service-monitor kernel task executing.' "$log" ||
   ! grep -a -q 'SCHEDULED TASK' "$log" ||
   ! grep -a -E -q '(running|ready) +[1-9][0-9]* +service-monitor' "$log"; then
    cat "$log"
    echo "Scheduler smoke test did not observe a running service task." >&2
    exit 1
fi

echo "QEMU preemptive scheduler and service-task test passed."
