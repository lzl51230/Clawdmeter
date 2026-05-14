#!/usr/bin/env python3
"""Serial debug helper for the Xingzhi parity firmware."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import sys
import time
from typing import Callable, TextIO


DEFAULT_PORT = "COM7"
DEFAULT_BAUD = 115200
STATUS_COMMAND = b"XDBG STATUS\n"
SCREENSHOT_COMMAND = b"XDBG SCREENSHOT\n"
SCREENSHOT_END = "XDBG SCREENSHOT_END"


@dataclass(frozen=True)
class ScreenshotMetadata:
    width: int
    height: int
    pixel_format: str
    byte_count: int


def parse_key_values(line: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for token in line.strip().split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        values[key] = value
    return values


def parse_status_line(text: str) -> dict[str, str]:
    for line in text.splitlines():
        if line.startswith("XDBG STATUS "):
            return parse_key_values(line)
    raise ValueError("XDBG STATUS line not found")


def parse_screenshot_start(line: str) -> ScreenshotMetadata:
    if not line.startswith("XDBG SCREENSHOT_START "):
        raise ValueError(f"Unexpected screenshot header: {line!r}")
    values = parse_key_values(line)
    try:
        return ScreenshotMetadata(
            width=int(values["width"]),
            height=int(values["height"]),
            pixel_format=values["format"],
            byte_count=int(values["bytes"]),
        )
    except KeyError as exc:
        raise ValueError(f"Missing screenshot metadata field: {exc.args[0]}") from exc


def open_serial(port: str, baud: int, timeout: float, serial_factory: Callable | None = None):
    if serial_factory is None:
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError("pyserial is required. Install it with: py -3 -m pip install pyserial") from exc
        serial_port = serial.Serial()
        serial_port.port = port
        serial_port.baudrate = baud
        serial_port.timeout = timeout
        serial_port.dtr = False
        serial_port.rts = False
        serial_port.open()
        return serial_port
    return serial_factory(port, baud, timeout=timeout)


def configure_serial(serial_port) -> None:
    serial_port.dtr = False
    serial_port.rts = False
    reset = getattr(serial_port, "reset_input_buffer", None)
    if reset:
        reset()


def write_command(serial_port, command: bytes) -> None:
    serial_port.write(command)
    serial_port.flush()


def read_line_text(serial_port) -> str:
    data = serial_port.readline()
    if not data:
        raise TimeoutError("Timed out waiting for serial response")
    return data.decode("utf-8", errors="replace").strip()


def read_status_response(serial_port) -> dict[str, str]:
    while True:
        line = read_line_text(serial_port)
        if line.startswith("XDBG STATUS "):
            return parse_status_line(line)
        if line.startswith("XDBG ERROR "):
            raise RuntimeError(line)


def read_action_response(serial_port) -> dict[str, str]:
    while True:
        line = read_line_text(serial_port)
        if line.startswith("XDBG ACTION "):
            return parse_key_values(line)
        if line.startswith("XDBG ERROR "):
            raise RuntimeError(line)


def read_ble_response(serial_port) -> dict[str, str]:
    while True:
        line = read_line_text(serial_port)
        if line.startswith("XDBG BLE "):
            return parse_key_values(line)
        if line.startswith("XDBG ERROR "):
            raise RuntimeError(line)


def read_exact(serial_port, byte_count: int) -> bytes:
    chunks: list[bytes] = []
    remaining = byte_count
    while remaining > 0:
        chunk = serial_port.read(min(4096, remaining))
        if not chunk:
            received = byte_count - remaining
            raise TimeoutError(f"Timed out reading screenshot: got {received} of {byte_count} bytes")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_screenshot_response(serial_port) -> tuple[ScreenshotMetadata, bytes]:
    metadata: ScreenshotMetadata | None = None
    while metadata is None:
        line = read_line_text(serial_port)
        if line.startswith("XDBG SCREENSHOT_START "):
            metadata = parse_screenshot_start(line)
        elif line.startswith("XDBG ERROR "):
            raise RuntimeError(line)

    if metadata.pixel_format != "RGB565LE":
        raise ValueError(f"Unsupported screenshot format: {metadata.pixel_format}")

    pixels = read_exact(serial_port, metadata.byte_count)
    while True:
        line = read_line_text(serial_port)
        if not line:
            continue
        if line == SCREENSHOT_END:
            break
        if line.startswith("XDBG ERROR "):
            raise RuntimeError(line)
    return metadata, pixels


def rgb565le_to_rgb888(data: bytes) -> bytes:
    if len(data) % 2:
        raise ValueError("RGB565 data length must be even")
    output = bytearray((len(data) // 2) * 3)
    out_index = 0
    for index in range(0, len(data), 2):
        value = data[index] | (data[index + 1] << 8)
        r5 = (value >> 11) & 0x1F
        g6 = (value >> 5) & 0x3F
        b5 = value & 0x1F
        output[out_index] = (r5 * 255) // 31
        output[out_index + 1] = (g6 * 255) // 63
        output[out_index + 2] = (b5 * 255) // 31
        out_index += 3
    return bytes(output)


def write_ppm(path: Path, width: int, height: int, rgb888: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + rgb888)


def write_bmp(path: Path, width: int, height: int, rgb888: bytes) -> None:
    row_stride = ((width * 3 + 3) // 4) * 4
    pixel_bytes = row_stride * height
    file_size = 14 + 40 + pixel_bytes
    header = bytearray()
    header.extend(b"BM")
    header.extend(struct.pack("<IHHI", file_size, 0, 0, 14 + 40))
    header.extend(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, pixel_bytes, 2835, 2835, 0, 0))

    rows = bytearray()
    padding = b"\x00" * (row_stride - width * 3)
    for y in range(height - 1, -1, -1):
        start = y * width * 3
        row = rgb888[start : start + width * 3]
        for index in range(0, len(row), 3):
            rows.extend((row[index + 2], row[index + 1], row[index]))
        rows.extend(padding)
    path.write_bytes(bytes(header) + bytes(rows))


def write_image(path: Path, metadata: ScreenshotMetadata, pixels: bytes) -> None:
    rgb888 = rgb565le_to_rgb888(pixels)
    if path.suffix.lower() == ".ppm":
        write_ppm(path, metadata.width, metadata.height, rgb888)
    else:
        write_bmp(path, metadata.width, metadata.height, rgb888)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Debug a flashed Xingzhi parity device over USB serial.")
    subcommands = parser.add_subparsers(dest="command", required=True)

    status = subcommands.add_parser("status", help="Request a parseable debug status line")
    status.add_argument("--port", default=DEFAULT_PORT, help="Serial port, for example COM7")
    status.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    status.add_argument("--timeout", type=float, default=3.0, help="Serial read timeout in seconds")
    status.add_argument("--settle-delay", type=float, default=0.4, help="Delay after opening the port")

    screenshot = subcommands.add_parser("screenshot", help="Capture the 240x240 RGB565 framebuffer")
    screenshot.add_argument("--port", default=DEFAULT_PORT, help="Serial port, for example COM7")
    screenshot.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    screenshot.add_argument("--timeout", type=float, default=12.0, help="Serial read timeout in seconds")
    screenshot.add_argument("--settle-delay", type=float, default=0.4, help="Delay after opening the port")
    screenshot.add_argument("--output", type=Path, default=Path("xingzhi-screenshot.bmp"), help="Output .bmp or .ppm path")
    screenshot.add_argument("--raw-output", type=Path, help="Optional raw RGB565 output path")

    button = subcommands.add_parser("button", help="Simulate a Xingzhi button action")
    button.add_argument("button", choices=["cycle", "screen", "1", "space", "2", "shift_tab", "shift-tab", "tab", "3"])
    button.add_argument("--event", choices=["click", "press", "release"], default="click", help="Simulated button event")
    button.add_argument("--port", default=DEFAULT_PORT, help="Serial port, for example COM7")
    button.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    button.add_argument("--timeout", type=float, default=3.0, help="Serial read timeout in seconds")
    button.add_argument("--settle-delay", type=float, default=0.4, help="Delay after opening the port")

    ble = subcommands.add_parser("ble", help="Run a BLE recovery command")
    ble.add_argument("action", choices=["reset", "recover", "clear"], help="BLE recovery action")
    ble.add_argument("--port", default=DEFAULT_PORT, help="Serial port, for example COM7")
    ble.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    ble.add_argument("--timeout", type=float, default=3.0, help="Serial read timeout in seconds")
    ble.add_argument("--settle-delay", type=float, default=0.4, help="Delay after opening the port")

    return parser


def run(
    argv: list[str] | None = None,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
    serial_factory: Callable | None = None,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> int:
    args = build_parser().parse_args(argv)
    try:
        with open_serial(args.port, args.baud, args.timeout, serial_factory) as serial_port:
            configure_serial(serial_port)
            if args.settle_delay > 0:
                sleep_fn(args.settle_delay)

            if args.command == "status":
                write_command(serial_port, STATUS_COMMAND)
                status = read_status_response(serial_port)
                for key in sorted(status):
                    print(f"{key}={status[key]}", file=stdout)
                return 0

            if args.command == "button":
                write_command(serial_port, f"XDBG BUTTON {args.button} {args.event}\n".encode("ascii"))
                action = read_action_response(serial_port)
                for key in sorted(action):
                    print(f"{key}={action[key]}", file=stdout)
                return 0

            if args.command == "ble":
                write_command(serial_port, f"XDBG BLE {args.action}\n".encode("ascii"))
                result = read_ble_response(serial_port)
                for key in sorted(result):
                    print(f"{key}={result[key]}", file=stdout)
                return 0

            write_command(serial_port, SCREENSHOT_COMMAND)
            metadata, pixels = read_screenshot_response(serial_port)
            write_image(args.output, metadata, pixels)
            if args.raw_output:
                args.raw_output.write_bytes(pixels)
            print(
                f"Captured {metadata.width}x{metadata.height} {metadata.pixel_format} to {args.output}",
                file=stdout,
            )
            return 0
    except Exception as exc:
        print(f"Error: {exc}", file=stderr)
        return 1


def main() -> int:
    return run()


if __name__ == "__main__":
    raise SystemExit(main())
