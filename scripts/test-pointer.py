#!/usr/bin/env python3
import json
import os
import socket
import subprocess
import sys
import tempfile
import time


def connect_socket(path, process):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited with status {process.returncode}")
        try:
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.settimeout(1)
            connection.connect(path)
            return connection
        except (FileNotFoundError, ConnectionRefusedError):
            time.sleep(0.1)
    raise TimeoutError(f"Timed out connecting to QEMU socket: {path}")


def qmp_command(stream, command):
    stream.write((json.dumps(command) + "\n").encode())
    while True:
        response = json.loads(stream.readline())
        if "event" in response:
            continue
        if "error" in response:
            raise RuntimeError(f"QMP command failed: {response['error']}")
        return response.get("return")


def send_pointer_input(stream, pressed):
    events = [
        {"type": "rel", "data": {"axis": "x", "value": 40}},
        {"type": "rel", "data": {"axis": "y", "value": -20}},
        {"type": "btn", "data": {"button": "left", "down": pressed}},
    ]
    qmp_command(stream, {
        "execute": "input-send-event",
        "arguments": {"events": events},
    })


def send_serial_command(serial, command, expected_buttons):
    serial.sendall(command.encode() + b"\r")
    output = bytearray()
    deadline = time.monotonic() + 5
    expected = f"; buttons: {expected_buttons}"
    while time.monotonic() < deadline:
        try:
            output.extend(serial.recv(4096))
        except socket.timeout:
            pass
        text = output.decode(errors="replace")
        if "PS/2 pointer packets: " in text and expected in text:
            lines = [line for line in text.splitlines()
                     if "PS/2 pointer packets:" in line]
            if lines:
                return lines[-1]
    raise AssertionError(
        f"Guest did not report pointer status for {command!r}: "
        f"{output.decode(errors='replace')}"
    )


def main():
    qemu, iso = sys.argv[1:3]
    with tempfile.TemporaryDirectory(prefix="devos-pointer-") as directory:
        serial_path = os.path.join(directory, "serial.sock")
        qmp_path = os.path.join(directory, "qmp.sock")
        process = subprocess.Popen([
            qemu, "-m", "2G", "-cdrom", iso, "-display", "none",
            "-no-reboot", "-no-shutdown",
            "-serial", f"unix:{serial_path},server=on,wait=off",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            serial = connect_socket(serial_path, process)
            qmp = connect_socket(qmp_path, process)
            qmp_stream = qmp.makefile("rwb", buffering=0)
            greeting = json.loads(qmp_stream.readline())
            if "QMP" not in greeting:
                raise AssertionError("QEMU did not send a QMP greeting")
            qmp_command(qmp_stream, {"execute": "qmp_capabilities"})

            boot_output = bytearray()
            deadline = time.monotonic() + 12
            while (time.monotonic() < deadline and
                   b"PS/2 mouse initialized (IRQ12)." not in boot_output):
                try:
                    boot_output.extend(serial.recv(4096))
                except socket.timeout:
                    pass
            if b"PS/2 mouse initialized (IRQ12)." not in boot_output:
                raise AssertionError(
                    "PS/2 mouse failed to initialize: " +
                    boot_output.decode(errors="replace")
                )

            send_pointer_input(qmp_stream, True)
            time.sleep(0.3)
            pressed = send_serial_command(serial, "pointer", 1)
            if "; buttons: 1" not in pressed:
                raise AssertionError(f"Mouse button state missing: {pressed}")
            if "; position: 680,380;" not in pressed:
                raise AssertionError(f"Mouse motion missing: {pressed}")

            send_pointer_input(qmp_stream, False)
            time.sleep(0.3)
            released = send_serial_command(serial, "pointer", 0)
            if "; buttons: 0" not in released:
                raise AssertionError(f"Mouse button release missing: {released}")
            print("QEMU PS/2 pointer input test passed.")
            print(pressed)
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
