import io
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


TOOL_PATH = Path(__file__).resolve().parents[1] / "xingzhi_debug.py"
SPEC = importlib.util.spec_from_file_location("xingzhi_debug", TOOL_PATH)
xingzhi_debug = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = xingzhi_debug
SPEC.loader.exec_module(xingzhi_debug)


class FakeSerial:
    def __init__(self, port, baud, timeout=1):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.writes = []
        self.flushed = False
        self.closed = False
        self.read_buffer = bytearray()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.closed = True
        return False

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        self.flushed = True

    def readline(self):
        marker = self.read_buffer.find(b"\n")
        if marker < 0:
            data = bytes(self.read_buffer)
            self.read_buffer.clear()
            return data
        data = bytes(self.read_buffer[: marker + 1])
        del self.read_buffer[: marker + 1]
        return data

    def read(self, size):
        data = bytes(self.read_buffer[:size])
        del self.read_buffer[:size]
        return data

    def reset_input_buffer(self):
        pass


class XingzhiDebugToolTest(unittest.TestCase):
    def test_parse_status_line_extracts_key_values(self):
        status = xingzhi_debug.parse_status_line(
            "boot log\n"
            "XDBG STATUS target=xingzhi_parity screen=usage width=240 height=240 "
            "source=ble ble=advertising ble_name=Claude_Controller hid=unavailable "
            "power=valid battery=100 charging=1 adc=2448/2451 samples=3"
        )

        self.assertEqual(status["target"], "xingzhi_parity")
        self.assertEqual(status["screen"], "usage")
        self.assertEqual(status["width"], "240")
        self.assertEqual(status["source"], "ble")
        self.assertEqual(status["ble_name"], "Claude_Controller")
        self.assertEqual(status["hid"], "unavailable")
        self.assertEqual(status["power"], "valid")
        self.assertEqual(status["battery"], "100")

    def test_parse_screenshot_start_reads_frame_metadata(self):
        metadata = xingzhi_debug.parse_screenshot_start(
            "XDBG SCREENSHOT_START width=240 height=240 format=RGB565LE bytes=115200"
        )

        self.assertEqual(metadata.width, 240)
        self.assertEqual(metadata.height, 240)
        self.assertEqual(metadata.pixel_format, "RGB565LE")
        self.assertEqual(metadata.byte_count, 115200)

    def test_read_screenshot_response_ignores_boot_logs(self):
        frame = b"\x00\xf8" + b"\xe0\x07"
        serial_port = FakeSerial("COM9", 115200)
        serial_port.read_buffer.extend(
            b"booting\n"
            b"XDBG SCREENSHOT_START width=2 height=1 format=RGB565LE bytes=4\n"
            + frame
            + b"\nXDBG SCREENSHOT_END\n"
        )

        metadata, pixels = xingzhi_debug.read_screenshot_response(serial_port)

        self.assertEqual(metadata.width, 2)
        self.assertEqual(metadata.height, 1)
        self.assertEqual(pixels, frame)

    def test_rgb565le_to_rgb888_converts_primary_colors(self):
        rgb = xingzhi_debug.rgb565le_to_rgb888(b"\x00\xf8\xe0\x07\x1f\x00")

        self.assertEqual(rgb, bytes([255, 0, 0, 0, 255, 0, 0, 0, 255]))

    def test_write_ppm_writes_binary_ppm(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "shot.ppm"
            xingzhi_debug.write_ppm(output, 1, 1, bytes([255, 0, 0]))

            self.assertTrue(output.read_bytes().startswith(b"P6\n1 1\n255\n"))

    def test_status_command_writes_debug_command(self):
        serial_port = FakeSerial("COM9", 115200)
        serial_port.read_buffer.extend(
            b"noise\nXDBG STATUS target=xingzhi_parity screen=usage width=240 height=240\n"
        )

        code = xingzhi_debug.run(
            ["status", "--port", "COM9"],
            stdout=io.StringIO(),
            stderr=io.StringIO(),
            serial_factory=lambda *args, **kwargs: serial_port,
            sleep_fn=lambda _: None,
        )

        self.assertEqual(code, 0)
        self.assertEqual(serial_port.writes[0], b"XDBG STATUS\n")
        self.assertTrue(serial_port.flushed)

    def test_button_command_writes_simulated_action(self):
        serial_port = FakeSerial("COM9", 115200)
        serial_port.read_buffer.extend(
            b"XDBG ACTION ok=1 action=cycle event=click screen=status count=1\n"
        )
        stdout = io.StringIO()

        code = xingzhi_debug.run(
            ["button", "cycle", "--event", "click", "--port", "COM9"],
            stdout=stdout,
            stderr=io.StringIO(),
            serial_factory=lambda *args, **kwargs: serial_port,
            sleep_fn=lambda _: None,
        )

        self.assertEqual(code, 0)
        self.assertEqual(serial_port.writes[0], b"XDBG BUTTON cycle click\n")
        self.assertIn("screen=status", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
