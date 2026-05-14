#!/usr/bin/env python3
"""Send Claude usage payloads to the Xingzhi firmware over Windows BLE."""

from __future__ import annotations

import argparse
import asyncio
from dataclasses import dataclass
import importlib
import json
from pathlib import Path
import sys
import time
from typing import Any, Callable, Mapping, TextIO
from urllib import request


DEVICE_NAME = "Claude Controller"
SERVICE_UUID = "4c41555a-4465-7669-6365-000000000001"
RX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000002"
TX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000003"
REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000004"
CLAUDE_MESSAGES_URL = "https://api.anthropic.com/v1/messages"
DEFAULT_CREDENTIALS = Path.home() / ".claude" / ".credentials.json"

PRESETS = {
    "normal": {"s": 42, "sr": 37, "w": 28, "wr": 720, "st": "allowed", "ok": True},
    "high": {"s": 88, "sr": 12, "w": 82, "wr": 180, "st": "limited", "ok": True},
    "invalid": {"s": 0, "sr": -1, "w": 0, "wr": -1, "st": "error", "ok": False},
}


class MissingCredentialsError(RuntimeError):
    """Raised when Claude credentials are unavailable."""


class MissingBleakError(RuntimeError):
    """Raised when the Bleak dependency is unavailable."""


@dataclass(frozen=True)
class DeviceChoice:
    device: Any
    address: str
    name: str
    service_match: bool


@dataclass(frozen=True)
class BleConfig:
    device_name: str = DEVICE_NAME
    address: str | None = None
    scan_timeout: float = 10.0
    poll_interval: float = 60.0
    ack_timeout: float = 3.0
    write_response: bool = True
    watch: bool = False
    require_ack: bool = False
    max_writes: int | None = None


class NotificationState:
    def __init__(self) -> None:
        self.acks: asyncio.Queue[str] = asyncio.Queue()
        self.refresh_requested = asyncio.Event()


