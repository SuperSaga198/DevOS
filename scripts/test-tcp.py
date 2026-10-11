#!/usr/bin/env python3
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main() -> int:
    qemu, iso = sys.argv[1:3]
    with tempfile.TemporaryDirectory(prefix="devos-tcp-") as directory:
        log_path = Path(directory) / "qemu.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [
                    qemu,
                    "-m", "2G",
                    "-cdrom", iso,
                    "-netdev",
                    "user,id=devos-net,hostfwd=tcp::10008-10.0.2.15:7008",
                    "-device", "e1000,netdev=devos-net",
                    "-display", "none",
                    "-serial", "stdio",
                    "-monitor", "none",
                    "-no-reboot",
                    "-no-shutdown",
                ],
                stdin=subprocess.PIPE,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            try:
                time.sleep(6)
                if process.poll() is not None:
                    raise RuntimeError("QEMU exited before first-run setup")
                assert process.stdin is not None
                process.stdin.write(b"devuser\rdevhost\r")
                process.stdin.flush()
                time.sleep(4)
                process.stdin.write(b"ping\r")
                process.stdin.flush()
                time.sleep(2)

                payload = b"DevOS TCP echo verified"
                deadline = time.monotonic() + 18
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError("QEMU exited during TCP test")
                    try:
                        with socket.create_connection(
                            ("127.0.0.1", 10008), timeout=1
                        ) as client:
                            client.settimeout(3)
                            client.sendall(payload)
                            response = bytearray()
                            while len(response) < len(payload):
                                chunk = client.recv(len(payload) - len(response))
                                if not chunk:
                                    break
                                response.extend(chunk)
                            if bytes(response) != payload:
                                raise RuntimeError(
                                    f"unexpected TCP payload: {bytes(response)!r}"
                                )
                        process.stdin.write(b"tcp\r")
                        process.stdin.flush()
                        time.sleep(1)
                        log.flush()
                        output = log_path.read_bytes()
                        if not re.search(
                            rb"TCP echo service: IPv4 10\.0\.2\.15 port "
                            rb"7008; replies [1-9][0-9]* of [1-9][0-9]*",
                            output,
                        ):
                            raise RuntimeError(
                                "guest echoed TCP data but did not record "
                                "the connection in its counters"
                            )
                        break
                    except (ConnectionRefusedError, socket.timeout):
                        time.sleep(0.25)
                else:
                    process.stdin.write(b"net\rtcp\r")
                    process.stdin.flush()
                    time.sleep(1)
                    log.flush()
                    boot_log = log_path.read_text(errors="replace")
                    raise RuntimeError(
                        "guest did not echo TCP data; QEMU output:\n"
                        + boot_log[-6000:]
                    )

                log.flush()
                output = log_path.read_bytes()
                if b"Intel e1000 NIC ready at 10.0.2.15." not in output:
                    raise RuntimeError("guest did not initialize the e1000 NIC")
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
    print("QEMU e1000 TCP echo service test passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"TCP smoke test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
