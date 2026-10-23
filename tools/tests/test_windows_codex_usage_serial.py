import io
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import windows_codex_usage_serial as serial_tool


PAYLOAD = json.dumps({"s": 23, "w": 42, "ok": True, "src": "codex", "cfg": {
    "model": "example", "plan_model": "example", "service_tier": "fast",
}})
STATUS = b"XDBG STATUS source=serial payload=valid model=example plan_model=example service_tier=fast\n"


class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.closed = False
        self.lines = [b"Usage update: session=23\n", STATUS]
        self.writes = []

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        pass

    def readline(self):
        return self.lines.pop(0) if self.lines else b""


class CodexSerialTest(unittest.TestCase):
    def test_write_and_verify_then_release_port(self):
        port = FakeSerial()
        status = serial_tool.send_snapshot(PAYLOAD, "COM3", 115200, 1, lambda *a, **k: port, lambda _: None)
        self.assertEqual(status["payload"], "valid")
        self.assertTrue(port.closed)
        self.assertFalse(port.dtr)
        self.assertFalse(port.rts)
        self.assertEqual(port.writes, [PAYLOAD.encode() + b"\nXDBG STATUS\n"])

    def test_mismatch_and_timeout_release_port(self):
        for lines in ([STATUS.replace(b"example", b"other")], []):
            port = FakeSerial()
            port.lines = lines
            with self.assertRaises((RuntimeError, TimeoutError)):
                serial_tool.send_snapshot(PAYLOAD, "COM3", 115200, 1, lambda *a, **k: port, lambda _: None)
            self.assertTrue(port.closed)

    def test_config_only_packet_can_be_verified_without_usage(self):
        port = FakeSerial()
        port.lines = [STATUS.replace(b"payload=valid", b"payload=none")]
        payload = json.dumps({"src": "codex", "cfg": json.loads(PAYLOAD)["cfg"]})
        status = serial_tool.send_snapshot(payload, "COM3", 115200, 1, lambda *a, **k: port, lambda _: None)
        self.assertEqual(status["payload"], "none")

    def test_oversized_packet_never_opens_port(self):
        with self.assertRaises(ValueError):
            serial_tool.send_snapshot("x" * 512, "COM3", 115200, 1, lambda *a, **k: self.fail("opened"))

    def test_dry_run_never_opens_port(self):
        output = io.StringIO()
        result = serial_tool.run(["--dry-run"], stdout=output, serial_factory=lambda *a, **k: self.fail("opened"), payload_provider=lambda: PAYLOAD)
        self.assertEqual(result, 0)
        self.assertIn("example", output.getvalue())

    def test_watch_retries_busy_port_then_closes_before_waiting(self):
        port = FakeSerial()
        attempts = []
        delays = []
        def factory(*a, **k):
            attempts.append(1)
            if len(attempts) == 1:
                raise OSError("busy")
            return port
        def sleep(delay):
            delays.append(delay)
            if delay == 60:
                self.assertTrue(port.closed)
                raise KeyboardInterrupt()
        result = serial_tool.run(["--watch"], stdout=io.StringIO(), stderr=io.StringIO(), serial_factory=factory, sleep_fn=sleep, payload_provider=lambda: PAYLOAD)
        self.assertEqual(result, 0)
        self.assertEqual(len(attempts), 2)
        self.assertEqual(delays, [5, 0.4, 60])

    def test_single_failure_returns_nonzero(self):
        result = serial_tool.run([], stdout=io.StringIO(), stderr=io.StringIO(), serial_factory=lambda *a, **k: (_ for _ in ()).throw(OSError("disconnected")), payload_provider=lambda: PAYLOAD)
        self.assertEqual(result, 1)


if __name__ == "__main__":
    unittest.main()
