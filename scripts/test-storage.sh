#!/bin/sh
set -eu

qemu=$1
iso=$2
disk=$(mktemp "${TMPDIR:-/tmp}/devos-storage.XXXXXX")
log=$(mktemp "${TMPDIR:-/tmp}/devos-storage-log.XXXXXX")
trap 'rm -f "$disk" "$log"' EXIT HUP INT TERM

truncate -s 16M "$disk"

require_log()
{
    if ! grep -a -q "$1" "$log"; then
        cat "$log"
        echo "Storage test did not find expected output: $1" >&2
        exit 1
    fi
}

run_qemu()
{
    phase=$1
    : >"$log"
    set +e
    if [ "$phase" = write ]; then
        {
            sleep 6
            printf 'devuser\rdevhost\r'
            sleep 1
            printf 'touch /persisted\r'
            sleep 0.5
            printf 'write /persisted persistent across reboot\r'
            sleep 1
        } | timeout 20s "$qemu" -m 2G -cdrom "$iso" \
            -drive "file=$disk,format=raw,if=ide,index=0,media=disk" \
            -display none -serial stdio -monitor none -no-reboot -no-shutdown \
            >"$log" 2>&1
    else
        {
            sleep 6
            printf 'cat /persisted\r'
            sleep 1
        } | timeout 20s "$qemu" -m 2G -cdrom "$iso" \
            -drive "file=$disk,format=raw,if=ide,index=0,media=disk" \
            -display none -serial stdio -monitor none -no-reboot -no-shutdown \
            >"$log" 2>&1
    fi
    qemu_status=$?
    set -e

    if [ "$qemu_status" -ne 124 ]; then
        cat "$log"
        echo "QEMU exited before the storage smoke-test timeout (status $qemu_status)." >&2
        exit 1
    fi
}

run_qemu write
require_log 'PS/2 mouse initialized (IRQ12).'
require_log 'PCI enumeration complete:'
require_log 'IDE controllers: 1'
require_log 'ATA primary master ready: 32768 sectors'
require_log 'Persistent filesystem blank; changes will be saved.'
require_log 'OOBE setup saved to disk.'
require_log 'devos> write /persisted persistent across reboot'

run_qemu reboot
require_log 'Persistent filesystem loaded.'
require_log 'OOBE profile loaded: devuser@devhost'
require_log 'persistent across reboot'
echo "PCI/ATA discovery and persistent filesystem reboot test passed."