def log(stdout: TextIO, message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", file=stdout)


def compact_payload(payload: Mapping[str, Any]) -> str:
    return json.dumps(payload, separators=(",", ":"))


def build_preset_payload(preset: str) -> str:
    try:
        return compact_payload(PRESETS[preset])
    except KeyError as exc:
        raise ValueError(f"Unknown preset: {preset}") from exc


def _find_access_token(value: Any) -> str | None:
    if isinstance(value, dict):
        token = value.get("accessToken")
        if isinstance(token, str) and token:
            return token
        for child in value.values():
            found = _find_access_token(child)
            if found:
                return found
    elif isinstance(value, list):
        for child in value:
            found = _find_access_token(child)
            if found:
                return found
    return None


def read_access_token(credentials_path: Path) -> str:
    if not credentials_path.exists():
        raise MissingCredentialsError(f"Claude credentials not found: {credentials_path}")

    try:
        data = json.loads(credentials_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise MissingCredentialsError(f"Claude credentials are not valid JSON: {credentials_path}") from exc

    token = _find_access_token(data)
    if not token:
        raise MissingCredentialsError(f"Claude accessToken not found in {credentials_path}")
    return token


def _get_header(headers: Mapping[str, Any], name: str, default: str = "") -> str:
    lower_name = name.lower()
    getter = getattr(headers, "get", None)
    if getter:
        direct = getter(name)
        if direct is not None:
            return str(direct).strip()
    for key, value in headers.items():
        if str(key).lower() == lower_name:
            return str(value).strip()
    return default


def _usage_percent(value: str) -> int:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0
    if number <= 1.0:
        number *= 100.0
    return max(0, min(999, int(round(number))))


def _reset_minutes(value: str, now: int) -> int:
    try:
        reset_epoch = float(value)
    except (TypeError, ValueError):
        return -1
    remaining = int(round((reset_epoch - now) / 60.0))
    return max(0, remaining)


def build_payload_from_headers(headers: Mapping[str, Any], now: int | None = None) -> str:
    now = int(time.time()) if now is None else now
    payload = {
        "s": _usage_percent(_get_header(headers, "anthropic-ratelimit-unified-5h-utilization", "0")),
        "sr": _reset_minutes(_get_header(headers, "anthropic-ratelimit-unified-5h-reset", "0"), now),
        "w": _usage_percent(_get_header(headers, "anthropic-ratelimit-unified-7d-utilization", "0")),
        "wr": _reset_minutes(_get_header(headers, "anthropic-ratelimit-unified-7d-reset", "0"), now),
        "st": _get_header(headers, "anthropic-ratelimit-unified-5h-status", "unknown") or "unknown",
        "ok": True,
    }
    return compact_payload(payload)


def poll_claude_usage(
    credentials_path: Path = DEFAULT_CREDENTIALS,
    opener: Callable = request.urlopen,
    now_fn: Callable[[], float] = time.time,
    timeout: float = 15.0,
) -> str:
    token = read_access_token(credentials_path)
    body = json.dumps(
        {
            "model": "claude-haiku-4-5-20251001",
            "max_tokens": 1,
            "messages": [{"role": "user", "content": "hi"}],
        },
        separators=(",", ":"),
    ).encode("utf-8")
    req = request.Request(
        CLAUDE_MESSAGES_URL,
        data=body,
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "anthropic-version": "2023-06-01",
            "anthropic-beta": "oauth-2025-04-20",
            "Content-Type": "application/json",
            "User-Agent": "claude-code/2.1.5",
        },
    )
    with opener(req, timeout=timeout) as response:
        return build_payload_from_headers(response.headers, now=int(now_fn()))


def load_bleak():
    try:
        bleak = importlib.import_module("bleak")
    except ImportError as exc:
        raise MissingBleakError("bleak is required. Install it with: py -3 -m pip install bleak") from exc
    return bleak.BleakScanner, bleak.BleakClient


def _iter_discovered(discovered: Any):
    values = discovered.values() if isinstance(discovered, dict) else discovered
    for item in values:
        if isinstance(item, tuple) and len(item) >= 2:
            yield item[0], item[1]
        else:
            yield item, None


def _device_address(device: Any) -> str:
    return str(getattr(device, "address", "") or "")


def _device_name(device: Any, advertisement: Any) -> str:
    advertised = getattr(advertisement, "local_name", None)
    if advertised:
        return str(advertised)
    return str(getattr(device, "name", "") or "")


def _service_uuids(advertisement: Any) -> set[str]:
    values = getattr(advertisement, "service_uuids", None) or []
    return {str(value).lower() for value in values}


def select_device(
    discovered: Any,
    device_name: str = DEVICE_NAME,
    service_uuid: str = SERVICE_UUID,
    address: str | None = None,
) -> DeviceChoice | None:
    choices: list[DeviceChoice] = []
    service_uuid = service_uuid.lower()
    address_lower = address.lower() if address else None

    for device, advertisement in _iter_discovered(discovered):
        found_address = _device_address(device)
        found_name = _device_name(device, advertisement)
        services = _service_uuids(advertisement)
        service_match = service_uuid in services
        name_match = found_name == device_name
        address_match = bool(address_lower and found_address.lower() == address_lower)

        if address_match or name_match or service_match:
            choices.append(DeviceChoice(device, found_address, found_name, service_match))

    if not choices:
        return None
    if address_lower:
        for choice in choices:
            if choice.address.lower() == address_lower:
                return choice
    service_matches = [choice for choice in choices if choice.service_match]
    if service_matches:
        return service_matches[0]
    for choice in choices:
        if choice.name == device_name:
            return choice
    return choices[0]


async def find_device(scanner_cls: Any, config: BleConfig, stdout: TextIO) -> DeviceChoice:
    log(stdout, f"Scanning for {config.device_name!r} service={SERVICE_UUID} timeout={config.scan_timeout:.1f}s")
    discovered = await scanner_cls.discover(timeout=config.scan_timeout, return_adv=True)
    choice = select_device(discovered, config.device_name, SERVICE_UUID, config.address)
    if not choice:
        raise RuntimeError(f"BLE device not found: {config.device_name}")
    service_note = "service match" if choice.service_match else "name/address match"
    log(stdout, f"Selected {choice.name or '-'} at {choice.address or '-'} ({service_note})")
    return choice


def _decode_notification(data: bytes | bytearray | memoryview) -> str:
    return bytes(data).decode("utf-8", errors="replace")


async def start_notifications(client: Any, state: NotificationState, stdout: TextIO) -> None:
    def tx_handler(sender: Any, data: bytes | bytearray | memoryview) -> None:
        text = _decode_notification(data)
        log(stdout, f"TX notify: {text}")
        if '"ack":true' in text or '"ack": true' in text:
            state.acks.put_nowait("ack")
        elif '"err":true' in text or '"err": true' in text:
            state.acks.put_nowait("nack")

    def req_handler(sender: Any, data: bytes | bytearray | memoryview) -> None:
        log(stdout, f"REQ notify: {bytes(data).hex() or '-'}")
        state.refresh_requested.set()

    await client.start_notify(TX_CHAR_UUID, tx_handler)
    log(stdout, "Subscribed to TX notifications")
    await client.start_notify(REQ_CHAR_UUID, req_handler)
    log(stdout, "Subscribed to REQ notifications")


async def maybe_await(value: Any) -> Any:
    if asyncio.iscoroutine(value):
        return await value
    return value


async def write_payload(client: Any, payload: str, config: BleConfig, state: NotificationState, stdout: TextIO) -> bool:
    data = payload.encode("utf-8")
    log(stdout, f"Writing RX payload: {payload}")
    await client.write_gatt_char(RX_CHAR_UUID, data, response=config.write_response)
    log(stdout, "BLE write succeeded")

    try:
        result = await asyncio.wait_for(state.acks.get(), timeout=config.ack_timeout)
    except asyncio.TimeoutError:
        log(stdout, "TX ack timeout")
        return not config.require_ack

    if result == "ack":
        log(stdout, "Device acknowledged payload")
        return True
    log(stdout, "Device rejected payload")
    return False


async def run_ble_session(
    config: BleConfig,
    payload_provider: Callable[[], str],
    stdout: TextIO,
    scanner_cls: Any,
    client_cls: Any,
) -> bool:
    choice = await find_device(scanner_cls, config, stdout)
    target = choice.device if not config.address else config.address
    state = NotificationState()

    def disconnected_callback(client: Any) -> None:
        log(stdout, "BLE disconnected")

    log(stdout, f"Connecting to {choice.address or config.address or choice.name}")
    async with client_cls(target, disconnected_callback=disconnected_callback) as client:
        log(stdout, "Connected")
        log(stdout, "Using RX/TX/REQ characteristics")
        await start_notifications(client, state, stdout)

        writes = 0
        while True:
            payload = await maybe_await(payload_provider())
            ok = await write_payload(client, payload, config, state, stdout)
            writes += 1
            if not ok:
                return False
            if not config.watch:
                return True
            if config.max_writes and writes >= config.max_writes:
                return True

            try:
                await asyncio.wait_for(state.refresh_requested.wait(), timeout=config.poll_interval)
                state.refresh_requested.clear()
                log(stdout, "Refresh requested by device; polling immediately")
            except asyncio.TimeoutError:
                log(stdout, "Poll interval elapsed; polling again")


async def run_ble_with_retries(
    config: BleConfig,
    payload_provider: Callable[[], str],
    stdout: TextIO,
    scanner_cls: Any,
    client_cls: Any,
    retry_delay: float,
) -> int:
    while True:
        try:
            ok = await run_ble_session(config, payload_provider, stdout, scanner_cls, client_cls)
            if ok:
                return 0
            if not config.watch:
                return 1
            log(stdout, f"BLE send failed; retrying in {retry_delay:.1f}s")
        except Exception as exc:
            log(stdout, f"BLE error: {exc}")
            if not config.watch:
                return 1
            log(stdout, f"Retrying in {retry_delay:.1f}s")
        await asyncio.sleep(retry_delay)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Send Claude usage to the Xingzhi BLE GATT RX characteristic.")
    parser.add_argument("--device-name", default=DEVICE_NAME, help="Advertised BLE device name")
    parser.add_argument("--address", help="Optional BLE address to connect to instead of name/service selection")
    parser.add_argument("--scan-timeout", type=float, default=10.0, help="Seconds to scan for the device")
    parser.add_argument("--poll-interval", type=float, default=60.0, help="Seconds between Claude usage polls in --watch mode")
    parser.add_argument("--retry-delay", type=float, default=5.0, help="Seconds before reconnect retry in --watch mode")
    parser.add_argument("--ack-timeout", type=float, default=3.0, help="Seconds to wait for TX ack/nack notification")
    parser.add_argument("--require-ack", action="store_true", help="Treat missing TX ack as a failed send")
    parser.add_argument("--watch", action="store_true", help="Keep running and poll repeatedly")
    parser.add_argument("--test-preset", choices=sorted(PRESETS), help="Send a fixed payload instead of polling Claude")
    parser.add_argument("--credentials", type=Path, default=DEFAULT_CREDENTIALS, help="Claude credentials JSON path")
    parser.add_argument("--dry-run", action="store_true", help="Print the payload without scanning or connecting")
    return parser


def run(
    argv: list[str] | None = None,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
    opener: Callable = request.urlopen,
    now_fn: Callable[[], float] = time.time,
    bleak_loader: Callable = load_bleak,
) -> int:
    args = build_parser().parse_args(argv)

    def payload_provider() -> str:
        if args.test_preset:
            return build_preset_payload(args.test_preset)
        return poll_claude_usage(args.credentials, opener=opener, now_fn=now_fn)

    try:
        first_payload = payload_provider()
    except Exception as exc:
        print(f"Error: {exc}", file=stderr)
        return 1

    if args.dry_run:
        print(f"Payload: {first_payload}", file=stdout)
        return 0

    try:
        scanner_cls, client_cls = bleak_loader()
    except MissingBleakError as exc:
        print(f"Error: {exc}", file=stderr)
        return 1

    config = BleConfig(
        device_name=args.device_name,
        address=args.address,
        scan_timeout=args.scan_timeout,
        poll_interval=args.poll_interval,
        ack_timeout=args.ack_timeout,
        watch=args.watch,
        require_ack=args.require_ack,
    )

    def cached_payload_provider() -> str:
        nonlocal first_payload
        if first_payload is not None:
            payload = first_payload
            first_payload = None
            return payload
        return payload_provider()

    return asyncio.run(run_ble_with_retries(config, cached_payload_provider, stdout, scanner_cls, client_cls, args.retry_delay))


def main() -> int:
    return run()


if __name__ == "__main__":
    raise SystemExit(main())
