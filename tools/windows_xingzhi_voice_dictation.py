#!/usr/bin/env python3
"""Receive Xingzhi BLE voice audio and save it as a WAV file."""

from __future__ import annotations

import argparse
import asyncio
from dataclasses import dataclass
import json
import os
from pathlib import Path
import sys
import time
from typing import Any, Callable, Mapping, TextIO
from urllib import request
import uuid

TOOL_DIR = Path(__file__).resolve().parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from xingzhi_ble_common import (  # noqa: E402
    DEVICE_NAME,
    SERVICE_UUID,
    BleConfig,
    MissingBleakError,
    create_ble_client,
    find_device,
    load_bleak,
    log,
)


VOICE_CHAR_UUID = "4c41555a-4465-7669-6365-000000000005"
VOICE_CTRL_CHAR_UUID = "4c41555a-4465-7669-6365-000000000006"
FRAME_VERSION = 1
METADATA_FRAME_BYTES = 16
CHUNK_HEADER_BYTES = 9
COMPLETE_FRAME_BYTES = 8
DEFAULT_OUTPUT = Path("xingzhi-voice.wav")
DEFAULT_ENV_FILE = Path(".env")
DEFAULT_TRANSCRIBE_MODEL = "TeleAI/TeleSpeechASR"
SILICONFLOW_TRANSCRIPTIONS_URL = "https://api.siliconflow.cn/v1/audio/transcriptions"
CF_UNICODETEXT = 13
GMEM_MOVEABLE = 0x0002
GMEM_ZEROINIT = 0x0040
KEYEVENTF_KEYUP = 0x0002
VK_CONTROL = 0x11
VK_V = 0x56


@dataclass(frozen=True)
class VoiceMetadata:
    total_bytes: int
    chunk_size: int
    total_chunks: int
    sample_rate: int
    bits_per_sample: int


class MissingSiliconFlowKeyError(RuntimeError):
    """Raised when SILICONFLOW_API_KEY is unavailable."""


class SiliconFlowTranscriber:
    def __init__(
        self,
        api_key: str,
        model: str = DEFAULT_TRANSCRIBE_MODEL,
        timeout: float = 60.0,
        opener: Callable = request.urlopen,
    ) -> None:
        self.api_key = api_key
        self.model = model
        self.timeout = timeout
        self.opener = opener

    def transcribe(self, audio: bytes, filename: str = "xingzhi-voice.wav") -> str:
        if not audio:
            raise ValueError("empty_audio")
        body, content_type = build_transcription_multipart(
            audio,
            filename=filename,
            model=self.model,
        )
        req = request.Request(
            SILICONFLOW_TRANSCRIPTIONS_URL,
            data=body,
            method="POST",
            headers={
                "Authorization": f"Bearer {self.api_key}",
                "Content-Type": content_type,
            },
        )
        with self.opener(req, timeout=self.timeout) as response:
            raw = response.read()
        payload = json.loads(raw.decode("utf-8"))
        text = str(payload.get("text", "")).strip()
        if not text:
            raise RuntimeError("empty_transcript")
        return text


@dataclass(frozen=True)
class PasteResult:
    restored_clipboard: bool
    previous_text_available: bool


