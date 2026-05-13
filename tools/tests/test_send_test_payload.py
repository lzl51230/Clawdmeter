import io
import json
from pathlib import Path
import importlib.util
import sys
import unittest


TOOL_PATH = Path(__file__).resolve().parents[1] / "send_test_payload.py"
SPEC = importlib.util.spec_from_file_location("send_test_payload", TOOL_PATH)
send_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = send_tool
SPEC.loader.exec_module(send_tool)


class FakeSerial:
    instances = []

    def __init__(self, port, baud, timeout=1):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.writes = []
        self.flushed = False
        FakeSerial.instances.append(self)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        self.flushed = True


class SendTestPayloadTest(unittest.TestCase):
    def setUp(self):
        FakeSerial.instances = []

    def test_normal_preset_contains_expected_fields(self):
        payload = json.loads(send_tool.build_payload("normal"))

        self.assertEqual(payload["s"], 42)
        self.assertEqual(payload["st"], "allowed")
        self.assertTrue(payload["ok"])
        self.assertTrue(payload["valid"])

    def test_high_preset_is_high_usage(self):
        payload = json.loads(send_tool.build_payload("high"))

        self.assertGreaterEqual(payload["s"], 80)
        self.assertGreaterEqual(payload["w"], 80)
        self.assertTrue(payload["ok"])

    def test_invalid_preset_is_error_payload(self):
        payload = json.loads(send_tool.build_payload("invalid"))

        self.assertEqual(payload["st"], "error")
        self.assertFalse(payload["ok"])
        self.assertFalse(payload["valid"])

    def test_dry_run_does_not_open_serial(self):
        code = send_tool.run(
            ["normal", "--dry-run"],
            stdout=io.StringIO(),
            stderr=io.StringIO(),
            serial_factory=FakeSerial,
        )

        self.assertEqual(code, 0)
        self.assertEqual(FakeSerial.instances, [])

    def test_send_writes_newline_terminated_payload(self):
        code = send_tool.run(
            ["high", "--port", "COM9", "--baud", "9600"],
            stdout=io.StringIO(),
            stderr=io.StringIO(),
            serial_factory=FakeSerial,
            sleep_fn=lambda _: None,
        )

        self.assertEqual(code, 0)
        self.assertEqual(len(FakeSerial.instances), 1)
        serial = FakeSerial.instances[0]
        self.assertEqual(serial.port, "COM9")
        self.assertEqual(serial.baud, 9600)
        self.assertFalse(serial.dtr)
        self.assertFalse(serial.rts)
        self.assertTrue(serial.writes[0].endswith(b"\n"))
        self.assertTrue(serial.flushed)

    def test_serial_failure_returns_error(self):
        def failing_serial(*args, **kwargs):
            raise OSError("busy")

        code = send_tool.run(
            ["normal"],
            stdout=io.StringIO(),
            stderr=io.StringIO(),
            serial_factory=failing_serial,
            sleep_fn=lambda _: None,
        )

        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
