#!/usr/bin/env python3
"""Run Xingzhi usage updates and voice dictation over one BLE connection."""

from __future__ import annotations

import argparse
import asyncio
from pathlib import Path
import sys
from typing import Any, Callable, Mapping, TextIO
from urllib import request

TOOL_DIR = Path(__file__).resolve().parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

import windows_claude_usage_ble as usage_tool  # noqa: E402
import windows_xingzhi_voice_dictation as voice_tool  # noqa: E402
from xingzhi_ble_common import (  # noqa: E402
    DEVICE_NAME,
    SERVICE_UUID,
    BleConfig,
    MissingBleakError,
    create_ble_client,
    find_device,
    load_bleak,
    log,
    maybe_await,
)


DEFAULT_VOICE_OUTPUT = Path("xingzhi-voice.wav")


class CombinedVoiceState:
    def __init__(self, stdout: TextIO) -> None:
        self.stdout = stdout
        self.active_receiver: voice_tool.VoiceReceiver | None = None

    def start_receiver(self) -> voice_tool.VoiceReceiver:
        receiver = voice_tool.VoiceReceiver(self.stdout)
        self.active_receiver = receiver
        return receiver

    def clear_receiver(self, receiver: voice_tool.VoiceReceiver) -> None:
        if self.active_receiver is receiver:
            self.active_receiver = None

    def handle_notification(self, sender: Any, data: bytes | bytearray | memoryview) -> None:
        if self.active_receiver is None:
            log(self.stdout, "Ignoring voice frame without active receiver")
            return
        self.active_receiver.handle_notification(sender, data)

    def fail_active(self, message: str) -> None:
        if self.active_receiver is not None:
            self.active_receiver.fail(message)


async def _wait_for_refresh_or_timeout(
    state: usage_tool.NotificationState,
    stop_event: asyncio.Event,
    poll_interval: float,
    stdout: TextIO,
) -> bool:
    refresh_task = asyncio.create_task(state.refresh_requested.wait())
    stop_task = asyncio.create_task(stop_event.wait())
    done, pending = await asyncio.wait(
        {refresh_task, stop_task},
        timeout=poll_interval,
        return_when=asyncio.FIRST_COMPLETED,
    )
    for task in pending:
        task.cancel()
    if pending:
        await asyncio.gather(*pending, return_exceptions=True)

    if stop_task in done or stop_event.is_set():
        return False
    if refresh_task in done and refresh_task.result():
        state.refresh_requested.clear()
        log(stdout, "Refresh requested by device; polling immediately")
        return True
    log(stdout, "Poll interval elapsed; polling again")
    return True


async def _write_payload_locked(
    client: Any,
    payload: str,
    config: BleConfig,
    state: usage_tool.NotificationState,
    stdout: TextIO,
    write_lock: asyncio.Lock,
) -> bool:
    async with write_lock:
        return await usage_tool.write_payload(client, payload, config, state, stdout)


async def _write_voice_control_locked(
    client: Any,
    message: str,
    config: BleConfig,
    stdout: TextIO,
    write_lock: asyncio.Lock,
) -> bool:
    try:
        async with write_lock:
            await voice_tool.write_voice_control(client, message, response=config.write_response)
        return True
    except Exception as exc:
        log(stdout, f"Voice control write failed: {exc}")
        return False


async def usage_loop(
    client: Any,
    config: BleConfig,
    state: usage_tool.NotificationState,
    payload_provider: Callable[[], str],
    stdout: TextIO,
    stop_event: asyncio.Event,
    write_lock: asyncio.Lock,
) -> bool:
    writes = 0
    while not stop_event.is_set():
        payload = await maybe_await(payload_provider())
        ok = await _write_payload_locked(client, payload, config, state, stdout, write_lock)
        writes += 1
        if not ok:
            return False
        if config.max_writes and writes >= config.max_writes:
            return True
        if not await _wait_for_refresh_or_timeout(state, stop_event, config.poll_interval, stdout):
            return False
    return False


