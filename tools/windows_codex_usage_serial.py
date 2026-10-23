#!/usr/bin/env python3
"""Send local Codex usage and settings to Xingzhi over USB serial."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time
from typing import Callable, TextIO

from codex_usage import DEFAULT_CODEX_SESSION_FILE_LIMIT, poll_codex_local_usage
from xingzhi_debug import configure_serial, open_serial, read_status_response


class SerialTarget:
    def __init__(self, port: str):
        self.port = port
        self.identity: tuple | None = None

    @staticmethod
    def device_identity(port, field: str) -> tuple:
        value = getattr(port, field, None)
        if field == "device":
            value = value.casefold()
        return (field, getattr(port, "vid", None), getattr(port, "pid", None), value)

    def resolve(self, ports) -> str | None:
        if self.identity is None and self.port == "auto":
            candidates = [port for port in ports
                          if port.device.startswith("/dev/cu.")
                          and (getattr(port, "vid", None), getattr(port, "pid", None))
                          == (0x303A, 0x1001)]
        elif self.identity is None:
            candidates = [port for port in ports if port.device.casefold() == self.port.casefold()]
        else:
            candidates = [port for port in ports
                          if self.device_identity(port, self.identity[0]) == self.identity]
        if len(candidates) > 1:
            names = ", ".join(sorted(port.device for port in candidates))
            raise OSError(f"Multiple serial ports match the device: {names}; "
                          "use --port to select one, or wait for a unique match")
        if not candidates:
            return None
        selected = candidates[0]
        if self.identity is None:
            field = next((field for field in ("serial_number", "location")
                          if getattr(selected, field, None)), "device")
            self.identity = self.device_identity(selected, field)
        self.port = selected.device
        return self.port


def send_snapshot(
    payload: str,
    port: str,
    baud: int,
    timeout: float,
    serial_factory: Callable | None = None,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> dict[str, str]:
    encoded = payload.encode("utf-8")
    if len(encoded) >= 512:
        raise ValueError("Payload exceeds the 511-byte device limit")
    expected = json.loads(payload)
    with open_serial(port, baud, timeout, serial_factory) as connection:
        connection.write_timeout = timeout
        configure_serial(connection)
        sleep_fn(0.4)
        packet = encoded + b"\nXDBG STATUS\n"
        if connection.write(packet) != len(packet):
            raise TimeoutError("Incomplete serial write")
        status = read_status_response(connection)
        if status.get("source") != "serial":
            raise RuntimeError("Device did not accept the serial update")
        for key, value in expected["cfg"].items():
            if status.get(key) != (value or "-"):
                raise RuntimeError(f"Device settings mismatch: {key}")
        if expected.get("ok") and status.get("payload") != "valid":
            raise RuntimeError("Device rejected the usage payload")
        return status


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3",
                        help="Serial port, or auto to discover a macOS ESP32-S3 USB port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--codex-home", type=Path, help="Local Codex home (never auto-selects WSL)")
    parser.add_argument("--codex-max-session-files", type=int, default=DEFAULT_CODEX_SESSION_FILE_LIMIT)
    parser.add_argument("--codex-limit-id", default="codex")
    parser.add_argument("--watch", action="store_true")
    parser.add_argument("--follow-device", action=argparse.BooleanOptionalAction, default=False,
                        help="In watch mode, follow the same USB device when its serial port changes")
    parser.add_argument("--poll-interval", type=float, default=60.0)
    parser.add_argument("--retry-delay", type=float, default=5.0)
    parser.add_argument("--dry-run", action="store_true", help="Print a snapshot without opening the serial port")
    return parser


def run(
    argv: list[str] | None = None,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
    serial_factory: Callable | None = None,
    sleep_fn: Callable[[float], None] = time.sleep,
    payload_provider: Callable[[], str] | None = None,
    port_provider: Callable | None = None,
    monotonic_fn: Callable[[], float] = time.monotonic,
) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if min(args.poll_interval, args.retry_delay, args.timeout) <= 0:
        parser.error("poll interval, retry delay and timeout must be positive")
    if payload_provider is None:
        def payload_provider() -> str:
            return poll_codex_local_usage(
                args.codex_home,
                max_session_files=args.codex_max_session_files,
                limit_id=args.codex_limit_id,
                on_warning=lambda message: print(f"Warning: {message}", file=stderr, flush=True),
            )
    follow_device = args.watch and args.follow_device
    target = SerialTarget(args.port) if (follow_device or args.port == "auto") and not args.dry_run else None
    active_port = args.port
    next_update = 0.0
    try:
        while True:
            try:
                if target is not None:
                    if port_provider is None:
                        from serial.tools import list_ports
                        port_provider = list_ports.comports
                    selected_port = target.resolve(port_provider())
                    if selected_port is None:
                        raise OSError("Device unavailable; waiting for reconnect")
                    if selected_port != active_port:
                        print(f"Serial port changed: {active_port} -> {selected_port}", file=stdout, flush=True)
                        active_port = selected_port
                        next_update = 0.0
                    if not follow_device:
                        target = None
                    remaining = next_update - monotonic_fn()
                    if remaining > 0:
                        sleep_fn(min(args.retry_delay, remaining))
                        continue
                payload = payload_provider()
                print(f"Payload: {payload}", file=stdout, flush=True)
                if args.dry_run:
                    return 0
                status = send_snapshot(payload, active_port, args.baud, args.timeout, serial_factory, sleep_fn)
                print(f"Serial update verified: {active_port}, payload={status.get('payload')}", file=stdout, flush=True)
                if not args.watch:
                    return 0
                next_update = monotonic_fn() + args.poll_interval
                delay = min(args.retry_delay, args.poll_interval) if target is not None else args.poll_interval
            except Exception as exc:
                next_update = 0.0
                print(f"Serial update failed on {active_port}: {exc}", file=stderr, flush=True)
                if not args.watch:
                    return 1
                print(f"Waiting for device; retrying in {args.retry_delay:g}s (Ctrl+C to stop)", file=stderr, flush=True)
                delay = args.retry_delay
            sleep_fn(delay)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(run())
