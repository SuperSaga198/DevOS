#!/bin/sh
set -eu

qemu=$1
iso=$2
log=$(mktemp "${TMPDIR:-/tmp}/devos-network.XXXXXX")
trap 'rm -f "$log"' EXIT HUP INT TERM

set +e
{
    sleep 6
    printf 'devuser\rdevhost\r'
    sleep 1
    printf 'net\rping\rnet\r'
    sleep 1
} | timeout 22s "$qemu" -m 2G -cdrom "$iso" \
    -netdev user,id=devos-net -device e1000,netdev=devos-net \
    -display none -serial stdio -monitor none -no-reboot -no-shutdown \
    >"$log" 2>&1
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ]; then
    cat "$log"
    echo "QEMU exited before the network smoke-test timeout (status $qemu_status)." >&2
    exit 1
fi

if ! grep -a -q 'Intel e1000 NIC ready at 10.0.2.15.' "$log" ||
   ! grep -a -q 'e1000 ready; static IPv4 10.0.2.15' "$log" ||
   ! grep -a -q '10.0.2.2 replied to ICMP echo' "$log"; then
    cat "$log"
    echo "e1000 network smoke test did not complete gateway ping." >&2
    exit 1
fi

echo "QEMU e1000 receive/transmit and gateway ICMP test passed."