async def voice_loop(
    client: Any,
    config: BleConfig,
    output: Path,
    receive_timeout: float | None,
    stdout: TextIO,
    stop_event: asyncio.Event,
    write_lock: asyncio.Lock,
    voice_state: CombinedVoiceState,
    max_uploads: int | None = None,
    post_receive: Callable[[bytes, Path], str | None] | None = None,
) -> bool:
    uploads = 0
    while not stop_event.is_set():
        receiver = voice_state.start_receiver()
        log(stdout, "Waiting for GPIO40 voice upload")
        try:
            audio = await receiver.wait(receive_timeout)
        except asyncio.TimeoutError:
            log(stdout, "Voice receive failed: voice_receive_timeout")
            voice_state.clear_receiver(receiver)
            continue
        except Exception as exc:
            message = str(exc) or "voice_receive_error"
            log(stdout, f"Voice receive failed: {message}")
            voice_state.clear_receiver(receiver)
            if message == "voice_ble_disconnected" or stop_event.is_set():
                return False
            await _write_voice_control_locked(
                client,
                voice_tool.voice_control_error(message),
                config,
                stdout,
                write_lock,
            )
            uploads += 1
            if max_uploads and uploads >= max_uploads:
                return True
            continue

        voice_state.clear_receiver(receiver)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(audio)
        log(stdout, f"Saved WAV: {output}")
        try:
            status_message = post_receive(audio, output) if post_receive else "done"
        except Exception as exc:
            message = str(exc) or "post_receive_error"
            log(stdout, f"Voice post-receive failed: {message}")
            await _write_voice_control_locked(
                client,
                voice_tool.voice_control_error(message),
                config,
                stdout,
                write_lock,
            )
            uploads += 1
            if max_uploads and uploads >= max_uploads:
                return True
            continue

        ok = await _write_voice_control_locked(client, status_message or "done", config, stdout, write_lock)
        if not ok:
            return False
        uploads += 1
        if max_uploads and uploads >= max_uploads:
            return True
    return False


async def run_combined_session(
    config: BleConfig,
    payload_provider: Callable[[], str],
    voice_output: Path,
    receive_timeout: float | None,
    stdout: TextIO,
    scanner_cls: Any,
    client_cls: Any,
    device_cls: Any | None = None,
    paired_address_resolver: Callable[[str, str], Any] | None = None,
    post_receive: Callable[[bytes, Path], str | None] | None = None,
    max_voice_uploads: int | None = None,
) -> bool:
    choice = await find_device(scanner_cls, config, stdout, device_cls, paired_address_resolver)
    stop_event = asyncio.Event()
    write_lock = asyncio.Lock()
    usage_state = usage_tool.NotificationState()
    voice_state = CombinedVoiceState(stdout)
    loop = asyncio.get_running_loop()

    def disconnected_callback(client: Any) -> None:
        log(stdout, "BLE disconnected")
        loop.call_soon_threadsafe(stop_event.set)
        loop.call_soon_threadsafe(voice_state.fail_active, "voice_ble_disconnected")

    log(stdout, f"Connecting to {choice.address or config.address or choice.name}")
    async with create_ble_client(client_cls, choice.device, disconnected_callback, config) as client:
        log(stdout, "Connected")
        log(stdout, "Using RX/TX/REQ/VOICE characteristics")
        await usage_tool.start_notifications(client, usage_state, stdout)
        await client.start_notify(voice_tool.VOICE_CHAR_UUID, voice_state.handle_notification)
        log(stdout, "Subscribed to voice notifications")

        tasks = [
            asyncio.create_task(
                usage_loop(client, config, usage_state, payload_provider, stdout, stop_event, write_lock)
            ),
            asyncio.create_task(
                voice_loop(
                    client,
                    config,
                    voice_output,
                    receive_timeout,
                    stdout,
                    stop_event,
                    write_lock,
                    voice_state,
                    max_uploads=max_voice_uploads,
                    post_receive=post_receive,
                )
            ),
        ]
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        stop_event.set()
        try:
            ok = True
            for task in done:
                ok = ok and bool(task.result())
            return ok
        finally:
            for task in pending:
                task.cancel()
            if pending:
                await asyncio.gather(*pending, return_exceptions=True)


async def run_combined_with_retries(
    config: BleConfig,
    payload_provider: Callable[[], str],
    voice_output: Path,
    receive_timeout: float | None,
    stdout: TextIO,
    scanner_cls: Any,
    client_cls: Any,
    retry_delay: float,
    device_cls: Any | None = None,
    paired_address_resolver: Callable[[str, str], Any] | None = None,
    post_receive: Callable[[bytes, Path], str | None] | None = None,
    max_voice_uploads: int | None = None,
) -> int:
    while True:
        try:
            ok = await run_combined_session(
                config,
                payload_provider,
                voice_output,
                receive_timeout,
                stdout,
                scanner_cls,
                client_cls,
                device_cls,
                paired_address_resolver,
                post_receive,
                max_voice_uploads,
            )
            if ok:
                return 0
            log(stdout, f"Combined BLE session failed; retrying in {retry_delay:.1f}s")
        except Exception as exc:
            log(stdout, f"Combined BLE error: {exc}")
            log(stdout, f"Retrying in {retry_delay:.1f}s")
        await asyncio.sleep(retry_delay)


