#!/usr/bin/env python3
"""Send Windows-local Codex usage and settings to Xingzhi over USB serial."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time
from typing import Callable, TextIO

from codex_usage import DEFAULT_CODEX_SESSION_FILE_LIMIT, poll_codex_local_usage
from xingzhi_debug import configure_serial, open_serial, read_status_response


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
        configure_serial(connection)
        sleep_fn(0.4)
        connection.write(encoded + b"\nXDBG STATUS\n")
        connection.flush()
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
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--codex-home", type=Path, help="Windows-local Codex home (never auto-selects WSL)")
    parser.add_argument("--codex-max-session-files", type=int, default=DEFAULT_CODEX_SESSION_FILE_LIMIT)
    parser.add_argument("--codex-limit-id", default="codex")
    parser.add_argument("--watch", action="store_true")
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
    try:
        while True:
            try:
                payload = payload_provider()
                print(f"Payload: {payload}", file=stdout, flush=True)
                if args.dry_run:
                    return 0
                status = send_snapshot(payload, args.port, args.baud, args.timeout, serial_factory, sleep_fn)
                print(f"Serial update verified: {args.port}, payload={status.get('payload')}", file=stdout, flush=True)
                if not args.watch:
                    return 0
                delay = args.poll_interval
            except Exception as exc:
                print(f"Serial update failed on {args.port}: {exc}", file=stderr, flush=True)
                if not args.watch:
                    return 1
                delay = args.retry_delay
            sleep_fn(delay)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(run())
