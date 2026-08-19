#!/usr/bin/env python3
"""Provision the M15 Blynk credential blob over the confirmed UART0 link."""

from __future__ import annotations

import argparse
import binascii
import getpass
import os
import select
import struct
import sys
import termios
import time


PROTOCOL = "FUMURI-BLYNK/1"
MAX_ENDPOINT = 95
MAX_TEMPLATE_ID = 63
MAX_TOKEN = 191


def configure_serial(fd: int) -> None:
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attributes[3] = 0
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    termios.tcflush(fd, termios.TCIOFLUSH)


def set_payload(endpoint: str, template_id: str, token: str) -> bytes:
    endpoint_bytes = endpoint.encode("ascii")
    template_bytes = template_id.encode("ascii")
    token_bytes = token.encode("ascii")
    if not 0 < len(endpoint_bytes) <= MAX_ENDPOINT:
        raise ValueError("endpoint length is invalid")
    if not 0 < len(template_bytes) <= MAX_TEMPLATE_ID:
        raise ValueError("template ID length is invalid")
    if not 0 < len(token_bytes) <= MAX_TOKEN:
        raise ValueError("token length is invalid")
    return (
        struct.pack(">HHH", len(endpoint_bytes), len(template_bytes), len(token_bytes))
        + endpoint_bytes
        + template_bytes
        + token_bytes
    )


def frame(operation: str, payload: bytes = b"") -> bytes:
    checksum = binascii.crc32(payload) & 0xFFFFFFFF if payload else 0
    header = f"{PROTOCOL} {operation} {len(payload)} {checksum:08X}\n".encode("ascii")
    return header + payload


def exchange(port: str, request: bytes, timeout_seconds: float = 5.0) -> str:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure_serial(fd)
        view = memoryview(request)
        while view:
            written = os.write(fd, view)
            view = view[written:]

        deadline = time.monotonic() + timeout_seconds
        received = bytearray()
        while time.monotonic() < deadline:
            ready, _, _ = select.select([fd], [], [], min(0.25, deadline - time.monotonic()))
            if not ready:
                continue
            chunk = os.read(fd, 512)
            if not chunk:
                continue
            received.extend(chunk)
            for line in received.splitlines(keepends=True):
                if not line.endswith((b"\n", b"\r")):
                    continue
                marker = line.find(PROTOCOL.encode("ascii"))
                if marker >= 0:
                    return line[marker:].rstrip(b"\r\n").decode(
                        "utf-8", errors="replace"
                    )
        raise TimeoutError("no provisioning response received")
    finally:
        os.close(fd)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Set, inspect, or clear the Blynk NVS credential blob over UART0"
    )
    parser.add_argument("--port", required=True, help="confirmed KFB003 USB-to-UART0 device")
    subparsers = parser.add_subparsers(dest="operation", required=True)
    set_parser = subparsers.add_parser("set")
    set_parser.add_argument("--endpoint", required=True, help="direct regional host, e.g. fra1.blynk.cloud")
    set_parser.add_argument("--template-id", required=True)
    subparsers.add_parser("status")
    subparsers.add_parser("clear")
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    try:
        if arguments.operation == "set":
            token = getpass.getpass("Blynk device token (not echoed): ")
            payload = set_payload(arguments.endpoint, arguments.template_id, token)
            request = frame("SET", payload)
            token = ""
            payload = b""
        elif arguments.operation == "status":
            request = frame("STATUS")
        else:
            request = frame("CLEAR")
        response = exchange(arguments.port, request)
    except (OSError, ValueError, TimeoutError, UnicodeError) as error:
        print(f"provisioning failed: {error}", file=sys.stderr)
        return 1

    print(response)
    return 0 if response.startswith(f"{PROTOCOL} OK") else 1


if __name__ == "__main__":
    raise SystemExit(main())