class WindowsPasteAdapter:
    def __init__(self, restore_clipboard: bool = True, paste_delay: float = 0.08) -> None:
        if sys.platform != "win32":
            raise RuntimeError("paste_requires_windows")
        import ctypes
        import ctypes.wintypes

        self.ctypes = ctypes
        self.wintypes = ctypes.wintypes
        self.kernel32 = ctypes.windll.kernel32
        self.user32 = ctypes.windll.user32
        self.restore_clipboard = restore_clipboard
        self.paste_delay = paste_delay
        self._configure_clipboard_api()

    def _configure_clipboard_api(self) -> None:
        self.kernel32.GlobalAlloc.argtypes = [self.wintypes.UINT, self.ctypes.c_size_t]
        self.kernel32.GlobalAlloc.restype = self.wintypes.HGLOBAL
        self.kernel32.GlobalLock.argtypes = [self.wintypes.HGLOBAL]
        self.kernel32.GlobalLock.restype = self.ctypes.c_void_p
        self.kernel32.GlobalUnlock.argtypes = [self.wintypes.HGLOBAL]
        self.kernel32.GlobalUnlock.restype = self.wintypes.BOOL
        self.kernel32.GlobalFree.argtypes = [self.wintypes.HGLOBAL]
        self.kernel32.GlobalFree.restype = self.wintypes.HGLOBAL
        self.user32.OpenClipboard.argtypes = [self.wintypes.HWND]
        self.user32.OpenClipboard.restype = self.wintypes.BOOL
        self.user32.CloseClipboard.argtypes = []
        self.user32.CloseClipboard.restype = self.wintypes.BOOL
        self.user32.EmptyClipboard.argtypes = []
        self.user32.EmptyClipboard.restype = self.wintypes.BOOL
        self.user32.IsClipboardFormatAvailable.argtypes = [self.wintypes.UINT]
        self.user32.IsClipboardFormatAvailable.restype = self.wintypes.BOOL
        self.user32.GetClipboardData.argtypes = [self.wintypes.UINT]
        self.user32.GetClipboardData.restype = self.wintypes.HANDLE
        self.user32.SetClipboardData.argtypes = [self.wintypes.UINT, self.wintypes.HANDLE]
        self.user32.SetClipboardData.restype = self.wintypes.HANDLE
        self.user32.keybd_event.argtypes = [
            self.wintypes.BYTE,
            self.wintypes.BYTE,
            self.wintypes.DWORD,
            self.ctypes.c_ulong,
        ]
        self.user32.keybd_event.restype = None

    def paste_text(self, text: str) -> PasteResult:
        if not text:
            raise RuntimeError("empty_paste_text")
        previous_text = self._get_clipboard_text()
        self._set_clipboard_text(text)
        self._send_ctrl_v()
        if self.paste_delay > 0:
            time.sleep(self.paste_delay)
        restored = False
        if self.restore_clipboard and previous_text is not None:
            try:
                self._set_clipboard_text(previous_text)
                restored = True
            except Exception:
                restored = False
        return PasteResult(restored_clipboard=restored, previous_text_available=previous_text is not None)

    def _open_clipboard(self) -> None:
        if not self.user32.OpenClipboard(None):
            raise RuntimeError("clipboard_open_failed")

    def _get_clipboard_text(self) -> str | None:
        self._open_clipboard()
        try:
            if not self.user32.IsClipboardFormatAvailable(CF_UNICODETEXT):
                return None
            handle = self.user32.GetClipboardData(CF_UNICODETEXT)
            if not handle:
                return None
            pointer = self.kernel32.GlobalLock(handle)
            if not pointer:
                return None
            try:
                return self.ctypes.wstring_at(pointer)
            finally:
                self.kernel32.GlobalUnlock(handle)
        finally:
            self.user32.CloseClipboard()

    def _set_clipboard_text(self, text: str) -> None:
        data = text.encode("utf-16le") + b"\x00\x00"
        handle = self.kernel32.GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, len(data))
        if not handle:
            raise RuntimeError("clipboard_alloc_failed")
        pointer = self.kernel32.GlobalLock(handle)
        if not pointer:
            self.kernel32.GlobalFree(handle)
            raise RuntimeError("clipboard_lock_failed")
        try:
            self.ctypes.memmove(pointer, data, len(data))
        finally:
            self.kernel32.GlobalUnlock(handle)

        self._open_clipboard()
        try:
            if not self.user32.EmptyClipboard():
                raise RuntimeError("clipboard_empty_failed")
            if not self.user32.SetClipboardData(CF_UNICODETEXT, handle):
                raise RuntimeError("clipboard_set_failed")
            handle = None
        finally:
            self.user32.CloseClipboard()
            if handle:
                self.kernel32.GlobalFree(handle)

    def _send_ctrl_v(self) -> None:
        self.user32.keybd_event(VK_CONTROL, 0, 0, 0)
        self.user32.keybd_event(VK_V, 0, 0, 0)
        self.user32.keybd_event(VK_V, 0, KEYEVENTF_KEYUP, 0)
        self.user32.keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0)


