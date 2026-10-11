#!/usr/bin/env bash
set -euo pipefail

qemu=${1:-qemu-system-x86_64}
iso=${2:-build/devos.iso}

run_test() {
    printf '\n==> %s\n' "$*"
    "$@"
}

run_test bash scripts/test-oobe.sh "$qemu" "$iso"
run_test bash scripts/test-storage.sh "$qemu" "$iso"
run_test python3 scripts/test-pointer.py "$qemu" "$iso"
run_test bash scripts/test-scheduler.sh "$qemu" "$iso"
run_test bash scripts/test-audio.sh "$qemu" "$iso"
run_test bash scripts/test-network.sh "$qemu" "$iso"
run_test python3 scripts/test-udp.py "$qemu" "$iso"
run_test python3 scripts/test-tcp.py "$qemu" "$iso"
run_test bash scripts/test-user-process.sh "$qemu" "$iso"
run_test bash scripts/test-editor.sh "$qemu" "$iso"
run_test python3 scripts/measure-memory.py "$qemu" "$iso"

printf '\nQEMU VM preflight passed. This does not certify VMware compatibility.\n'