def build_payload_provider(args: argparse.Namespace, opener: Callable, now_fn: Callable[[], float]) -> Callable[[], str]:
    def payload_provider() -> str:
        if args.test_preset:
            return usage_tool.build_preset_payload(args.test_preset)
        if args.usage_source == "claude":
            return usage_tool.poll_claude_usage(args.credentials, opener=opener, now_fn=now_fn)
        if args.usage_source == "codex-wsl":
            return usage_tool.poll_codex_wsl_usage(
                args.codex_home.expanduser() if args.codex_home else None,
                now_fn=now_fn,
                max_session_files=args.codex_max_session_files,
                wsl_distro=args.codex_wsl_distro,
                limit_id=args.codex_limit_id,
            )
        raise ValueError(f"Unknown usage source: {args.usage_source}")

    return payload_provider


def build_voice_post_receive(
    args: argparse.Namespace,
    stdout: TextIO,
    transcriber_factory: Callable[..., voice_tool.SiliconFlowTranscriber],
    paste_adapter_factory: Callable[..., voice_tool.WindowsPasteAdapter],
    opener: Callable,
    environ: Mapping[str, str] | None,
) -> Callable[[bytes, Path], str | None] | None:
    asr_only = args.asr_only or args.no_paste
    if args.dump_only:
        return None

    api_key = voice_tool.read_siliconflow_api_key(args.env_file, environ)

    paste_adapter = None
    if not asr_only:
        paste_adapter = paste_adapter_factory(
            restore_clipboard=not args.no_restore_clipboard,
            paste_delay=args.paste_delay,
        )

    def transcribe_received_audio(audio: bytes, output_path: Path) -> str:
        transcriber = transcriber_factory(
            api_key=api_key,
            model=voice_tool.DEFAULT_TRANSCRIBE_MODEL,
            timeout=args.asr_timeout,
            opener=opener,
        )
        try:
            transcript = transcriber.transcribe(audio, filename=output_path.name)
        except Exception as exc:
            raise RuntimeError(voice_tool.sanitize_error(str(exc), [api_key])) from None
        if paste_adapter is not None:
            try:
                paste_adapter.paste_text(transcript)
            except Exception as exc:
                raise RuntimeError(voice_tool.sanitize_error(f"paste_failed:{exc}", [api_key])) from None
            print("Pasted transcript", file=stdout)
        else:
            print(f"Transcript: {transcript}", file=stdout)
        return "done"

    return transcribe_received_audio


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run Xingzhi usage updates and voice dictation over one BLE link.")
    parser.add_argument("--device-name", default=DEVICE_NAME, help="Advertised BLE device name")
    parser.add_argument("--address", help="Optional BLE address to connect to instead of name/service selection")
    parser.add_argument("--scan-timeout", type=float, default=10.0, help="Seconds to scan for the device")
    parser.add_argument("--pair", action="store_true", help="Ask Bleak/Windows to pair while connecting")
    parser.add_argument("--retry-delay", type=float, default=5.0, help="Seconds before reconnect retry")
    parser.add_argument("--ack-timeout", type=float, default=3.0, help="Seconds to wait for TX ack/nack notification")
    parser.add_argument("--require-ack", action="store_true", help="Treat missing TX ack as a failed send")
    parser.add_argument("--poll-interval", type=float, default=60.0, help="Seconds between usage polls")
    parser.add_argument(
        "--usage-source",
        choices=["claude", "codex-wsl"],
        default="codex-wsl",
        help="Usage data source when --test-preset is not set",
    )
    parser.add_argument("--test-preset", choices=sorted(usage_tool.PRESETS), help="Send a fixed usage payload")
    parser.add_argument("--credentials", type=Path, default=usage_tool.DEFAULT_CREDENTIALS, help="Claude credentials JSON path")
    parser.add_argument("--codex-home", type=Path, help="Codex home path for --usage-source codex-wsl")
    parser.add_argument("--codex-wsl-distro", help="WSL distro to query when auto-detecting Codex home on Windows")
    parser.add_argument(
        "--codex-limit-id",
        default=usage_tool.DEFAULT_CODEX_LIMIT_ID,
        help="Codex rate limit id to read from token_count events; use 'all' to accept the newest limit",
    )
    parser.add_argument(
        "--codex-max-session-files",
        type=int,
        default=usage_tool.DEFAULT_CODEX_SESSION_FILE_LIMIT,
        help="Maximum recent Codex session JSONL files to scan",
    )
    parser.add_argument("--voice-output", type=Path, default=DEFAULT_VOICE_OUTPUT, help="Output WAV path")
    parser.add_argument("--receive-timeout", type=float, default=0.0, help="Seconds to wait for one voice upload; 0 waits forever")
    parser.add_argument("--dump-only", action="store_true", help="Receive BLE voice audio and save WAV without ASR")
    parser.add_argument("--asr-only", action="store_true", help="Transcribe audio and print text without pasting")
    parser.add_argument("--no-paste", action="store_true", help="Alias for --asr-only")
    parser.add_argument("--env-file", type=Path, default=voice_tool.DEFAULT_ENV_FILE, help="Local .env file containing SILICONFLOW_API_KEY")
    parser.add_argument("--asr-timeout", type=float, default=60.0, help="Seconds to wait for SiliconFlow transcription")
    parser.add_argument("--no-restore-clipboard", action="store_true", help="Do not restore previous clipboard text after pasting")
    parser.add_argument("--paste-delay", type=float, default=0.08, help="Seconds to wait before clipboard restoration")
    parser.add_argument("--write-without-response", action="store_true", help="Write host status without BLE response")
    parser.add_argument("--max-usage-writes", type=int, help="Stop after N usage writes; intended for tests")
    parser.add_argument("--max-voice-uploads", type=int, help="Stop after N voice uploads; intended for tests")
    parser.add_argument("--dry-run", action="store_true", help="Print the usage payload without scanning or connecting")
    return parser


