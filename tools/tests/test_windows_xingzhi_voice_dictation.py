import asyncio
import io
import json
from pathlib import Path
import importlib.util
import sys
import tempfile
import unittest


TOOL_PATH = Path(__file__).resolve().parents[1] / "windows_xingzhi_voice_dictation.py"
SPEC = importlib.util.spec_from_file_location("windows_xingzhi_voice_dictation", TOOL_PATH)
voice_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = voice_tool
SPEC.loader.exec_module(voice_tool)


class FakeAdvertisement:
    def __init__(self, local_name="", service_uuids=None):
        self.local_name = local_name
        self.service_uuids = service_uuids or []


class FakeDevice:
    def __init__(self, address, name=""):
        self.address = address
        self.name = name


class FakeDirectDevice(FakeDevice):
    def __init__(self, address, name="", details=None):
        super().__init__(address, name)
        self.details = details


class FakeScanner:
    discovered = {}

    @classmethod
    async def discover(cls, timeout=10.0, return_adv=False):
        return cls.discovered


class FakeClient:
    instances = []
    frames = []
    frame_batches = []
    disconnects_remaining = 0

    def __init__(self, target, disconnected_callback=None):
        self.target = target
        self.disconnected_callback = disconnected_callback
        self.notify_handlers = {}
        self.writes = []
        FakeClient.instances.append(self)

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        return False

    async def start_notify(self, uuid, callback):
        self.notify_handlers[uuid] = callback
        if FakeClient.disconnects_remaining > 0:
            FakeClient.disconnects_remaining -= 1
            asyncio.get_running_loop().call_soon(self.disconnected_callback, self)
            return
        if self.frame_batches:
            asyncio.get_running_loop().call_soon(self._emit_next_batch, uuid)
            return
        for frame in self.frames:
            callback(uuid, frame)

    async def write_gatt_char(self, uuid, data, response=True):
        self.writes.append((uuid, data, response))
        if self.frame_batches:
            asyncio.get_running_loop().call_soon(self._emit_next_batch, voice_tool.VOICE_CHAR_UUID)

    def _emit_next_batch(self, uuid):
        if not self.frame_batches:
            return
        handler = self.notify_handlers.get(uuid)
        if not handler:
            return
        batch = self.frame_batches.pop(0)
        for frame in batch:
            handler(uuid, frame)


class FakeResponse:
    def __init__(self, payload):
        self.payload = payload

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def read(self):
        return json.dumps(self.payload).encode("utf-8")


class FakeTranscriber:
    instances = []

    def __init__(self, api_key, model, timeout=60.0, opener=None):
        self.api_key = api_key
        self.model = model
        self.timeout = timeout
        self.opener = opener
        self.calls = []
        FakeTranscriber.instances.append(self)

    def transcribe(self, audio, filename="xingzhi-voice.wav"):
        self.calls.append((audio, filename))
        return "你好，测试"


class LeakyTranscriber(FakeTranscriber):
    def transcribe(self, audio, filename="xingzhi-voice.wav"):
        raise RuntimeError(f"bad key {self.api_key}")


class FakePasteAdapter:
    instances = []

    def __init__(self, restore_clipboard=True, paste_delay=0.08):
        self.restore_clipboard = restore_clipboard
        self.paste_delay = paste_delay
        self.texts = []
        FakePasteAdapter.instances.append(self)

    def paste_text(self, text):
        self.texts.append(text)
        return voice_tool.PasteResult(restored_clipboard=True, previous_text_available=True)


class FailingPasteAdapter(FakePasteAdapter):
    def paste_text(self, text):
        self.texts.append(text)
        raise RuntimeError("window_not_focusable")


class FailsOncePasteAdapter(FakePasteAdapter):
    failed = False

    def paste_text(self, text):
        self.texts.append(text)
        if not FailsOncePasteAdapter.failed:
            FailsOncePasteAdapter.failed = True
            raise RuntimeError("window_not_focusable")
        return voice_tool.PasteResult(restored_clipboard=True, previous_text_available=True)


