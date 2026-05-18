#!/usr/bin/env python3
"""Shared Windows BLE helpers for Xingzhi tools."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import importlib
import re
import sys
import time
from typing import Any, Callable, TextIO


DEVICE_NAME = "Claude Controller"
SERVICE_UUID = "4c41555a-4465-7669-6365-000000000001"


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
    service_uuid: str = SERVICE_UUID
    address: str | None = None
    scan_timeout: float = 10.0
    pair: bool = False
    use_cached_services: bool = False
    poll_interval: float = 60.0
    ack_timeout: float = 3.0
    write_response: bool = True
    watch: bool = False
    require_ack: bool = False
    max_writes: int | None = None


def log(stdout: TextIO, message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", file=stdout)


def load_bleak():
    try:
        bleak = importlib.import_module("bleak")
        bleak_device = importlib.import_module("bleak.backends.device")
    except ImportError as exc:
        raise MissingBleakError("bleak is required. Install it with: py -3 -m pip install bleak") from exc
    return bleak.BleakScanner, bleak.BleakClient, bleak_device.BLEDevice


async def maybe_await(value: Any) -> Any:
    if asyncio.iscoroutine(value):
        return await value
    return value


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


def _format_ble_address(value: str) -> str:
    return ":".join(value[index : index + 2] for index in range(0, 12, 2)).upper()


def _extract_ble_address_from_device_id(device_id: str) -> str | None:
    matches = re.findall(r"(?:dev_|_)([0-9a-f]{12})(?=[#\\\\])", device_id.lower())
    if not matches:
        return None
    return _format_ble_address(matches[-1])


def _select_paired_windows_address(infos: Any, device_name: str, service_uuid: str) -> str | None:
    service_token = service_uuid.lower()
    fallback: str | None = None
    for info in infos:
        name = str(getattr(info, "name", "") or "")
        device_id = str(getattr(info, "id", "") or "")
        address = _extract_ble_address_from_device_id(device_id)
        if not address:
            continue

        id_lower = device_id.lower()
        name_match = name == device_name
        service_match = service_token in id_lower
        if name_match and service_match:
            return address
        if service_match or (name_match and fallback is None):
            fallback = address
    return fallback


async def resolve_paired_windows_ble_address(device_name: str, service_uuid: str) -> str | None:
    if not sys.platform.startswith("win"):
        return None
    try:
        enumeration = importlib.import_module("winrt.windows.devices.enumeration")
    except ImportError:
        return None

    infos = await enumeration.DeviceInformation.find_all_async()
    return _select_paired_windows_address(infos, device_name, service_uuid)


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


def create_ble_client(client_cls: Any, target: Any, disconnected_callback: Callable[[Any], None], config: BleConfig):
    kwargs: dict[str, Any] = {"disconnected_callback": disconnected_callback}
    if config.pair:
        kwargs["pair"] = True
    if sys.platform.startswith("win"):
        kwargs["winrt"] = {"use_cached_services": config.use_cached_services}
    try:
        return client_cls(target, **kwargs)
    except TypeError:
        return client_cls(target, disconnected_callback=disconnected_callback)


async def find_device(
    scanner_cls: Any,
    config: BleConfig,
    stdout: TextIO,
    device_cls: Any | None = None,
    paired_address_resolver: Callable[[str, str], Any] | None = None,
) -> DeviceChoice:
    if config.address and device_cls:
        log(stdout, f"Using direct BLE address {config.address}; skipping scan")
        return DeviceChoice(
            device_cls(config.address, config.device_name, details=None),
            config.address,
            config.device_name,
            False,
        )

    log(
        stdout,
        f"Scanning for {config.device_name!r} service={config.service_uuid} timeout={config.scan_timeout:.1f}s",
    )
    discovered = await scanner_cls.discover(timeout=config.scan_timeout, return_adv=True)
    choice = select_device(discovered, config.device_name, config.service_uuid, config.address)
    if not choice:
        resolver = paired_address_resolver or resolve_paired_windows_ble_address
        paired_address = await maybe_await(resolver(config.device_name, config.service_uuid)) if device_cls else None
        if paired_address:
            log(stdout, f"Found paired Windows BLE address {paired_address}; connecting directly")
            return DeviceChoice(
                device_cls(paired_address, config.device_name, details=None),
                paired_address,
                config.device_name,
                False,
            )
        raise RuntimeError(f"BLE device not found: {config.device_name}")
    service_note = "service match" if choice.service_match else "name/address match"
    log(stdout, f"Selected {choice.name or '-'} at {choice.address or '-'} ({service_note})")
    return choice
