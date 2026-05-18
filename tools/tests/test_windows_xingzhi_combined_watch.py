import io
import json
from pathlib import Path
import importlib.util
import sys
import tempfile
import unittest


TOOL_PATH = Path(__file__).resolve().parents[1] / "windows_xingzhi_combined_watch.py"
SPEC = importlib.util.spec_from_file_location("windows_xingzhi_combined_watch", TOOL_PATH)
combined_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = combined_tool
SPEC.loader.exec_module(combined_tool)


class FakeAdvertisement:
    def __init__(self, local_name="", service_uuids=None):
        self.local_name = local_name
        self.service_uuids = service_uuids or []


class FakeDevice:
    def __init__(self, address, name=""):
        self.address = address
        self.name = name


class FakeScanner:
    discovered = {}

    @classmethod
    async def discover(cls, timeout=10.0, return_adv=False):
        return cls.discovered


class FakeClient:
    instances = []
    voice_frames = []
    emit_req_after_first_rx = False
    disconnects_remaining = 0

    def __init__(self, target, disconnected_callback=None):
        self.target = target
        self.disconnected_callback = disconnected_callback
        self.notify_handlers = {}
        self.writes = []
        self.rx_writes = 0
        FakeClient.instances.append(self)

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        return False

    async def start_notify(self, uuid, callback):
        self.notify_handlers[uuid] = callback
        if uuid == combined_tool.voice_tool.VOICE_CHAR_UUID and FakeClient.disconnects_remaining > 0:
            FakeClient.disconnects_remaining -= 1
            combined_tool.asyncio.get_running_loop().call_soon(self.disconnected_callback, self)
            return
        if uuid == combined_tool.voice_tool.VOICE_CHAR_UUID and FakeClient.voice_frames:
            combined_tool.asyncio.get_running_loop().call_later(0.001, self._emit_voice_frames, uuid)

    async def write_gatt_char(self, uuid, data, response=True):
        self.writes.append((uuid, data, response))
        if uuid == combined_tool.usage_tool.RX_CHAR_UUID:
            self.rx_writes += 1
            tx_callback = self.notify_handlers.get(combined_tool.usage_tool.TX_CHAR_UUID)
            if tx_callback:
                tx_callback(combined_tool.usage_tool.TX_CHAR_UUID, b'{"ack":true}')
            req_callback = self.notify_handlers.get(combined_tool.usage_tool.REQ_CHAR_UUID)
            if FakeClient.emit_req_after_first_rx and self.rx_writes == 1 and req_callback:
                combined_tool.asyncio.get_running_loop().call_soon(
                    req_callback,
                    combined_tool.usage_tool.REQ_CHAR_UUID,
                    b"\x01",
                )

    def _emit_voice_frames(self, uuid):
        handler = self.notify_handlers.get(uuid)
        if not handler:
            return
        for frame in FakeClient.voice_frames:
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
        return "你好，合并模式"


class FakePasteAdapter:
    instances = []

    def __init__(self, restore_clipboard=True, paste_delay=0.08):
        self.restore_clipboard = restore_clipboard
        self.paste_delay = paste_delay
        self.texts = []
        FakePasteAdapter.instances.append(self)

    def paste_text(self, text):
        self.texts.append(text)
        return combined_tool.voice_tool.PasteResult(restored_clipboard=True, previous_text_available=True)


def metadata_frame(total_bytes, chunk_size, total_chunks, sample_rate=16000, bits=16):
    return (
        b"M"
        + bytes([combined_tool.voice_tool.FRAME_VERSION])
        + int(total_bytes).to_bytes(4, "little")
        + int(chunk_size).to_bytes(2, "little")
        + int(total_chunks).to_bytes(2, "little")
        + int(sample_rate).to_bytes(4, "little")
        + int(bits).to_bytes(2, "little")
    )


def chunk_frame(seq, offset, payload):
    return (
        b"C"
        + bytes([combined_tool.voice_tool.FRAME_VERSION])
        + int(seq).to_bytes(2, "little")
        + int(offset).to_bytes(4, "little")
        + bytes([len(payload)])
        + payload
    )


def complete_frame(total_chunks, total_bytes):
    return (
        b"E"
        + bytes([combined_tool.voice_tool.FRAME_VERSION])
        + int(total_chunks).to_bytes(2, "little")
        + int(total_bytes).to_bytes(4, "little")
    )