class FlakyScanner(FakeScanner):
    failures_remaining = 0

    @classmethod
    async def discover(cls, timeout=10.0, return_adv=False):
        if cls.failures_remaining > 0:
            cls.failures_remaining -= 1
            raise RuntimeError("scan_failed")
        return await super().discover(timeout=timeout, return_adv=return_adv)


def metadata_frame(total_bytes, chunk_size, total_chunks, sample_rate=16000, bits=16):
    return (
        b"M"
        + bytes([voice_tool.FRAME_VERSION])
        + int(total_bytes).to_bytes(4, "little")
        + int(chunk_size).to_bytes(2, "little")
        + int(total_chunks).to_bytes(2, "little")
        + int(sample_rate).to_bytes(4, "little")
        + int(bits).to_bytes(2, "little")
    )


def chunk_frame(seq, offset, payload):
    return (
        b"C"
        + bytes([voice_tool.FRAME_VERSION])
        + int(seq).to_bytes(2, "little")
        + int(offset).to_bytes(4, "little")
        + bytes([len(payload)])
        + payload
    )


def complete_frame(total_chunks, total_bytes):
    return (
        b"E"
        + bytes([voice_tool.FRAME_VERSION])
        + int(total_chunks).to_bytes(2, "little")
        + int(total_bytes).to_bytes(4, "little")
    )


def frames_for(audio, chunk_size=4):
    chunks = [audio[index : index + chunk_size] for index in range(0, len(audio), chunk_size)]
    frames = [metadata_frame(len(audio), chunk_size, len(chunks))]
    for seq, payload in enumerate(chunks):
        frames.append(chunk_frame(seq, seq * chunk_size, payload))
    frames.append(complete_frame(len(chunks), len(audio)))
    return frames


