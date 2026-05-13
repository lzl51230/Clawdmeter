#!/usr/bin/env python3
"""Send manual test usage payloads to the Xingzhi serial meter firmware."""

from __future__ import annotations

import argparse
import json
import sys
import time
from typing import Callable, TextIO


DEFAULT_PORT = "COM7"
DEFAULT_BAUD = 115200

PRESETS = {
    "normal": {
        "s": 42,
        "sr": 37,
        "w": 28,
        "wr": 720,
        "st": "allowed",
        "ok": True,
        "valid": True,
    },
    "high": {
        "s": 88,
        "sr": 12,
        "w": 82,
        "wr": 180,
        "st": "limited",
        "ok": True,
        "valid": True,
    },
    "invalid": {
        "s": 0,
        "sr": -1,
        "w": 0,
        "wr": -1,
        "st": "error",
        "ok": False,
        "valid": False,
    },
}


def build_payload(preset: str) -> str:
    try:
        payload = PRESETS[preset]
    except KeyError as exc:
        raise ValueError(f"Unknown preset: {preset}") from exc
    return json.dumps(payload, separators=(",", ":"))


def write_payload(
    port: str,
    baud: int,
    payload: str,
    serial_factory: Callable | None = None,
    settle_delay: float = 1.2,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> None:
    if serial_factory is None:
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError("pyserial is required. Install it with: py -3 -m pip install pyserial") from exc
        serial_port = serial.Serial()
        serial_port.port = port
        serial_port.baudrate = baud
        serial_port.timeout = 1
        serial_port.dtr = False
        serial_port.rts = False
        serial_port.open()
    else:
        serial_port = serial_factory(port, baud, timeout=1)

    with serial_port:
        serial_port.dtr = False
        serial_port.rts = False
        if settle_delay > 0:
            sleep_fn(settle_delay)
        serial_port.write((payload + "\n").encode("utf-8"))
        serial_port.flush()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Send a preset usage payload to the Xingzhi serial meter.")
    parser.add_argument("preset", choices=sorted(PRESETS.keys()), help="Payload preset to send")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Windows serial port, for example COM7")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    parser.add_argument("--repeat", type=int, default=1, help="Number of times to send the payload")
    parser.add_argument("--interval", type=float, default=0.5, help="Seconds between repeated sends")
    parser.add_argument("--settle-delay", type=float, default=1.2, help="Seconds to wait after opening the port")
    parser.add_argument("--dry-run", action="store_true", help="Print payload without opening the serial port")
    return parser


def run(
    argv: list[str] | None = None,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
    serial_factory: Callable | None = None,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> int:
    args = build_parser().parse_args(argv)
    payload = build_payload(args.preset)
    repeat = max(args.repeat, 1)

    print(f"Preset: {args.preset}", file=stdout)
    print(f"Port: {args.port}", file=stdout)
    print(f"Baud: {args.baud}", file=stdout)
    print(f"Payload: {payload}", file=stdout)

    if args.dry_run:
        return 0

    try:
        for index in range(repeat):
            write_payload(args.port, args.baud, payload, serial_factory, args.settle_delay, sleep_fn)
            if index + 1 < repeat:
                sleep_fn(args.interval)
        print("Payload sent.", file=stdout)
        return 0
    except Exception as exc:
        print(f"Error sending payload to {args.port}: {exc}", file=stderr)
        print("Check the COM port, USB cable, and whether another monitor has the port open.", file=stderr)
        return 1


def main() -> int:
    return run()


if __name__ == "__main__":
    raise SystemExit(main())