class VoiceReceiver:
    def __init__(self, stdout: TextIO) -> None:
        self.stdout = stdout
        self.metadata: VoiceMetadata | None = None
        self.chunks: dict[int, bytes] = {}
        self.audio: bytes | None = None
        self.error: str | None = None
        self.finished = asyncio.Event()

    def handle_notification(self, sender: Any, data: bytes | bytearray | memoryview) -> None:
        if self.finished.is_set():
            return
        try:
            self._handle_frame(bytes(data))
        except ValueError as exc:
            self._fail(str(exc))

    async def wait(self, timeout: float | None) -> bytes:
        if timeout is None:
            await self.finished.wait()
        else:
            await asyncio.wait_for(self.finished.wait(), timeout=timeout)
        if self.error:
            raise RuntimeError(self.error)
        if self.audio is None:
            raise RuntimeError("voice_audio_missing")
        return self.audio

    def _fail(self, message: str) -> None:
        self.error = message
        self.finished.set()

    def fail(self, message: str) -> None:
        self._fail(message)

    def _handle_frame(self, frame: bytes) -> None:
        if not frame:
            raise ValueError("empty_frame")
        kind = chr(frame[0])
        if kind == "M":
            self._handle_metadata(frame)
        elif kind == "C":
            self._handle_chunk(frame)
        elif kind == "E":
            self._handle_complete(frame)
        else:
            raise ValueError(f"unknown_frame_{frame[0]:02x}")

    def _handle_metadata(self, frame: bytes) -> None:
        if len(frame) != METADATA_FRAME_BYTES:
            raise ValueError("bad_metadata_length")
        if frame[1] != FRAME_VERSION:
            raise ValueError("bad_metadata_version")
        total_bytes = _u32(frame, 2)
        chunk_size = _u16(frame, 6)
        total_chunks = _u16(frame, 8)
        sample_rate = _u32(frame, 10)
        bits_per_sample = _u16(frame, 14)
        if total_bytes <= 0 or chunk_size <= 0 or total_chunks <= 0:
            raise ValueError("bad_metadata_values")
        expected_chunks = (total_bytes + chunk_size - 1) // chunk_size
        if total_chunks != expected_chunks:
            raise ValueError("metadata_chunk_mismatch")
        self.metadata = VoiceMetadata(total_bytes, chunk_size, total_chunks, sample_rate, bits_per_sample)
        self.chunks.clear()
        log(self.stdout, f"Voice metadata: {total_bytes} bytes, {total_chunks} chunks, {sample_rate} Hz")

    def _handle_chunk(self, frame: bytes) -> None:
        metadata = self._require_metadata()
        if len(frame) < CHUNK_HEADER_BYTES:
            raise ValueError("bad_chunk_length")
        if frame[1] != FRAME_VERSION:
            raise ValueError("bad_chunk_version")
        seq = _u16(frame, 2)
        offset = _u32(frame, 4)
        payload_len = frame[8]
        payload = frame[CHUNK_HEADER_BYTES:]
        if payload_len != len(payload):
            raise ValueError("chunk_payload_length_mismatch")
        if payload_len <= 0 or payload_len > metadata.chunk_size:
            raise ValueError("bad_chunk_payload_length")
        if seq >= metadata.total_chunks:
            raise ValueError("chunk_seq_out_of_range")
        if offset != seq * metadata.chunk_size:
            raise ValueError("chunk_offset_mismatch")
        if offset + payload_len > metadata.total_bytes:
            raise ValueError("chunk_overflow")
        if seq in self.chunks:
            raise ValueError("duplicate_chunk")
        self.chunks[seq] = payload
        if seq == 0 or len(self.chunks) == metadata.total_chunks or len(self.chunks) % 100 == 0:
            log(self.stdout, f"Voice chunks: {len(self.chunks)}/{metadata.total_chunks}")

    def _handle_complete(self, frame: bytes) -> None:
        metadata = self._require_metadata()
        if len(frame) != COMPLETE_FRAME_BYTES:
            raise ValueError("bad_complete_length")
        if frame[1] != FRAME_VERSION:
            raise ValueError("bad_complete_version")
        total_chunks = _u16(frame, 2)
        total_bytes = _u32(frame, 4)
        if total_chunks != metadata.total_chunks or total_bytes != metadata.total_bytes:
            raise ValueError("complete_metadata_mismatch")
        if self.audio is not None:
            raise ValueError("duplicate_complete")
        missing = [seq for seq in range(metadata.total_chunks) if seq not in self.chunks]
        if missing:
            raise ValueError(f"missing_chunk_{missing[0]}")
        audio = b"".join(self.chunks[seq] for seq in range(metadata.total_chunks))
        if len(audio) != metadata.total_bytes:
            raise ValueError("assembled_length_mismatch")
        self.audio = audio
        self.finished.set()
        log(self.stdout, f"Voice complete: {len(audio)} bytes")

    def _require_metadata(self) -> VoiceMetadata:
        if self.metadata is None:
            raise ValueError("metadata_missing")
        return self.metadata