def frames_for(audio, chunk_size=8):
    chunks = [audio[index : index + chunk_size] for index in range(0, len(audio), chunk_size)]
    frames = [metadata_frame(len(audio), chunk_size, len(chunks))]
    for seq, payload in enumerate(chunks):
        frames.append(chunk_frame(seq, seq * chunk_size, payload))
    frames.append(complete_frame(len(chunks), len(audio)))
    return frames


class WindowsXingzhiCombinedWatchTest(unittest.TestCase):
    def setUp(self):
        FakeClient.instances = []
        FakeClient.voice_frames = []
        FakeClient.emit_req_after_first_rx = False
        FakeClient.disconnects_remaining = 0
        FakeTranscriber.instances = []
        FakePasteAdapter.instances = []
        FakeScanner.discovered = {
            "BB": (
                FakeDevice("BB:BB:BB:BB:BB:BB", "Claude Controller"),
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[combined_tool.SERVICE_UUID]),
            )
        }

    def test_combined_session_uses_one_connection_for_usage_and_voice(self):
        audio = b"RIFF" + b"\x00" * 40 + b"combined"
        FakeClient.voice_frames = frames_for(audio)
        stdout = io.StringIO()

        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "voice.wav"
            env_file = Path(tmpdir) / ".env"
            env_file.write_text("SILICONFLOW_API_KEY=sk-test-123\n", encoding="utf-8")
            code = combined_tool.run(
                [
                    "--test-preset",
                    "high",
                    "--max-voice-uploads",
                    "1",
                    "--voice-output",
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
            saved = output.read_bytes()

        self.assertEqual(code, 0)
        self.assertEqual(len(FakeClient.instances), 1)
        self.assertEqual(saved, audio)
        writes = FakeClient.instances[0].writes
        self.assertEqual(writes[0][0], combined_tool.usage_tool.RX_CHAR_UUID)
        self.assertEqual(writes[-1][0], combined_tool.voice_tool.VOICE_CTRL_CHAR_UUID)
        self.assertEqual(writes[-1][1], b"done")
        self.assertEqual(FakePasteAdapter.instances[0].texts, ["你好，合并模式"])
        self.assertIn("Pasted transcript", stdout.getvalue())

    def test_voice_wait_does_not_block_usage_polling(self):
        stdout = io.StringIO()
        code = combined_tool.run(
            [
                "--test-preset",
                "normal",
                "--dump-only",
                "--max-usage-writes",
                "2",
                "--poll-interval",
                "0.01",
            ],
            stdout=stdout,
            stderr=io.StringIO(),
            bleak_loader=lambda: (FakeScanner, FakeClient),
        )

        self.assertEqual(code, 0)
        writes = [write for write in FakeClient.instances[0].writes if write[0] == combined_tool.usage_tool.RX_CHAR_UUID]
        self.assertEqual(len(writes), 2)
        self.assertIn("Poll interval elapsed; polling again", stdout.getvalue())

    def test_req_triggers_usage_refresh_while_voice_waits(self):
        FakeClient.emit_req_after_first_rx = True
        stdout = io.StringIO()
        code = combined_tool.run(
            [
                "--test-preset",
                "normal",
                "--dump-only",
                "--max-usage-writes",
                "2",
                "--poll-interval",
                "99",
            ],
            stdout=stdout,
            stderr=io.StringIO(),
            bleak_loader=lambda: (FakeScanner, FakeClient),
        )

        self.assertEqual(code, 0)
        writes = [write for write in FakeClient.instances[0].writes if write[0] == combined_tool.usage_tool.RX_CHAR_UUID]
        self.assertEqual(len(writes), 2)
        self.assertIn("Refresh requested by device; polling immediately", stdout.getvalue())

    def test_disconnect_retries_combined_session(self):
        FakeClient.disconnects_remaining = 1
        stdout = io.StringIO()
        code = combined_tool.run(
            [
                "--test-preset",
                "normal",
                "--dump-only",
                "--max-usage-writes",
                "1",
                "--retry-delay",
                "0",
            ],
            stdout=stdout,
            stderr=io.StringIO(),
            bleak_loader=lambda: (FakeScanner, FakeClient),
        )

        self.assertEqual(code, 0)
        self.assertEqual(len(FakeClient.instances), 2)
        self.assertIn("BLE disconnected", stdout.getvalue())
        self.assertIn("Combined BLE session failed; retrying in 0.0s", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
