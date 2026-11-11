import json
import os
from pathlib import Path
import sys
import tempfile
import tracemalloc
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import codex_usage as usage


def event(percent, limit_id="codex", timestamp="2026-01-01", **extra):
    limits = {"primary": {"used_percent": percent}}
    if limit_id is not None:
        limits["limit_id"] = limit_id
    return json.dumps({"timestamp": timestamp, "payload": {
        "type": "token_count", "rate_limits": limits, **extra,
    }}, ensure_ascii=False).encode()


class ReverseUsageReaderTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.home = Path(temp.name)
        self.sessions = self.home / "sessions"
        self.sessions.mkdir()

    def write(self, name, content, age=1):
        path = self.sessions / name
        path.write_bytes(content)
        os.utime(path, ns=(age, age))
        return path

    def percent(self, **kwargs):
        return usage.read_latest_codex_rate_limits(self.home, **kwargs)["primary"]["used_percent"]

    def test_newest_file_and_last_record_win_over_timestamps(self):
        self.write("old.jsonl", event(1, timestamp="2099"), age=1)
        self.write("new.jsonl", event(2, timestamp="2098") + b"\n" + event(3), age=2)
        self.assertEqual(self.percent(), 3)

    def test_success_does_not_open_older_file(self):
        old = self.write("old.jsonl", event(1), age=1)
        self.write("new.jsonl", event(2), age=2)
        original = Path.open
        def guarded(path, *args, **kwargs):
            self.assertNotEqual(path, old)
            return original(path, *args, **kwargs)
        with patch.object(Path, "open", guarded):
            self.assertEqual(self.percent(), 2)

    def test_missing_usage_falls_back_but_respects_candidate_limit(self):
        self.write("old.jsonl", event(1), age=1)
        self.write("new.jsonl", b"{}\n", age=2)
        self.assertEqual(self.percent(), 1)
        with self.assertRaises(usage.MissingCodexUsageError):
            self.percent(max_files=1)

    def test_limit_selection_and_legacy_fallback_are_per_file(self):
        path = self.write("session.jsonl", b"\n".join([
            event(1), event(2, None), event(3, None), event(4, "spark"),
        ]))
        self.assertEqual(self.percent(), 1)
        for limit_id in ("all", "", None, "spark"):
            self.assertEqual(self.percent(limit_id=limit_id), 4)
        self.assertEqual(self.percent(limit_id="other"), 3)
        path.write_bytes(event(5, None))
        self.write("older.jsonl", event(6), age=0)
        self.assertEqual(self.percent(), 5)

    def test_invalid_records_and_partial_tail_are_skipped(self):
        self.write("session.jsonl", b"\n".join([
            event(1), b"null", b"[]", b"42", b"{}", b"garbage", b"\xff",
            b'{"payload": {"type": "token_count", "rate_limits": []}}',
            b'{"payload":',
        ]))
        self.assertEqual(self.percent(), 1)

    def test_chunk_boundaries_utf8_crlf_and_missing_final_newline(self):
        path = self.write("session.jsonl", b"")
        for block_size in (1, 7, 64, usage.CODEX_READ_BLOCK_BYTES):
            for ending in (b"", b"\n", b"\r\n"):
                with self.subTest(block_size=block_size, ending=ending):
                    path.write_bytes(event(1) + b"\r\n" + event(2, note="中文") + ending)
                    with patch.object(usage, "CODEX_READ_BLOCK_BYTES", block_size):
                        self.assertEqual(self.percent(), 2)

    def test_oversized_lines_skip_to_boundary_and_recover(self):
        path = self.write("session.jsonl", b"")
        for ending in (b"", b"\n"):
            for size in (127, 128, 129, 2048):
                content = b"head\n" + b"x" * size + b"\ntail" + ending
                path.write_bytes(content)
                with patch.object(usage, "CODEX_READ_BLOCK_BYTES", 7), patch.object(usage, "CODEX_MAX_LINE_BYTES", 128):
                    with path.open("rb") as stream:
                        lines = list(usage._iter_codex_lines_reverse(stream))
                expected = [b"tail"] + ([b"x" * size] if size <= 128 else []) + [b"head"]
                self.assertEqual(lines, expected)

    def test_empty_or_entirely_oversized_file_has_no_lines(self):
        for content in (b"", b"\n\n", b"x" * 1024):
            path = self.write("session.jsonl", content)
            with patch.object(usage, "CODEX_MAX_LINE_BYTES", 128), path.open("rb") as stream:
                self.assertEqual(list(usage._iter_codex_lines_reverse(stream)), [])

    def test_missing_candidate_and_read_failure_fall_back(self):
        old = self.write("old.jsonl", event(1))
        missing = self.sessions / "missing.jsonl"
        with patch.object(usage, "_codex_session_files", return_value=[missing, old]):
            self.assertEqual(self.percent(), 1)
        new = self.write("new.jsonl", event(2), age=2)
        original = usage._iter_codex_lines_reverse
        def failed(stream):
            if stream.name == str(new):
                raise OSError("truncated")
            yield from original(stream)
        with patch.object(usage, "_iter_codex_lines_reverse", failed):
            self.assertEqual(self.percent(), 1)

    def test_stat_race_skips_disappearing_file(self):
        old = self.write("old.jsonl", event(1))
        missing = self.sessions / "missing.jsonl"
        with patch.object(Path, "rglob", return_value=iter([missing, old])):
            self.assertEqual(self.percent(), 1)

    def test_truncation_short_read_is_rejected(self):
        path = self.write("session.jsonl", event(1))
        with path.open("rb") as stream, patch.object(stream, "read", return_value=b""):
            with self.assertRaises(OSError):
                list(usage._iter_codex_lines_reverse(stream))

    def test_appends_are_seen_on_next_poll(self):
        path = self.write("session.jsonl", event(1) + b"\n")
        with path.open("rb") as stream:
            original = stream.read
            def append_after_snapshot(size):
                with path.open("ab") as writer:
                    writer.write(event(2) + b"\n")
                return original(size)
            with patch.object(stream, "read", side_effect=append_after_snapshot):
                lines = list(usage._iter_codex_lines_reverse(stream))
        self.assertEqual(lines, [event(1)])
        self.assertEqual(self.percent(), 2)

    def test_sparse_ten_gib_file_reads_only_one_tail_block(self):
        path = self.sessions / "large.jsonl"
        with path.open("wb") as stream:
            stream.seek(10 * 1024**3)
            stream.write(b"\n" + event(7) + b"\n")
        original = Path.open
        read_sizes = []
        def tracked(path, *args, **kwargs):
            stream = original(path, *args, **kwargs)
            read = stream.read
            def counted(size=-1):
                self.assertGreater(size, 0)
                read_sizes.append(size)
                return read(size)
            stream.read = counted
            return stream
        with patch.object(Path, "open", tracked):
            self.assertEqual(self.percent(), 7)
        self.assertEqual(read_sizes, [usage.CODEX_READ_BLOCK_BYTES])

    def test_large_single_line_memory_is_bounded(self):
        path = self.sessions / "large.jsonl"
        with path.open("wb") as stream:
            stream.write(event(7) + b"\n")
            chunk = b"x" * (1024 * 1024)
            for _ in range(32):
                stream.write(chunk)
        tracemalloc.start()
        try:
            self.assertEqual(self.percent(), 7)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertLess(peak, 8 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
