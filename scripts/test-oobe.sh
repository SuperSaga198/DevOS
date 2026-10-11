#!/bin/sh
set -eu

qemu=$1
iso=$2
disk=$(mktemp "${TMPDIR:-/tmp}/devos-oobe-disk.XXXXXX")
log=$(mktemp "${TMPDIR:-/tmp}/devos-oobe-log.XXXXXX")
trap 'rm -f "$disk" "$log"' EXIT HUP INT TERM

truncate -s 16M "$disk"

run_qemu()
{
    phase=$1
    : >"$log"
    set +e
    if [ "$phase" = setup ]; then
        {
            sleep 6
            printf 'devuser\rdevhost\r'
            sleep 1
        } | timeout 20s "$qemu" -m 2G -cdrom "$iso" \
            -drive "file=$disk,format=raw,if=ide,index=0,media=disk" \
            -display none -serial stdio -monitor none -no-reboot -no-shutdown \
            >"$log" 2>&1
    else
        sleep 6 | timeout 20s "$qemu" -m 2G -cdrom "$iso" \
            -drive "file=$disk,format=raw,if=ide,index=0,media=disk" \
            -display none -serial stdio -monitor none -no-reboot -no-shutdown \
            >"$log" 2>&1
    fi
    qemu_status=$?
    set -e

    if [ "$qemu_status" -ne 124 ]; then
        cat "$log"
        echo "QEMU exited before the OOBE smoke-test timeout (status $qemu_status)." >&2
        exit 1
    fi
}

require_log()
{
    if ! grep -a -q "$1" "$log"; then
        cat "$log"
        echo "OOBE test did not find expected output: $1" >&2
        exit 1
    fi
}

run_qemu setup
require_log 'OOBE setup saved to disk.'

run_qemu reboot
require_log 'OOBE profile loaded: devuser@devhost'

echo "OOBE first-boot setup and persistent profile reboot test passed."
