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


class FakeDirectDevice(FakeDevice):
    def __init__(self, address, name="", details=None):
        super().__init__(address, name)
        self.details = details


class FakeDeviceInfo:
    def __init__(self, name, device_id):
        self.name = name
        self.id = device_id


class FakeScanner:
    discovered = {}

    @classmethod
    async def discover(cls, timeout=10.0, return_adv=False):
        return cls.discovered


class FailingScanner:
    @classmethod
    async def discover(cls, timeout=10.0, return_adv=False):
        raise AssertionError("address mode should not scan")


class EmptyScanner:
    @classmethod
    async def discover(cls, timeout=10.0, return_adv=False):
        return {}


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


class NackClient(FakeClient):
    async def write_gatt_char(self, uuid, data, response=True):
        self.writes.append((uuid, data, response))
        callback = self.notify_handlers.get(ble_tool.TX_CHAR_UUID)
        if callback:
            callback(ble_tool.TX_CHAR_UUID, b'{"err":true}')


class NackThenAckClient(FakeClient):
    attempts = 0

    async def write_gatt_char(self, uuid, data, response=True):
        self.writes.append((uuid, data, response))
        callback = self.notify_handlers.get(ble_tool.TX_CHAR_UUID)
        if not callback:
            return
        NackThenAckClient.attempts += 1
        if NackThenAckClient.attempts == 1:
            callback(ble_tool.TX_CHAR_UUID, b'{"err":true}')
        else:
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
        NackThenAckClient.attempts = 0
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
        self.assertEqual(payload["src"], "claude")

    def test_payload_from_codex_rate_limits_converts_usage_windows(self):
        payload = json.loads(
            ble_tool.build_payload_from_codex_rate_limits(
                {
                    "primary": {"used_percent": 23, "resets_at": 1300},
                    "secondary": {"used_percent": 52, "resets_at": 4600},
                    "plan_type": "pro",
                    "rate_limit_reached_type": None,
                },
                now=1000,
            )
        )

        self.assertEqual(payload["s"], 23)
        self.assertEqual(payload["sr"], 5)
        self.assertEqual(payload["w"], 52)
        self.assertEqual(payload["wr"], 60)
        self.assertEqual(payload["st"], "allowed")
        self.assertEqual(payload["src"], "codex")

    def test_payload_from_codex_rate_limits_marks_limit_reached(self):
        payload = json.loads(
            ble_tool.build_payload_from_codex_rate_limits(
                {
                    "primary": {"used_percent": 100, "reset_after_seconds": 120},
                    "secondary": {"used_percent": 20, "reset_after_seconds": 3600},
                    "rate_limit_reached_type": "primary",
                },
                now=1000,
            )
        )

        self.assertEqual(payload["s"], 100)
        self.assertEqual(payload["sr"], 2)
        self.assertEqual(payload["st"], "limited")

    def test_codex_wsl_dry_run_reads_latest_session_payload(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            codex_home = Path(tmpdir)
            session = codex_home / "sessions" / "2026" / "05" / "14" / "rollout.jsonl"
            session.parent.mkdir(parents=True)
            session.write_text(
                "\n".join(
                    [
                        json.dumps({"type": "event_msg", "payload": {"type": "other"}}),
                        json.dumps(
                            {
                                "timestamp": "2026-05-14T02:45:05.274Z",
                                "type": "event_msg",
                                "payload": {
                                    "type": "token_count",
                                    "rate_limits": {
                                        "primary": {"used_percent": 9, "resets_at": 1300},
                                        "secondary": {"used_percent": 37, "resets_at": 4600},
                                        "plan_type": "pro",
                                    },
                                },
                            }
                        ),
                    ]
                ),
                encoding="utf-8",
            )

            stdout = io.StringIO()
            code = ble_tool.run(
                ["--codex-home", str(codex_home), "--dry-run"],
                stdout=stdout,
                stderr=io.StringIO(),
                now_fn=lambda: 1000,
                bleak_loader=lambda: (_ for _ in ()).throw(AssertionError("should not import bleak")),
            )

        self.assertEqual(code, 0)
        payload = json.loads(stdout.getvalue().split("Payload: ", 1)[1])
        self.assertEqual(payload["s"], 9)
        self.assertEqual(payload["w"], 37)
        self.assertEqual(payload["st"], "allowed")
        self.assertEqual(payload["src"], "codex")

    def test_codex_wsl_dry_run_prefers_default_codex_limit_over_newer_spark_limit(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            codex_home = Path(tmpdir)
            session = codex_home / "sessions" / "2026" / "05" / "18" / "rollout.jsonl"
            session.parent.mkdir(parents=True)
            session.write_text(
                "\n".join(
                    [
                        json.dumps(
                            {
                                "timestamp": "2026-05-18T07:10:39.964Z",
                                "type": "event_msg",
                                "payload": {
                                    "type": "token_count",
                                    "rate_limits": {
                                        "limit_id": "codex",
                                        "primary": {"used_percent": 4, "resets_at": 1300},
                                        "secondary": {"used_percent": 3, "resets_at": 4600},
                                    },
                                },
                            }
                        ),
                        json.dumps(
                            {
                                "timestamp": "2026-05-18T07:10:39.965Z",
                                "type": "event_msg",
                                "payload": {
                                    "type": "token_count",
                                    "rate_limits": {
                                        "limit_id": "codex_bengalfox",
                                        "limit_name": "GPT-5.3-Codex-Spark",
                                        "primary": {"used_percent": 0, "resets_at": 3100},
                                        "secondary": {"used_percent": 0, "resets_at": 604800},
                                    },
                                },
                            }
                        ),
                    ]
                ),
                encoding="utf-8",
            )

            stdout = io.StringIO()
            code = ble_tool.run(
                ["--codex-home", str(codex_home), "--dry-run"],
                stdout=stdout,
                stderr=io.StringIO(),
                now_fn=lambda: 1000,
                bleak_loader=lambda: (_ for _ in ()).throw(AssertionError("should not import bleak")),
            )

        self.assertEqual(code, 0)
        payload = json.loads(stdout.getvalue().split("Payload: ", 1)[1])
        self.assertEqual(payload["s"], 4)
        self.assertEqual(payload["w"], 3)

    def test_read_access_token_finds_nested_token(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            credentials = Path(tmpdir) / "credentials.json"
            credentials.write_text(json.dumps({"claudeAiOauth": {"accessToken": "token-123"}}), encoding="utf-8")

            self.assertEqual(ble_tool.read_access_token(credentials), "token-123")

    def test_missing_credentials_returns_actionable_error(self):
        stderr = io.StringIO()
        code = ble_tool.run(
            ["--usage-source", "claude", "--credentials", "/no/such/file", "--dry-run"],
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

    def test_select_paired_windows_address_prefers_custom_service(self):
        infos = [
            FakeDeviceInfo("Claude Controller", r"\\?\BTHLE#Dev_111111111111#a&x&0&111111111111#{781aee18-7733}"),
            FakeDeviceInfo(
                "Claude Controller",
                r"\\?\BTHLEDevice#{4c41555a-4465-7669-6365-000000000001}_Dev_VID&0205ac_PID&820a_REV&0210_94a9901b6dfd#b&x&2&0025#{4c41555a-4465-7669-6365-000000000001}",
            ),
        ]

        address = ble_tool._select_paired_windows_address(infos, "Claude Controller", ble_tool.SERVICE_UUID)

        self.assertEqual(address, "94:A9:90:1B:6D:FD")

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

    def test_address_mode_skips_scan_and_uses_direct_ble_device(self):
        stdout = io.StringIO()
        config = ble_tool.BleConfig(address="94:A9:90:1B:6D:FD", ack_timeout=0.1)

        ok = asyncio.run(
            ble_tool.run_ble_session(
                config,
                lambda: ble_tool.build_preset_payload("high"),
                stdout,
                FailingScanner,
                FakeClient,
                FakeDirectDevice,
            )
        )

        self.assertTrue(ok)
        self.assertIsInstance(FakeClient.instances[0].target, FakeDirectDevice)
        self.assertEqual(FakeClient.instances[0].target.address, "94:A9:90:1B:6D:FD")
        self.assertIn("Using direct BLE address", stdout.getvalue())

    def test_scan_failure_uses_paired_windows_address_fallback(self):
        stdout = io.StringIO()
        config = ble_tool.BleConfig(ack_timeout=0.1)

        async def paired_resolver(device_name, service_uuid):
            return "94:A9:90:1B:6D:FD"

        ok = asyncio.run(
            ble_tool.run_ble_session(
                config,
                lambda: ble_tool.build_preset_payload("high"),
                stdout,
                EmptyScanner,
                FakeClient,
                FakeDirectDevice,
                paired_resolver,
            )
        )

        self.assertTrue(ok)
        self.assertIsInstance(FakeClient.instances[0].target, FakeDirectDevice)
        self.assertEqual(FakeClient.instances[0].target.address, "94:A9:90:1B:6D:FD")
        self.assertIn("Found paired Windows BLE address", stdout.getvalue())

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

    def test_one_shot_nack_returns_failure(self):
        FakeScanner.discovered = {
            "BB": (
                FakeDevice("BB:BB:BB:BB:BB:BB", "Claude Controller"),
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[ble_tool.SERVICE_UUID]),
            )
        }
        stdout = io.StringIO()
        config = ble_tool.BleConfig(require_ack=True, ack_timeout=0.1)

        code = asyncio.run(
            ble_tool.run_ble_with_retries(
                config,
                lambda: ble_tool.build_preset_payload("normal"),
                stdout,
                FakeScanner,
                NackClient,
                retry_delay=0,
            )
        )

        self.assertEqual(code, 1)
        self.assertIn("Device rejected payload", stdout.getvalue())

    def test_watch_nack_reconnects_and_retries(self):
        FakeScanner.discovered = {
            "BB": (
                FakeDevice("BB:BB:BB:BB:BB:BB", "Claude Controller"),
                FakeAdvertisement(local_name="Claude Controller", service_uuids=[ble_tool.SERVICE_UUID]),
            )
        }
        stdout = io.StringIO()
        config = ble_tool.BleConfig(watch=True, require_ack=True, max_writes=1, ack_timeout=0.1)

        code = asyncio.run(
            ble_tool.run_ble_with_retries(
                config,
                lambda: ble_tool.build_preset_payload("normal"),
                stdout,
                FakeScanner,
                NackThenAckClient,
                retry_delay=0,
            )
        )

        self.assertEqual(code, 0)
        self.assertEqual(len(FakeClient.instances), 2)
        self.assertIn("BLE send failed; retrying", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