class WindowsXingzhiVoiceDictationTest(unittest.TestCase):
    def setUp(self):
        FakeClient.instances = []
        FakeClient.frames = []
        FakeClient.frame_batches = []
        FakeClient.disconnects_remaining = 0
        FakeTranscriber.instances = []
        FakePasteAdapter.instances = []
        FailsOncePasteAdapter.failed = False
        FlakyScanner.failures_remaining = 0
        FakeScanner.discovered = {
            "BB": (
                FakeDevice("BB:BB:BB:BB:BB:BB", "Claude Controller"),
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[voice_tool.SERVICE_UUID]),
            )
        }

    def test_receiver_reassembles_ordered_frames(self):
        receiver = voice_tool.VoiceReceiver(io.StringIO())
        audio = b"RIFF123456"

        for frame in frames_for(audio, chunk_size=4):
            receiver.handle_notification("voice", frame)
        result = asyncio.run(receiver.wait(0.1))

        self.assertEqual(result, audio)
        self.assertIsNone(receiver.error)

    def test_receiver_wait_accepts_no_timeout(self):
        receiver = voice_tool.VoiceReceiver(io.StringIO())
        audio = b"RIFF123456"

        for frame in frames_for(audio, chunk_size=4):
            receiver.handle_notification("voice", frame)
        result = asyncio.run(receiver.wait(None))

        self.assertEqual(result, audio)

    def test_ble_dump_saves_wav_and_writes_done(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.frames = frames_for(audio, chunk_size=8)
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            ok = asyncio.run(
                voice_tool.run_voice_dump_session(
                    voice_tool.BleConfig(),
                    output,
                    0.1,
                    stdout,
                    FakeScanner,
                    FakeClient,
                )
            )
            saved = output.read_bytes()

        self.assertTrue(ok)
        self.assertEqual(saved, audio)
        self.assertEqual(FakeClient.instances[0].writes[-1][0], voice_tool.VOICE_CTRL_CHAR_UUID)
        self.assertEqual(FakeClient.instances[0].writes[-1][1], b"done")
        self.assertIn("Saved WAV", stdout.getvalue())

    def test_ble_asr_only_transcribes_before_writing_done(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.frames = frames_for(audio, chunk_size=8)
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                ["--asr-only", "--output", str(output), "--env-file", str(env_file)],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                environ={},
            )
            saved = output.read_bytes()

        self.assertEqual(code, 0)
        self.assertEqual(saved, audio)
        self.assertIn("Transcript: 你好，测试", stdout.getvalue())
        self.assertEqual(FakeTranscriber.instances[0].calls[0][0], audio)
        self.assertEqual(FakeClient.instances[0].writes[-1][1], b"done")
        self.assertEqual(FakePasteAdapter.instances, [])

    def test_ble_no_paste_alias_transcribes_without_paste_adapter(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.frames = frames_for(audio, chunk_size=8)
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                ["--no-paste", "--output", str(output), "--env-file", str(env_file)],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                paste_adapter_factory=lambda **kwargs: (_ for _ in ()).throw(AssertionError("should not paste")),
                environ={},
            )

        self.assertEqual(code, 0)
        self.assertIn("Transcript: 你好，测试", stdout.getvalue())
        self.assertEqual(FakePasteAdapter.instances, [])

    def test_ble_default_dictation_pastes_and_writes_done(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.frames = frames_for(audio, chunk_size=8)
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                ["--output", str(output), "--env-file", str(env_file)],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                paste_adapter_factory=FakePasteAdapter,
                environ={},
            )

        self.assertEqual(code, 0)
        self.assertIn("Pasted transcript", stdout.getvalue())
        self.assertNotIn("Transcript: 你好，测试", stdout.getvalue())
        self.assertEqual(FakePasteAdapter.instances[0].texts, ["你好，测试"])
        self.assertEqual(FakeClient.instances[0].writes[-1][1], b"done")

    def test_watch_processes_multiple_uploads_in_one_session(self):
        audio_one = b"RIFF" + b"\x00" * 40 + b"first"
        audio_two = b"RIFF" + b"\x00" * 40 + b"second"
        FakeClient.frame_batches = [
            frames_for(audio_one, chunk_size=8),
            frames_for(audio_two, chunk_size=8),
        ]
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                ["--watch", "--max-uploads", "2", "--output", str(output), "--env-file", str(env_file)],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                paste_adapter_factory=FakePasteAdapter,
                environ={},
            )
            saved = output.read_bytes()

        self.assertEqual(code, 0)
        self.assertEqual(saved, audio_two)
        self.assertEqual([write[1] for write in FakeClient.instances[0].writes], [b"done", b"done"])
        self.assertEqual(len(FakeTranscriber.instances), 2)
        self.assertEqual(FakePasteAdapter.instances[0].texts, ["你好，测试", "你好，测试"])

    def test_watch_paste_error_continues_to_next_upload(self):
        audio_one = b"RIFF" + b"\x00" * 40 + b"first"
        audio_two = b"RIFF" + b"\x00" * 40 + b"second"
        FakeClient.frame_batches = [
            frames_for(audio_one, chunk_size=8),
            frames_for(audio_two, chunk_size=8),
        ]

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                ["--watch", "--max-uploads", "2", "--output", str(output), "--env-file", str(env_file)],
                stdout=io.StringIO(),
                stderr=io.StringIO(),
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                paste_adapter_factory=FailsOncePasteAdapter,
                environ={},
            )

        self.assertEqual(code, 0)
        writes = [write[1] for write in FakeClient.instances[0].writes]
        self.assertTrue(writes[0].startswith(b"err:paste_failed:window_not_focusable"))
        self.assertEqual(writes[1], b"done")

    def test_ble_default_dictation_paste_error_writes_error(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.frames = frames_for(audio, chunk_size=8)
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                ["--output", str(output), "--env-file", str(env_file)],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                paste_adapter_factory=FailingPasteAdapter,
                environ={},
            )

        self.assertEqual(code, 1)
        self.assertTrue(FakeClient.instances[0].writes[-1][1].startswith(b"err:paste_failed:window_not_focusable"))

    def test_watch_retries_after_scan_error(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.frame_batches = [frames_for(audio, chunk_size=8)]
        FlakyScanner.failures_remaining = 1
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                [
                    "--watch",
                    "--max-uploads",
                    "1",
                    "--retry-delay",
                    "0",
                    "--output",
                    str(output),
                    "--env-file",
                    str(env_file),
                ],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (FlakyScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                paste_adapter_factory=FakePasteAdapter,
                environ={},
            )

        self.assertEqual(code, 0)
        self.assertIn("Retrying in 0.0s", stdout.getvalue())
        self.assertEqual(FakeClient.instances[0].writes[-1][1], b"done")

    def test_watch_retries_after_disconnect_while_waiting_forever(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.disconnects_remaining = 1
        FakeClient.frame_batches = [frames_for(audio, chunk_size=8)]
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = voice_tool.run(
                [
                    "--watch",
                    "--max-uploads",
                    "1",
                    "--receive-timeout",
                    "0",
                    "--retry-delay",
                    "0",
                    "--output",
                    str(output),
                    "--env-file",
                    str(env_file),
                ],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=FakeTranscriber,
                paste_adapter_factory=FakePasteAdapter,
                environ={},
            )

        self.assertEqual(code, 0)
        self.assertEqual(len(FakeClient.instances), 2)
        self.assertIn("BLE disconnected", stdout.getvalue())
        self.assertIn("Voice session failed; retrying in 0.0s", stdout.getvalue())
        self.assertEqual(FakeClient.instances[-1].writes[-1][1], b"done")

    def test_ble_asr_error_writes_error_and_redacts_key(self):
        audio = b"RIFF" + b"\x00" * 40 + b"pcm-data"
        FakeClient.frames = frames_for(audio, chunk_size=8)
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-secret-value\n", encoding="utf-8")
            code = voice_tool.run(
                ["--asr-only", "--output", str(output), "--env-file", str(env_file)],
                stdout=stdout,
                stderr=stderr,
                bleak_loader=lambda: (FakeScanner, FakeClient),
                transcriber_factory=LeakyTranscriber,
                environ={},
            )

        self.assertEqual(code, 1)
        last_write = FakeClient.instances[0].writes[-1][1].decode("ascii")
        combined = stdout.getvalue() + stderr.getvalue() + last_write
        self.assertTrue(last_write.startswith("err:bad key [redacted]"))
        self.assertNotIn("sk-secret-value", combined)

    def test_ble_dump_missing_chunk_writes_error(self):
        audio = b"abcdefgh"
        FakeClient.frames = [
            metadata_frame(len(audio), 4, 2),
            chunk_frame(1, 4, b"efgh"),
            complete_frame(2, len(audio)),
        ]
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            ok = asyncio.run(
                voice_tool.run_voice_dump_session(
                    voice_tool.BleConfig(),
                    output,
                    0.1,
                    stdout,
                    FakeScanner,
                    FakeClient,
                )
            )

        self.assertFalse(ok)
        self.assertFalse(output.exists())
        self.assertEqual(FakeClient.instances[0].writes[-1][0], voice_tool.VOICE_CTRL_CHAR_UUID)
        self.assertTrue(FakeClient.instances[0].writes[-1][1].startswith(b"err:missing_chunk_0"))
        self.assertIn("Voice receive failed", stdout.getvalue())

    def test_address_mode_uses_direct_ble_device(self):
        audio = b"RIFF-data"
        FakeClient.frames = frames_for(audio, chunk_size=4)
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            ok = asyncio.run(
                voice_tool.run_voice_dump_session(
                    voice_tool.BleConfig(address="94:A9:90:1B:6D:FD"),
                    output,
                    0.1,
                    stdout,
                    FakeScanner,
                    FakeClient,
                    FakeDirectDevice,
                )
            )

        self.assertTrue(ok)
        self.assertIsInstance(FakeClient.instances[0].target, FakeDirectDevice)
        self.assertIn("Using direct BLE address", stdout.getvalue())

    def test_missing_bleak_import_reports_install_command(self):
        def missing_bleak():
            raise voice_tool.MissingBleakError("bleak is required. Install it with: py -3 -m pip install bleak")

        stderr = io.StringIO()
        code = voice_tool.run(
            ["--dump-only", "--output", "unused.wav"],
            stdout=io.StringIO(),
            stderr=stderr,
            bleak_loader=missing_bleak,
        )

        self.assertEqual(code, 1)
        self.assertIn("py -3 -m pip install bleak", stderr.getvalue())

    def test_env_file_parser_reads_quoted_siliconflow_key(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY='sk-test-123'\nOTHER=value\n", encoding="utf-8")

            key = voice_tool.read_siliconflow_api_key(env_file, environ={})

        self.assertEqual(key, "sk-test-123")

    def test_input_wav_asr_only_uses_fake_transcriber(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            wav = Path(tmpdir) / "voice.wav"
            wav.write_bytes(b"RIFF" + b"\x00" * 44)
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            stdout = io.StringIO()
            code = voice_tool.run(
                ["--input-wav", str(wav), "--env-file", str(env_file)],
                stdout=stdout,
                stderr=io.StringIO(),
                bleak_loader=lambda: (_ for _ in ()).throw(AssertionError("should not import bleak")),
                transcriber_factory=FakeTranscriber,
                environ={},
            )

        self.assertEqual(code, 0)
        self.assertIn("Transcript: 你好，测试", stdout.getvalue())
        self.assertEqual(FakeTranscriber.instances[0].model, "TeleAI/TeleSpeechASR")
        self.assertEqual(FakeTranscriber.instances[0].calls[0][1], "voice.wav")

    def test_missing_siliconflow_key_returns_error_before_bleak(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            wav = Path(tmpdir) / "voice.wav"
            wav.write_bytes(b"RIFF")
            stderr = io.StringIO()
            code = voice_tool.run(
                ["--input-wav", str(wav), "--env-file", str(Path(tmpdir) / ".env")],
                stdout=io.StringIO(),
                stderr=stderr,
                bleak_loader=lambda: (_ for _ in ()).throw(AssertionError("should not import bleak")),
                environ={},
            )

        self.assertEqual(code, 1)
        self.assertIn("SILICONFLOW_API_KEY not found", stderr.getvalue())

    def test_asr_error_does_not_leak_api_key(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            wav = Path(tmpdir) / "voice.wav"
            wav.write_bytes(b"RIFF")
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-secret-value\n", encoding="utf-8")
            stderr = io.StringIO()
            code = voice_tool.run(
                ["--input-wav", str(wav), "--env-file", str(env_file)],
                stdout=io.StringIO(),
                stderr=stderr,
                bleak_loader=lambda: (_ for _ in ()).throw(AssertionError("should not import bleak")),
                transcriber_factory=LeakyTranscriber,
                environ={},
            )

        self.assertEqual(code, 1)
        self.assertIn("[redacted]", stderr.getvalue())
        self.assertNotIn("sk-secret-value", stderr.getvalue())

    def test_siliconflow_transcriber_builds_multipart_request(self):
        requests = []

        def fake_opener(req, timeout=60.0):
            requests.append((req, timeout))
            return FakeResponse({"text": "hello"})

        transcriber = voice_tool.SiliconFlowTranscriber(
            api_key="sk-test-123",
            model="TeleAI/TeleSpeechASR",
            timeout=12.0,
            opener=fake_opener,
        )

        text = transcriber.transcribe(b"RIFF-data", filename="voice.wav")

        self.assertEqual(text, "hello")
        req, timeout = requests[0]
        self.assertEqual(timeout, 12.0)
        self.assertEqual(req.full_url, voice_tool.SILICONFLOW_TRANSCRIPTIONS_URL)
        self.assertEqual(req.headers["Authorization"], "Bearer sk-test-123")
        body = req.data
        self.assertIn(b'name="model"', body)
        self.assertIn(b"TeleAI/TeleSpeechASR", body)
        self.assertNotIn(b'name="language"', body)
        self.assertNotIn(b'name="response_format"', body)
        self.assertIn(b"RIFF-data", body)


if __name__ == "__main__":
    unittest.main()
