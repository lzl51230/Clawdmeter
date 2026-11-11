"""Codex usage collection shared by serial and BLE transports."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Any, BinaryIO, Callable, Iterator, Mapping

DEFAULT_CODEX_SESSION_FILE_LIMIT = 200
DEFAULT_CODEX_LIMIT_ID = "codex"
CODEX_READ_BLOCK_BYTES = 64 * 1024
CODEX_MAX_LINE_BYTES = 1024 * 1024


class MissingCodexUsageError(RuntimeError):
    """No local Codex rate-limit event is available."""


def compact_payload(payload: Mapping[str, Any]) -> str:
    return json.dumps(payload, separators=(",", ":"))


def _usage_percent(value: str) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0
    return max(0, min(100, number))


def _reset_minutes(value: str, now: int) -> int:
    try:
        reset_epoch = float(value)
    except (TypeError, ValueError):
        return -1
    remaining = int(round((reset_epoch - now) / 60.0))
    return max(0, remaining)


def _number(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _codex_limit_reset_minutes(limit: Mapping[str, Any], now: int) -> int:
    reset_at = limit.get("resets_at", limit.get("reset_at"))
    if reset_at is not None:
        return _reset_minutes(str(reset_at), now)

    reset_after_seconds = limit.get("reset_after_seconds")
    if reset_after_seconds is None:
        return -1
    return max(0, int(round(_number(reset_after_seconds) / 60.0)))


def _codex_status(rate_limits: Mapping[str, Any], session_percent: float, weekly_percent: float) -> str:
    if rate_limits.get("rate_limit_reached_type") or rate_limits.get("limit_reached") is True:
        return "limited"
    if rate_limits.get("allowed") is False:
        return "limited"
    if session_percent >= 100 or weekly_percent >= 100:
        return "limited"
    return "allowed"


def build_payload_from_codex_rate_limits(rate_limits: Mapping[str, Any], now: int | None = None) -> str:
    now = int(time.time()) if now is None else now
    primary = rate_limits.get("primary") if isinstance(rate_limits.get("primary"), Mapping) else {}
    secondary = rate_limits.get("secondary") if isinstance(rate_limits.get("secondary"), Mapping) else {}
    session, weekly = primary, secondary
    if any(window.get("window_minutes") is not None for window in (primary, secondary)):
        session = next((w for w in (primary, secondary) if w.get("window_minutes") == 300), {})
        weekly = next((w for w in (primary, secondary) if w.get("window_minutes") == 10080), {})
    session_percent = _usage_percent(str(session["used_percent"])) if "used_percent" in session else None
    weekly_percent = _usage_percent(str(weekly["used_percent"])) if "used_percent" in weekly else None
    payload = {
        "s": session_percent,
        "sr": _codex_limit_reset_minutes(session, now),
        "w": weekly_percent,
        "wr": _codex_limit_reset_minutes(weekly, now),
        "st": _codex_status(rate_limits, session_percent or 0, weekly_percent or 0),
        "src": "codex",
        "ok": True,
    }
    return compact_payload(payload)


def resolve_default_codex_home(wsl_distro: str | None = None) -> Path:
    configured = os.environ.get("CODEX_HOME")
    if configured:
        return Path(configured).expanduser()

    if sys.platform.startswith("win"):
        command = 'wslpath -w "${CODEX_HOME:-$HOME/.codex}"'
        args = ["wsl.exe"]
        if wsl_distro:
            args.extend(["-d", wsl_distro])
        args.extend(["sh", "-lc", command])
        try:
            output = subprocess.check_output(args, stderr=subprocess.DEVNULL, text=True, timeout=5).strip()
        except (FileNotFoundError, subprocess.SubprocessError):
            output = ""
        if output:
            return Path(output)

    return Path.home() / ".codex"


def _codex_session_files(codex_home: Path, max_files: int) -> list[Path]:
    sessions_dir = codex_home / "sessions"
    if not sessions_dir.exists():
        raise MissingCodexUsageError(f"Codex sessions directory not found: {sessions_dir}")

    files = []
    for path in sessions_dir.rglob("*.jsonl"):
        try:
            files.append((path.stat().st_mtime_ns, path))
        except OSError:
            continue
    files.sort(key=lambda entry: entry[0], reverse=True)
    return [path for _, path in files[:max(1, max_files)]]


def _iter_codex_lines_reverse(log_file: BinaryIO) -> Iterator[bytes]:
    """从打开时的文件尾部反向读取，缓冲大小不随文件或超长行增长。"""
    position = os.fstat(log_file.fileno()).st_size
    pending = b""
    oversized = False
    while position:
        size = min(position, CODEX_READ_BLOCK_BYTES)
        position -= size
        log_file.seek(position)
        block = log_file.read(size)
        if len(block) != size:
            raise OSError("Codex session was truncated while reading")
        parts = block.split(b"\n")
        for index in range(len(parts) - 1, -1, -1):
            part = parts[index]
            if not oversized:
                if len(part) + len(pending) > CODEX_MAX_LINE_BYTES:
                    pending = b""
                    oversized = True
                else:
                    pending = part + pending
            if index:
                if not oversized and pending:
                    yield pending
                pending = b""
                oversized = False
    if not oversized and pending:
        yield pending


def _codex_rate_limits_from_event(event: Mapping[str, Any]) -> Mapping[str, Any] | None:
    payload = event.get("payload")
    if not isinstance(payload, Mapping) or payload.get("type") != "token_count":
        return None

    rate_limits = payload.get("rate_limits") or event.get("rate_limits")
    if isinstance(rate_limits, Mapping) and rate_limits:
        return rate_limits
    return None


def read_latest_codex_rate_limits(
    codex_home: Path,
    max_files: int = DEFAULT_CODEX_SESSION_FILE_LIMIT,
    limit_id: str | None = DEFAULT_CODEX_LIMIT_ID,
) -> Mapping[str, Any]:
    # 最近活跃会话优先；单个会话内按追加顺序选择最后一条匹配记录。
    match_any_limit = not limit_id or limit_id == "all"
    for path in _codex_session_files(codex_home, max_files):
        fallback_rate_limits = None
        try:
            with path.open("rb") as log_file:
                for line in _iter_codex_lines_reverse(log_file):
                    try:
                        event = json.loads(line)
                    except (ValueError, UnicodeDecodeError, RecursionError):
                        continue
                    if not isinstance(event, Mapping):
                        continue
                    rate_limits = _codex_rate_limits_from_event(event)
                    if not rate_limits:
                        continue
                    event_limit_id = rate_limits.get("limit_id")
                    if match_any_limit or str(event_limit_id) == limit_id:
                        return rate_limits
                    if event_limit_id is None and fallback_rate_limits is None:
                        fallback_rate_limits = rate_limits
        except OSError:
            continue
        if fallback_rate_limits:
            return fallback_rate_limits

    limit_hint = "" if match_any_limit else f" for limit_id={limit_id}"
    raise MissingCodexUsageError(f"Codex token_count rate limits{limit_hint} not found under: {codex_home / 'sessions'}")


def poll_codex_wsl_usage(
    codex_home: Path | None = None,
    now_fn: Callable[[], float] = time.time,
    max_session_files: int = DEFAULT_CODEX_SESSION_FILE_LIMIT,
    wsl_distro: str | None = None,
    limit_id: str | None = DEFAULT_CODEX_LIMIT_ID,
) -> str:
    resolved_home = codex_home or resolve_default_codex_home(wsl_distro)
    rate_limits = read_latest_codex_rate_limits(resolved_home, max_files=max_session_files, limit_id=limit_id)
    return build_payload_from_codex_rate_limits(rate_limits, now=int(now_fn()))


def resolve_local_codex_home(codex_home: Path | None = None) -> Path:
    if codex_home is not None:
        return codex_home.expanduser()
    return Path(os.environ.get("CODEX_HOME") or Path.home() / ".codex").expanduser()


def _display_token(value: Any, limit: int = 63) -> str:
    if not isinstance(value, str):
        return ""
    # Model identifiers are ASCII tokens; keep the serial status line parseable.
    if not value or any(ord(c) < 33 or ord(c) > 126 for c in value):
        return ""
    return value[:limit]


def read_codex_settings(codex_home: Path) -> dict[str, str]:
    import tomllib

    with (codex_home / "config.toml").open("rb") as config_file:
        config = tomllib.load(config_file)
    profile = config.get("profile")
    profiles = config.get("profiles", {})
    if isinstance(profile, str) and isinstance(profiles, dict) and isinstance(profiles.get(profile), dict):
        config.update(profiles[profile])
    model = _display_token(config.get("model"))
    # Codex's Plan preset inherits the model; plan_mode_reasoning_effort is not a model.
    return {
        "model": model,
        "plan_model": model,
        "reasoning_effort": _display_token(config.get("model_reasoning_effort"), 15),
        "plan_reasoning_effort": _display_token(config.get("plan_mode_reasoning_effort"), 15),
        "service_tier": _display_token(config.get("service_tier", "default"), 15),
    }


def poll_codex_local_usage(
    codex_home: Path | None = None,
    now_fn: Callable[[], float] = time.time,
    max_session_files: int = DEFAULT_CODEX_SESSION_FILE_LIMIT,
    limit_id: str | None = DEFAULT_CODEX_LIMIT_ID,
    on_warning: Callable[[str], None] | None = None,
) -> str:
    home = resolve_local_codex_home(codex_home)
    payload: dict[str, Any] = {"src": "codex"}
    try:
        rate_limits = read_latest_codex_rate_limits(home, max_session_files, limit_id)
        payload.update(json.loads(build_payload_from_codex_rate_limits(rate_limits, now=int(now_fn()))))
    except (MissingCodexUsageError, OSError) as exc:
        if on_warning:
            on_warning(str(exc))
    try:
        payload["cfg"] = read_codex_settings(home)
    except (OSError, ValueError):
        payload["cfg"] = {
            "model": "", "plan_model": "", "service_tier": "",
            "reasoning_effort": "", "plan_reasoning_effort": "",
        }
        if on_warning:
            on_warning("Codex config is missing or invalid; displaying unknown settings")
    return compact_payload(payload)
