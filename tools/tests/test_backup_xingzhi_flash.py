import io
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


TOOL_PATH = Path(__file__).resolve().parents[1] / "backup_xingzhi_flash.py"
SPEC = importlib.util.spec_from_file_location("backup_xingzhi_flash", TOOL_PATH)
backup_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = backup_tool
SPEC.loader.exec_module(backup_tool)


class FakeRunner:
    def __init__(self, handler):
        self.handler = handler
        self.calls = []

    def __call__(self, args):
        self.calls.append(list(args))
        return self.handler(args)


class BackupXingzhiFlashTest(unittest.TestCase):
    def test_parse_size_accepts_hex_and_megabytes(self):
        self.assertEqual(backup_tool.parse_size("0x800000"), 8 * 1024 * 1024)
        self.assertEqual(backup_tool.parse_size("8MB"), 8 * 1024 * 1024)
        self.assertEqual(backup_tool.parse_size("512KB"), 512 * 1024)

    def test_parse_detected_flash_size_from_esptool_output(self):
        output = """
        Detecting chip type... ESP32-S3
        Detected flash size: 8MB
        """
        self.assertEqual(
            backup_tool.parse_detected_flash_size(output),
            8 * 1024 * 1024,
        )

    def test_dry_run_prints_commands_without_creating_output(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "backup.bin"

            def handler(args):
                self.fail(f"runner should not be called in dry-run with explicit size: {args}")

            code = backup_tool.run(
                ["--port", "COM7", "--size", "1KB", "--output", str(output), "--dry-run"],
                runner=FakeRunner(handler),
                stdout=io.StringIO(),
                stderr=io.StringIO(),
            )

            self.assertEqual(code, 0)
            self.assertFalse(output.exists())

    def test_successful_backup_detects_size_and_verifies_file(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "backup.bin"

            def handler(args):
                if args[-1] == "flash-id":
                    return backup_tool.CommandResult(args, 0, "Detected flash size: 1KB\n", "")
                output.write_bytes(b"\xAA" * 1024)
                return backup_tool.CommandResult(args, 0, "Read 1024 bytes\n", "")

            runner = FakeRunner(handler)
            code = backup_tool.run(
                ["--port", "COM7", "--output", str(output)],
                runner=runner,
                stdout=io.StringIO(),
                stderr=io.StringIO(),
            )

            self.assertEqual(code, 0)
            self.assertEqual(output.stat().st_size, 1024)
            self.assertEqual(len(runner.calls), 2)
            self.assertIn("read-flash", runner.calls[1])

    def test_chunked_backup_combines_parts(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "backup.bin"

            def handler(args):
                if args[-1] == "flash-id":
                    return backup_tool.CommandResult(args, 0, "Detected flash size: 2KB\n", "")
                start = int(args[-3], 16)
                size = int(args[-2], 16)
                Path(args[-1]).write_bytes(bytes([start // 1024]) * size)
                return backup_tool.CommandResult(args, 0, f"Read {size} bytes\n", "")

            code = backup_tool.run(
                [
                    "--port",
                    "COM7",
                    "--output",
                    str(output),
                    "--chunk-size",
                    "1KB",
                ],
                runner=FakeRunner(handler),
                stdout=io.StringIO(),
                stderr=io.StringIO(),
            )

            self.assertEqual(code, 0)
            self.assertEqual(output.stat().st_size, 2048)
            self.assertEqual(output.read_bytes(), (b"\x00" * 1024) + (b"\x01" * 1024))

    def test_chunked_backup_reuses_existing_chunk_dir(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "backup.bin"
            chunk_dir = output.parent / f".{output.name}.chunks"
            chunk_dir.mkdir()
            (chunk_dir / "part-00000000.bin").write_bytes(b"\x00" * 1024)

            def handler(args):
                if args[-1] == "flash-id":
                    return backup_tool.CommandResult(args, 0, "Detected flash size: 2KB\n", "")
                start = int(args[-3], 16)
                size = int(args[-2], 16)
                Path(args[-1]).write_bytes(bytes([start // 1024]) * size)
                return backup_tool.CommandResult(args, 0, f"Read {size} bytes\n", "")

            runner = FakeRunner(handler)
            stdout = io.StringIO()
            code = backup_tool.run(
                [
                    "--port",
                    "COM7",
                    "--output",
                    str(output),
                    "--chunk-size",
                    "1KB",
                ],
                runner=runner,
                stdout=stdout,
                stderr=io.StringIO(),
            )

            self.assertEqual(code, 0)
            self.assertEqual(output.read_bytes(), (b"\x00" * 1024) + (b"\x01" * 1024))
            self.assertFalse(chunk_dir.exists())
            read_calls = [call for call in runner.calls if "read-flash" in call]
            self.assertEqual(len(read_calls), 1)
            self.assertEqual(read_calls[0][-3:-1], ["0x400", "0x400"])
            self.assertIn("Reusing cached chunk 0x0+0x400", stdout.getvalue())

    def test_chunked_backup_retries_failed_part(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "backup.bin"
            failed_once = {"value": False}

            def handler(args):
                if args[-1] == "flash-id":
                    return backup_tool.CommandResult(args, 0, "Detected flash size: 1KB\n", "")
                start = int(args[-3], 16)
                size = int(args[-2], 16)
                if start == 512 and not failed_once["value"]:
                    failed_once["value"] = True
                    Path(args[-1]).write_bytes(b"short")
                    return backup_tool.CommandResult(args, 0, "Read partial bytes\n", "")
                Path(args[-1]).write_bytes(bytes([start // 512]) * size)
                return backup_tool.CommandResult(args, 0, f"Read {size} bytes\n", "")

            runner = FakeRunner(handler)
            code = backup_tool.run(
                [
                    "--port",
                    "COM7",
                    "--output",
                    str(output),
                    "--chunk-size",
                    "512",
                    "--retries",
                    "1",
                ],
                runner=runner,
                stdout=io.StringIO(),
                stderr=io.StringIO(),
            )

            self.assertEqual(code, 0)
            self.assertTrue(failed_once["value"])
            self.assertEqual(
                output.read_bytes(),
                (b"\x00" * 512) + (b"\x01" * 512),
            )

    def test_chunked_backup_splits_failed_part(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "backup.bin"

            def handler(args):
                if args[-1] == "flash-id":
                    return backup_tool.CommandResult(args, 0, "Detected flash size: 2KB\n", "")
                start = int(args[-3], 16)
                size = int(args[-2], 16)
                if start == 0 and size == 1024:
                    return backup_tool.CommandResult(args, 1, "", "full chunk failed")
                Path(args[-1]).write_bytes(bytes([start // 512]) * size)
                return backup_tool.CommandResult(args, 0, f"Read {size} bytes\n", "")

            code = backup_tool.run(
                [
                    "--port",
                    "COM7",
                    "--output",
                    str(output),
                    "--chunk-size",
                    "1KB",
                    "--min-chunk-size",
                    "512",
                    "--retries",
                    "0",
                ],
                runner=FakeRunner(handler),
                stdout=io.StringIO(),
                stderr=io.StringIO(),
            )

            self.assertEqual(code, 0)
            self.assertEqual(
                output.read_bytes(),
                (b"\x00" * 512) + (b"\x01" * 512) + (b"\x02" * 1024),
            )

    def test_backup_fails_when_file_size_is_wrong(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "backup.bin"

            def handler(args):
                if args[-1] == "flash-id":
                    return backup_tool.CommandResult(args, 0, "Detected flash size: 1KB\n", "")
                output.write_bytes(b"too short")
                return backup_tool.CommandResult(args, 0, "Read some bytes\n", "")

            code = backup_tool.run(
                ["--port", "COM7", "--output", str(output)],
                runner=FakeRunner(handler),
                stdout=io.StringIO(),
                stderr=io.StringIO(),
            )

            self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
