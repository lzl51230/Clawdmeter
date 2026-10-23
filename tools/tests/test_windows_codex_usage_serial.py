import io
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import windows_codex_usage_serial as serial_tool


PAYLOAD = json.dumps({"s": 23, "w": 42, "ok": True, "src": "codex", "cfg": {
    "model": "example", "plan_model": "example", "service_tier": "fast",
    "reasoning_effort": "high", "plan_reasoning_effort": "xhigh",
}})
STATUS = b"XDBG STATUS source=serial payload=valid model=example plan_model=example service_tier=fast reasoning_effort=high plan_reasoning_effort=xhigh\n"


class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.closed = False
        self.lines = [b"Usage update: session=23\n", STATUS]
        self.writes = []
        self.flushed = False

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def flush(self):
        self.flushed = True

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
        self.assertEqual(port.write_timeout, 1)
        self.assertFalse(port.flushed)
        self.assertEqual(port.writes, [PAYLOAD.encode() + b"\nXDBG STATUS\n"])

    def test_short_write_releases_port(self):
        port = FakeSerial()
        port.write = lambda data: len(data) - 1
        with self.assertRaisesRegex(TimeoutError, "Incomplete serial write"):
            serial_tool.send_snapshot(PAYLOAD, "COM3", 115200, 1, lambda *a, **k: port, lambda _: None)
        self.assertTrue(port.closed)

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

    def test_wrong_effort_is_rejected_and_port_is_released(self):
        port = FakeSerial()
        port.lines = [STATUS.replace(b"plan_reasoning_effort=xhigh", b"plan_reasoning_effort=high")]
        with self.assertRaisesRegex(RuntimeError, "plan_reasoning_effort"):
            serial_tool.send_snapshot(PAYLOAD, "COM3", 115200, 1, lambda *a, **k: port, lambda _: None)
        self.assertTrue(port.closed)

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
            if len(attempts) > 3:
                raise KeyboardInterrupt()
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

    @staticmethod
    def device(port="COM3", serial_number="target", location="usb-1"):
        return SimpleNamespace(device=port, vid=0x303A, pid=0x1001,
                               serial_number=serial_number, location=location)

    def run_follow_watch(self, port_sets, make_connection=None):
        scans = iter(port_sets)
        clock = [0.0]
        attempts = []
        connections = []
        payloads = []
        closed_at_wait = []
        output, errors = io.StringIO(), io.StringIO()

        def enumerate_ports():
            try:
                ports = next(scans)
            except StopIteration:
                raise KeyboardInterrupt()
            if isinstance(ports, Exception):
                raise ports
            return ports

        def factory(port, *args, **kwargs):
            attempts.append((clock[0], port))
            connection = make_connection(len(attempts)) if make_connection else FakeSerial()
            connections.append(connection)
            return connection

        def sleep(delay):
            if delay != 0.4:
                closed_at_wait.append(all(port.closed for port in connections))
            clock[0] += delay

        def payload_provider():
            payload = json.loads(PAYLOAD)
            payload["s"] += len(payloads)
            payloads.append(json.dumps(payload))
            return payloads[-1]

        result = serial_tool.run(
            ["--watch", "--follow-device"], stdout=output, stderr=errors,
            serial_factory=factory, sleep_fn=sleep, payload_provider=payload_provider,
            port_provider=enumerate_ports, monotonic_fn=lambda: clock[0],
        )
        self.assertEqual(result, 0)
        self.assertTrue(all(closed_at_wait))
        return SimpleNamespace(attempts=attempts, connections=connections,
                               payloads=payloads, output=output.getvalue(), errors=errors.getvalue())

    def test_follow_waits_for_device_at_start(self):
        result = self.run_follow_watch([[], [], [self.device()]])
        self.assertEqual(result.attempts, [(10.0, "COM3")])
        self.assertEqual(len(result.payloads), 1)
        self.assertIn("retrying in 5", result.errors)

    def test_follow_reconnects_during_normal_wait_with_fresh_payload(self):
        result = self.run_follow_watch([[self.device()], [], [], [self.device()]])
        self.assertEqual([port for _, port in result.attempts], ["COM3", "COM3"])
        self.assertLess(result.attempts[1][0], 60)
        self.assertNotEqual(result.payloads[0], result.payloads[1])
        self.assertEqual(result.connections[1].writes, [result.payloads[1].encode() + b"\nXDBG STATUS\n"])

    def test_follow_changed_port_ignores_other_device_on_old_port(self):
        other = self.device(serial_number="other")
        result = self.run_follow_watch([[self.device()], [other], [other, self.device("COM8")]])
        self.assertEqual([port for _, port in result.attempts], ["COM3", "COM8"])
        self.assertIn("COM3 -> COM8", result.output)

    def test_follow_port_change_without_observing_absence_sends_immediately(self):
        result = self.run_follow_watch([[self.device()], [self.device("COM8")]])
        self.assertEqual([port for _, port in result.attempts], ["COM3", "COM8"])
        self.assertAlmostEqual(result.attempts[1][0], 5.4)

    def test_follow_recovers_from_port_enumeration_error(self):
        result = self.run_follow_watch([[self.device()], OSError("USB enumeration failed"), [self.device()]])
        self.assertEqual(len(result.attempts), 2)
        self.assertIn("USB enumeration failed", result.errors)

    def test_follow_can_be_interrupted_while_waiting_for_absent_device(self):
        def sleep(delay):
            raise KeyboardInterrupt()

        result = serial_tool.run(
            ["--watch", "--follow-device"], stdout=io.StringIO(), stderr=io.StringIO(),
            port_provider=lambda: [], sleep_fn=sleep,
        )
        self.assertEqual(result, 0)

    def test_follow_ambiguous_identity_waits_until_unique(self):
        result = self.run_follow_watch([
            [self.device()], [self.device("COM8"), self.device("COM9")], [self.device("COM8")],
        ])
        self.assertEqual([port for _, port in result.attempts], ["COM3", "COM8"])
        self.assertIn("Multiple serial ports", result.errors)

    def test_follow_without_serial_number_matches_usb_location(self):
        result = self.run_follow_watch([
            [self.device(serial_number=None)],
            [self.device("COM8", serial_number=None, location="usb-2")],
            [self.device("COM9", serial_number=None)],
        ])
        self.assertEqual([port for _, port in result.attempts], ["COM3", "COM9"])

    def test_follow_without_stable_identity_waits_on_original_port(self):
        result = self.run_follow_watch([
            [self.device(serial_number=None, location=None)],
            [self.device("COM8", serial_number=None, location=None)],
            [self.device(serial_number=None, location=None)],
        ])
        self.assertEqual([port for _, port in result.attempts], ["COM3", "COM3"])

    def test_follow_healthy_device_keeps_normal_update_interval(self):
        result = self.run_follow_watch([[self.device()]] * 14)
        self.assertEqual(len(result.attempts), 2)
        self.assertAlmostEqual(result.attempts[1][0] - result.attempts[0][0], 60.4)

    def test_follow_retries_open_write_and_read_failures(self):
        for stage in ("open", "write", "read"):
            with self.subTest(stage=stage):
                def fail(*args):
                    raise TimeoutError("device disconnected")

                def make_connection(attempt):
                    if attempt < 3 and stage == "open":
                        fail()
                    port = FakeSerial()
                    if attempt < 3:
                        if stage == "write":
                            port.write = fail
                        elif stage == "read":
                            port.readline = fail
                    return port

                result = self.run_follow_watch([[self.device()]] * 3, make_connection)
                self.assertEqual(len(result.attempts), 3)
                self.assertTrue(all(port.closed for port in result.connections))
                self.assertIn("Serial update verified", result.output)

    def test_follow_can_be_disabled_and_dry_run_never_enumerates(self):
        for args in (["--watch", "--follow-device", "--no-follow-device"],
                     ["--dry-run", "--watch", "--follow-device"],
                     ["--follow-device"]):
            with self.subTest(args=args):
                enumerated = []

                def enumerate_ports():
                    enumerated.append(True)
                    return []

                def sleep(delay):
                    if delay != 0.4:
                        raise KeyboardInterrupt()

                result = serial_tool.run(
                    args, stdout=io.StringIO(), stderr=io.StringIO(),
                    serial_factory=lambda *a, **k: FakeSerial(), sleep_fn=sleep,
                    payload_provider=lambda: PAYLOAD,
                    port_provider=enumerate_ports,
                )
                self.assertEqual(result, 0)
                self.assertEqual(enumerated, [])


if __name__ == "__main__":
    unittest.main()