def _u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


def _u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def parse_env_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[key] = value
    return values


def read_siliconflow_api_key(env_file: Path, environ: Mapping[str, str] | None = None) -> str:
    environ = os.environ if environ is None else environ
    key = str(environ.get("SILICONFLOW_API_KEY", "")).strip()
    if key:
        return key
    key = parse_env_file(env_file).get("SILICONFLOW_API_KEY", "").strip()
    if key:
        return key
    raise MissingSiliconFlowKeyError(f"SILICONFLOW_API_KEY not found in environment or {env_file}")


def sanitize_error(message: str, secrets: list[str]) -> str:
    sanitized = message
    for secret in secrets:
        if secret:
            sanitized = sanitized.replace(secret, "[redacted]")
    return sanitized


def voice_control_error(message: str) -> str:
    safe = "".join(char if 32 <= ord(char) <= 126 else "_" for char in message)
    return f"err:{safe[:60] or 'host_error'}"


def _multipart_field(boundary: str, name: str, value: str) -> bytes:
    return (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
        f"{value}\r\n"
    ).encode("utf-8")


def build_transcription_multipart(
    audio: bytes,
    filename: str,
    model: str,
) -> tuple[bytes, str]:
    boundary = f"xingzhi-{uuid.uuid4().hex}"
    body = bytearray()
    body.extend(_multipart_field(boundary, "model", model))
    body.extend(
        (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
            "Content-Type: audio/wav\r\n\r\n"
        ).encode("utf-8")
    )
    body.extend(audio)
    body.extend(f"\r\n--{boundary}--\r\n".encode("utf-8"))
    return bytes(body), f"multipart/form-data; boundary={boundary}"


async def write_voice_control(client: Any, message: str, response: bool = True) -> None:
    await client.write_gatt_char(VOICE_CTRL_CHAR_UUID, message.encode("ascii"), response=response)


