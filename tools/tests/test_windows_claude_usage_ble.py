import asyncio
import io
import json
from pathlib import Path
import importlib.util
import sys
import tempfile
import unittest


TOOL_PATH = Path(__file__).resolve().parents[1] / "windows_claude_usage_ble.py"
SPEC = importlib.util.spec_from_file_location("windows_claude_usage_ble", TOOL_PATH)
ble_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ble_tool
SPEC.loader.exec_module(ble_tool)


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
        if uuid == ble_tool.REQ_CHAR_UUID:
            callback(uuid, b"\x01")

    async def write_gatt_char(self, uuid, data, response=True):
        self.writes.append((uuid, data, response))
        callback = self.notify_handlers.get(ble_tool.TX_CHAR_UUID)
        if callback:
            callback(ble_tool.TX_CHAR_UUID, b'{"ack":true}')


class FakeResponse:
    def __init__(self, headers):
        self.headers = headers

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


class WindowsClaudeUsageBleTest(unittest.TestCase):
    def setUp(self):
        FakeClient.instances = []
        FakeScanner.discovered = {
            "AA": (
                FakeDevice("AA:AA:AA:AA:AA:AA", "Claude Controller"),
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[]),
            )
        }

    def test_build_preset_payload_is_compact(self):
        payload = ble_tool.build_preset_payload("high")

        self.assertEqual(json.loads(payload)["s"], 88)
        self.assertNotIn(" ", payload)

    def test_payload_from_headers_converts_utilization_and_reset(self):
        payload = json.loads(
            ble_tool.build_payload_from_headers(
                {
                    "anthropic-ratelimit-unified-5h-utilization": "0.875",
                    "anthropic-ratelimit-unified-5h-reset": "1060",
                    "anthropic-ratelimit-unified-7d-utilization": "0.42",
                    "anthropic-ratelimit-unified-7d-reset": "4600",
                    "anthropic-ratelimit-unified-5h-status": "limited",
                },
                now=1000,
            )
        )

        self.assertEqual(payload["s"], 88)
        self.assertEqual(payload["sr"], 1)
        self.assertEqual(payload["w"], 42)
        self.assertEqual(payload["wr"], 60)
        self.assertEqual(payload["st"], "limited")

    def test_read_access_token_finds_nested_token(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            credentials = Path(tmpdir) / "credentials.json"
            credentials.write_text(json.dumps({"claudeAiOauth": {"accessToken": "token-123"}}), encoding="utf-8")

            self.assertEqual(ble_tool.read_access_token(credentials), "token-123")

    def test_missing_credentials_returns_actionable_error(self):
        stderr = io.StringIO()
        code = ble_tool.run(
            ["--credentials", "/no/such/file", "--dry-run"],
            stdout=io.StringIO(),
            stderr=stderr,
        )

        self.assertEqual(code, 1)
        self.assertIn("Claude credentials not found", stderr.getvalue())

    def test_select_device_prefers_advertised_service_over_stale_name(self):
        stale = FakeDevice("AA:AA:AA:AA:AA:AA", "Claude Controller")
        live = FakeDevice("BB:BB:BB:BB:BB:BB", "Claude Controller")
        discovered = {
            "AA": (stale, FakeAdvertisement(local_name="Claude Controller", service_uuids=[])),
            "BB": (
                live,
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[ble_tool.SERVICE_UUID]),
            ),
        }

        choice = ble_tool.select_device(discovered)

        self.assertEqual(choice.address, "BB:BB:BB:BB:BB:BB")
        self.assertTrue(choice.service_match)

    def test_missing_bleak_import_reports_install_command(self):
        def missing_bleak():
            raise ble_tool.MissingBleakError("bleak is required. Install it with: py -3 -m pip install bleak")

        stderr = io.StringIO()
        code = ble_tool.run(
            ["--test-preset", "normal"],
            stdout=io.StringIO(),
            stderr=stderr,
            bleak_loader=missing_bleak,
        )

        self.assertEqual(code, 1)
        self.assertIn("py -3 -m pip install bleak", stderr.getvalue())

    def test_dry_run_does_not_require_bleak(self):
        code = ble_tool.run(
            ["--test-preset", "normal", "--dry-run"],
            stdout=io.StringIO(),
            stderr=io.StringIO(),
            bleak_loader=lambda: (_ for _ in ()).throw(AssertionError("should not import bleak")),
        )

        self.assertEqual(code, 0)

    def test_ble_session_writes_payload_and_logs_ack(self):
        FakeScanner.discovered = {
            "BB": (
                FakeDevice("BB:BB:BB:BB:BB:BB", "Claude Controller"),
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[ble_tool.SERVICE_UUID]),
            )
        }
        stdout = io.StringIO()
        config = ble_tool.BleConfig(ack_timeout=0.1)

        ok = asyncio.run(
            ble_tool.run_ble_session(
                config,
                lambda: ble_tool.build_preset_payload("high"),
                stdout,
                FakeScanner,
                FakeClient,
            )
        )

        self.assertTrue(ok)
        self.assertEqual(FakeClient.instances[0].writes[0][0], ble_tool.RX_CHAR_UUID)
        self.assertIn(b'"s":88', FakeClient.instances[0].writes[0][1])
        self.assertIn("Device acknowledged payload", stdout.getvalue())

    def test_refresh_notification_triggers_immediate_second_write_in_watch_mode(self):
        FakeScanner.discovered = {
            "BB": (
                FakeDevice("BB:BB:BB:BB:BB:BB", "Claude Controller"),
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[ble_tool.SERVICE_UUID]),
            )
        }
        config = ble_tool.BleConfig(watch=True, max_writes=2, poll_interval=30, ack_timeout=0.1)

        ok = asyncio.run(
            ble_tool.run_ble_session(
                config,
                lambda: ble_tool.build_preset_payload("normal"),
                io.StringIO(),
                FakeScanner,
                FakeClient,
            )
        )

        self.assertTrue(ok)
        self.assertEqual(len(FakeClient.instances[0].writes), 2)


if __name__ == "__main__":
    unittest.main()
