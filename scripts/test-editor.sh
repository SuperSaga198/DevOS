#!/bin/sh
set -eu

qemu=$1
iso=$2
log=$(mktemp "${TMPDIR:-/tmp}/devos-editor.XXXXXX")
trap 'rm -f "$log"' EXIT HUP INT TERM

set +e
{
    sleep 6
    printf 'devuser\rdevhost\r'
    sleep 4
    printf 'edit /scratch.txt\r'
    sleep 1
    printf 'hello from editor\n'
    sleep 1
    printf ':w\r'
    sleep 1
    printf ':q\r'
    sleep 1
    printf 'cat /scratch.txt\r'
    sleep 2
} | timeout 25s "$qemu" -m 2G -cdrom "$iso" \
    -display none -serial stdio -monitor none -no-reboot -no-shutdown \
    >"$log" 2>&1
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ]; then
    cat "$log"
    echo "QEMU exited before the text-editor smoke-test timeout (status $qemu_status)." >&2
    exit 1
fi

if ! grep -a -q 'Text editor open: /scratch.txt' "$log" ||
   ! grep -a -q 'Text editor saved.' "$log" ||
   ! grep -a -q 'hello from editor' "$log"; then
    cat "$log"
    echo "Editor did not open and save the typed file contents." >&2
    exit 1
fi

echo "QEMU terminal text-editor save/read test passed."
