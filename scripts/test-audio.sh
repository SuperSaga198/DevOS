#!/bin/sh
set -eu

qemu=$1
iso=$2
log=$(mktemp "${TMPDIR:-/tmp}/devos-audio.XXXXXX")
trap 'rm -f "$log"' EXIT HUP INT TERM

set +e
{
    sleep 6
    printf 'devuser\rdevhost\r'
    sleep 1
    printf 'beep 880 100\raudio\r'
    sleep 1
} | timeout 18s "$qemu" -m 2G -cdrom "$iso" \
    -display none -serial stdio -monitor none -no-reboot -no-shutdown \
    >"$log" 2>&1
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ]; then
    cat "$log"
    echo "QEMU exited before the audio smoke-test timeout (status $qemu_status)." >&2
    exit 1
fi

if ! grep -a -q 'PC speaker tone played' "$log" ||
   ! grep -a -q 'PC speaker PIT tone output; no PCM/mixer backend' "$log"; then
    cat "$log"
    echo "PC speaker smoke test did not observe tone playback." >&2
    exit 1
fi

echo "QEMU PC speaker tone command test passed."
