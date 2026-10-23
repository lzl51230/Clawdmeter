import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


@unittest.skipIf(sys.platform == "win32", "macOS launcher requires a POSIX shell")
class MacOSLauncherTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="codex 中文 ' ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        launcher = Path(__file__).resolve().parents[2] / "codexWatch.command"
        self.launcher = self.root / launcher.name
        shutil.copy2(launcher, self.launcher)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        python = self.bin / "python3"
        python.write_text(f"#!{sys.executable}\n" + '''import json, os, sys
if sys.argv[1] == "-c":
    if "import serial" in sys.argv[2]:
        sys.exit(int(os.environ.get("MISSING_SERIAL", "0")))
    sys.exit(0)
print(json.dumps({"cwd": os.getcwd(), "args": sys.argv[1:]}))
sys.exit(int(os.environ.get("WATCH_EXIT", "0")))
''', encoding="utf-8")
        python.chmod(0o755)

    def launch(self, *args, **env):
        return subprocess.run(
            [str(self.launcher), *args], cwd="/", capture_output=True, text=True,
            env={**os.environ, "PATH": f"{self.bin}:/usr/bin:/bin", **env}, timeout=10,
        )

    def test_launcher_uses_own_directory_and_forwards_overrides(self):
        result = self.launch("--port", "/dev/cu.custom", "--poll-interval", "30")
        self.assertEqual(result.returncode, 0, result.stderr)
        invocation = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(invocation["cwd"], str(self.root))
        self.assertEqual(invocation["args"][:2], ["-u", "tools/windows_codex_usage_serial.py"])
        self.assertEqual(invocation["args"][-4:], ["--port", "/dev/cu.custom", "--poll-interval", "30"])
        self.assertIn("auto", invocation["args"])
        self.assertIn("--follow-device", invocation["args"])

    def test_launcher_explains_missing_serial(self):
        result = self.launch(MISSING_SERIAL="1")
        self.assertEqual(result.returncode, 1)
        self.assertIn("缺少 pyserial", result.stderr)
        self.assertIn("-m pip install --user pyserial", result.stderr)
        install_line = result.stderr.splitlines()[1]
        self.assertEqual(shlex.split(install_line)[0], str(self.bin / "python3"))

    def test_dry_run_and_help_work_without_serial(self):
        for arg in ("--dry-run", "--help"):
            with self.subTest(arg=arg):
                result = self.launch(arg, MISSING_SERIAL="1")
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_launcher_preserves_watcher_exit_code(self):
        result = self.launch(WATCH_EXIT="7")
        self.assertEqual(result.returncode, 7)