async def run_voice_dump_session(
    config: BleConfig,
    output: Path,
    receive_timeout: float | None,
    stdout: TextIO,
    scanner_cls: Any,
    client_cls: Any,
    device_cls: Any | None = None,
    paired_address_resolver: Callable[[str, str], Any] | None = None,
    post_receive: Callable[[bytes, Path], str | None] | None = None,
) -> bool:
    choice = await find_device(scanner_cls, config, stdout, device_cls, paired_address_resolver)
    loop = asyncio.get_running_loop()
    active_receiver: VoiceReceiver | None = VoiceReceiver(stdout)

    def disconnected_callback(client: Any) -> None:
        log(stdout, "BLE disconnected")
        receiver = active_receiver
        if receiver is not None:
            loop.call_soon_threadsafe(receiver.fail, "voice_ble_disconnected")

    def voice_handler(sender: Any, data: bytes | bytearray | memoryview) -> None:
        if active_receiver is None:
            log(stdout, "Ignoring voice frame without active receiver")
            return
        active_receiver.handle_notification(sender, data)

    log(stdout, f"Connecting to {choice.address or config.address or choice.name}")
    async with create_ble_client(client_cls, choice.device, disconnected_callback, config) as client:
        log(stdout, "Connected")
        await client.start_notify(VOICE_CHAR_UUID, voice_handler)
        log(stdout, "Subscribed to voice notifications")

        uploads = 0
        while True:
            receiver = active_receiver or VoiceReceiver(stdout)
            active_receiver = receiver
            log(stdout, "Waiting for GPIO40 voice upload")
            try:
                audio = await receiver.wait(receive_timeout)
            except asyncio.TimeoutError:
                message = "voice_receive_timeout"
                log(stdout, f"Voice receive failed: {message}")
                if config.watch:
                    active_receiver = None
                    continue
                try:
                    await write_voice_control(client, voice_control_error(message), response=config.write_response)
                except Exception as control_exc:
                    log(stdout, f"Voice error write failed: {control_exc}")
                return False
            except Exception as exc:
                message = str(exc) or "voice_receive_error"
                log(stdout, f"Voice receive failed: {message}")
                active_receiver = None
                disconnected = message == "voice_ble_disconnected"
                if not disconnected:
                    try:
                        await write_voice_control(client, voice_control_error(message), response=config.write_response)
                    except Exception as control_exc:
                        log(stdout, f"Voice error write failed: {control_exc}")
                        return False
                if config.watch:
                    if disconnected:
                        return False
                    uploads += 1
                    if config.max_writes and uploads >= config.max_writes:
                        return True
                    continue
                return False

            active_receiver = None
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(audio)
            log(stdout, f"Saved WAV: {output}")
            try:
                status_message = post_receive(audio, output) if post_receive else "done"
            except Exception as exc:
                message = str(exc) or "post_receive_error"
                log(stdout, f"Voice post-receive failed: {message}")
                try:
                    await write_voice_control(client, voice_control_error(message), response=config.write_response)
                except Exception as control_exc:
                    log(stdout, f"Voice error write failed: {control_exc}")
                    return False
                uploads += 1
                if config.watch:
                    if config.max_writes and uploads >= config.max_writes:
                        return True
                    continue
                return False

            try:
                await write_voice_control(client, status_message or "done", response=config.write_response)
            except Exception as control_exc:
                log(stdout, f"Voice error write failed: {control_exc}")
                return False
            uploads += 1
            if not config.watch:
                return True
            if config.max_writes and uploads >= config.max_writes:
                return True


async def run_voice_with_retries(
    config: BleConfig,
    output: Path,
    receive_timeout: float | None,
    stdout: TextIO,
    scanner_cls: Any,
    client_cls: Any,
    retry_delay: float,
    device_cls: Any | None = None,
    paired_address_resolver: Callable[[str, str], Any] | None = None,
    post_receive: Callable[[bytes, Path], str | None] | None = None,
) -> int:
    while True:
        try:
            ok = await run_voice_dump_session(
                config,
                output,
                receive_timeout,
                stdout,
                scanner_cls,
                client_cls,
                device_cls,
                paired_address_resolver,
                post_receive,
            )
            if ok:
                return 0
            if not config.watch:
                return 1
            log(stdout, f"Voice session failed; retrying in {retry_delay:.1f}s")
        except Exception as exc:
            log(stdout, f"BLE error: {exc}")
            if not config.watch:
                return 1
            log(stdout, f"Retrying in {retry_delay:.1f}s")
        await asyncio.sleep(retry_delay)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Receive Xingzhi BLE voice audio and save a WAV file.")
    parser.add_argument("--device-name", default=DEVICE_NAME, help="Advertised BLE device name")
    parser.add_argument("--address", help="Optional BLE address to connect to instead of name/service selection")
    parser.add_argument("--scan-timeout", type=float, default=10.0, help="Seconds to scan for the device")
    parser.add_argument("--pair", action="store_true", help="Ask Bleak/Windows to pair while connecting")
    parser.add_argument("--receive-timeout", type=float, default=45.0, help="Seconds to wait for one voice upload; 0 waits forever")
    parser.add_argument("--retry-delay", type=float, default=5.0, help="Seconds before reconnect retry in --watch mode")
    parser.add_argument("--watch", action="store_true", help="Keep running and process repeated voice uploads")
    parser.add_argument("--max-uploads", type=int, help="Stop after N uploads; intended for tests and debugging")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="Output WAV path")
    parser.add_argument("--input-wav", type=Path, help="Transcribe an existing WAV instead of receiving BLE audio")
    parser.add_argument("--dump-only", action="store_true", help="Receive BLE voice audio and save WAV without ASR")
    parser.add_argument("--asr-only", action="store_true", help="Transcribe audio and print text without pasting")
    parser.add_argument("--no-paste", action="store_true", help="Alias for --asr-only")
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE, help="Local .env file containing SILICONFLOW_API_KEY")
    parser.add_argument("--asr-timeout", type=float, default=60.0, help="Seconds to wait for SiliconFlow transcription")
    parser.add_argument("--no-restore-clipboard", action="store_true", help="Do not restore previous clipboard text after pasting")
    parser.add_argument("--paste-delay", type=float, default=0.08, help="Seconds to wait before clipboard restoration")
    parser.add_argument(
        "--write-without-response",
        action="store_true",
        help="Write host status without BLE response",
    )
    return parser