def run(
    argv: list[str] | None = None,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
    opener: Callable = request.urlopen,
    now_fn: Callable[[], float] = usage_tool.time.time,
    bleak_loader: Callable = load_bleak,
    transcriber_factory: Callable[..., voice_tool.SiliconFlowTranscriber] = voice_tool.SiliconFlowTranscriber,
    paste_adapter_factory: Callable[..., voice_tool.WindowsPasteAdapter] = voice_tool.WindowsPasteAdapter,
    environ: Mapping[str, str] | None = None,
) -> int:
    args = build_parser().parse_args(argv)
    payload_provider = build_payload_provider(args, opener, now_fn)
    try:
        first_payload = payload_provider()
    except Exception as exc:
        print(f"Error: {exc}", file=stderr)
        return 1

    if args.dry_run:
        print(f"Payload: {first_payload}", file=stdout)
        return 0

    try:
        post_receive = build_voice_post_receive(
            args,
            stdout,
            transcriber_factory,
            paste_adapter_factory,
            opener,
            environ,
        )
    except Exception as exc:
        print(f"Error: {exc}", file=stderr)
        return 1

    try:
        loaded_bleak = bleak_loader()
    except MissingBleakError as exc:
        print(f"Error: {exc}", file=stderr)
        return 1
    scanner_cls, client_cls, device_cls = (*loaded_bleak, None)[:3]

    config = BleConfig(
        device_name=args.device_name,
        service_uuid=SERVICE_UUID,
        address=args.address,
        scan_timeout=args.scan_timeout,
        pair=args.pair,
        poll_interval=args.poll_interval,
        ack_timeout=args.ack_timeout,
        write_response=not args.write_without_response,
        watch=True,
        require_ack=args.require_ack,
        max_writes=args.max_usage_writes,
    )
    receive_timeout = None if args.receive_timeout <= 0 else args.receive_timeout

    def cached_payload_provider() -> str:
        nonlocal first_payload
        if first_payload is not None:
            payload = first_payload
            first_payload = None
            return payload
        return payload_provider()

    try:
        return asyncio.run(
            run_combined_with_retries(
                config,
                cached_payload_provider,
                args.voice_output,
                receive_timeout,
                stdout,
                scanner_cls,
                client_cls,
                args.retry_delay,
                device_cls,
                post_receive=post_receive,
                max_voice_uploads=args.max_voice_uploads,
            )
        )
    except Exception as exc:
        print(f"Error: {exc}", file=stderr)
        return 1


def main() -> int:
    return run()


if __name__ == "__main__":
    raise SystemExit(main())
