#!/usr/bin/env python3
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def process_rss_kib(pid: int) -> int:
    status = Path(f"/proc/{pid}/status").read_text()
    match = re.search(r"^VmRSS:\s+(\d+)\s+kB$", status, re.MULTILINE)
    if match is None:
        raise RuntimeError("QEMU VmRSS field was not available")
    return int(match.group(1))


def wait_for(
    log_path: Path, needle: bytes, deadline: float, occurrences: int = 1
) -> bytes:
    while time.monotonic() < deadline:
        output = log_path.read_bytes()
        if output.count(needle) >= occurrences:
            return output
        time.sleep(0.1)
    raise RuntimeError(f"guest did not print {needle.decode(errors='replace')!r}")


def sample_peak_rss(pid: int, duration: float) -> int:
    peak = 0
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        peak = max(peak, process_rss_kib(pid))
        time.sleep(0.1)
    return peak


def send(process: subprocess.Popen, command: bytes) -> None:
    if process.stdin is None or process.poll() is not None:
        raise RuntimeError("QEMU exited while measuring memory")
    process.stdin.write(command)
    process.stdin.flush()


def allocator_stats(output: bytes) -> tuple[int, int]:
    matches = re.findall(
        rb"Managed pages: (\d+) \([^)]*\), free: (\d+) pages", output
    )
    if not matches:
        raise RuntimeError("guest did not report physical allocator statistics")
    return tuple(map(int, matches[-1]))


def main() -> int:
    if len(sys.argv) != 3:
        raise RuntimeError("usage: measure-memory.py QEMU ISO")
    qemu, iso = sys.argv[1:3]
    with tempfile.TemporaryDirectory(prefix="devos-memory-") as directory:
        log_path = Path(directory) / "qemu.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [
                    qemu, "-m", "2G", "-cdrom", iso,
                    "-display", "none", "-serial", "stdio",
                    "-monitor", "none", "-no-reboot", "-no-shutdown",
                ],
                stdin=subprocess.PIPE,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            try:
                wait_for(log_path, b"OOBE required.", time.monotonic() + 18)
                send(process, b"devuser\rdevhost\r")
                wait_for(
                    log_path, b"Press T to open the terminal",
                    time.monotonic() + 10,
                )
                wait_for(
                    log_path, b"Ring-3 process exited cleanly.",
                    time.monotonic() + 5,
                )
                idle_rss = sample_peak_rss(process.pid, 2)

                send(process, b"mem\r")
                first_output = wait_for(
                    log_path, b"ignored above 4 GiB:", time.monotonic() + 5
                )
                baseline = allocator_stats(first_output)

                send(process, b"pagealloc\r")
                allocation_output = wait_for(
                    log_path, b"Allocated physical page",
                    time.monotonic() + 5,
                )
                send(process, b"mem\r")
                after_alloc_output = wait_for(
                    log_path, b"ignored above 4 GiB:",
                    time.monotonic() + 5, occurrences=2,
                )
                allocated = allocator_stats(after_alloc_output)
                working_rss = sample_peak_rss(process.pid, 2)

                send(process, b"pagefree\r")
                wait_for(log_path, b"Freed physical page", time.monotonic() + 5)
                send(process, b"mem\r")
                final_output = wait_for(
                    log_path, b"ignored above 4 GiB:",
                    time.monotonic() + 5, occurrences=3,
                )
                restored = allocator_stats(final_output)

                if baseline[0] != allocated[0] or baseline[0] != restored[0]:
                    raise RuntimeError("managed page count changed during measurement")
                if baseline[1] != allocated[1] + 1:
                    raise RuntimeError("pagealloc did not consume exactly one page")
                if baseline != restored:
                    raise RuntimeError(
                        "pagefree did not restore the baseline: "
                        f"before={baseline}, after={restored}"
                    )
                if b"Physical allocator ready:" not in allocation_output:
                    raise RuntimeError("allocator startup was not recorded")
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)

    allocated_pages = baseline[0] - baseline[1]
    print("QEMU guest RAM configured: 2048 MiB")
    print(f"QEMU process peak RSS after setup/idle: {idle_rss / 1024:.1f} MiB")
    print(f"QEMU process peak RSS during memory commands: {working_rss / 1024:.1f} MiB")
    print(f"Kernel allocator-managed pages in use: {allocated_pages} pages")
    print("One page allocation/free round-trip matched allocator counters.")
    print("Note: QEMU RSS is host/emulator-specific; allocator counters omit")
    print("static kernel image/BSS, framebuffer, and other non-allocator memory.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"memory measurement failed: {error}", file=sys.stderr)
        raise SystemExit(1)