def run(
    argv: list[str] | None = None,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
    bleak_loader: Callable = load_bleak,
    transcriber_factory: Callable[..., SiliconFlowTranscriber] = SiliconFlowTranscriber,
    paste_adapter_factory: Callable[..., WindowsPasteAdapter] = WindowsPasteAdapter,
    opener: Callable = request.urlopen,
    environ: Mapping[str, str] | None = None,
) -> int:
    args = build_parser().parse_args(argv)

    asr_only = args.asr_only or args.no_paste
    if args.watch and args.input_wav is not None:
        print("Error: --watch cannot be combined with --input-wav", file=stderr)
        return 1
    if args.dump_only and (args.input_wav is not None or asr_only):
        print("Error: --dump-only cannot be combined with --input-wav, --asr-only, or --no-paste", file=stderr)
        return 1
    paste_enabled = not args.dump_only and not asr_only and args.input_wav is None
    needs_asr = not args.dump_only
    api_key = ""
    if needs_asr:
        try:
            api_key = read_siliconflow_api_key(args.env_file, environ)
        except Exception as exc:
            print(f"Error: {exc}", file=stderr)
            return 1

    def transcribe_audio(audio: bytes, filename: str) -> str:
        transcriber = transcriber_factory(
            api_key=api_key,
            model=DEFAULT_TRANSCRIBE_MODEL,
            timeout=args.asr_timeout,
            opener=opener,
        )
        return transcriber.transcribe(audio, filename=filename)

    if args.input_wav:
        try:
            transcript = transcribe_audio(args.input_wav.read_bytes(), args.input_wav.name)
        except Exception as exc:
            print(f"Error: {sanitize_error(str(exc), [api_key])}", file=stderr)
            return 1
        print(f"Transcript: {transcript}", file=stdout)
        return 0

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
        write_response=not args.write_without_response,
        watch=args.watch,
        max_writes=args.max_uploads,
    )
    receive_timeout = None if args.receive_timeout <= 0 else args.receive_timeout

    post_receive: Callable[[bytes, Path], str | None] | None = None
    if needs_asr:
        paste_adapter = None
        if paste_enabled:
            try:
                paste_adapter = paste_adapter_factory(
                    restore_clipboard=not args.no_restore_clipboard,
                    paste_delay=args.paste_delay,
                )
            except Exception as exc:
                print(f"Error: {exc}", file=stderr)
                return 1

        def transcribe_received_audio(audio: bytes, output_path: Path) -> str:
            try:
                transcript = transcribe_audio(audio, output_path.name)
            except Exception as exc:
                raise RuntimeError(sanitize_error(str(exc), [api_key])) from None
            if paste_adapter is not None:
                try:
                    paste_adapter.paste_text(transcript)
                except Exception as exc:
                    raise RuntimeError(sanitize_error(f"paste_failed:{exc}", [api_key])) from None
                print("Pasted transcript", file=stdout)
            else:
                print(f"Transcript: {transcript}", file=stdout)
            return "done"

        post_receive = transcribe_received_audio

    try:
        ok = asyncio.run(
            run_voice_with_retries(
                config,
                args.output,
                receive_timeout,
                stdout,
                scanner_cls,
                client_cls,
                args.retry_delay,
                device_cls,
                post_receive=post_receive,
            )
        )
    except Exception as exc:
        print(f"Error: {exc}", file=stderr)
        return 1
    return ok


def main() -> int:
    return run()


if __name__ == "__main__":
    raise SystemExit(main())
