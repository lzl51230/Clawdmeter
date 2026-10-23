import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import codex_usage as usage


class CodexLocalUsageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)

    def config(self, text):
        (self.home / "config.toml").write_text(text, encoding="utf-8")

    def event(self, percent=23):
        folder = self.home / "sessions"
        folder.mkdir(exist_ok=True)
        (folder / "session.jsonl").write_text(json.dumps({
            "timestamp": "2026-10-23T00:00:00Z", "type": "event_msg",
            "payload": {"type": "token_count", "rate_limits": {
                "limit_id": "codex", "primary": {"used_percent": percent},
                "secondary": {"used_percent": 42},
            }},
        }), encoding="utf-8")

    def test_local_home_never_uses_wsl(self):
        with patch.dict(os.environ, {"CODEX_HOME": str(self.home)}), patch.object(usage.subprocess, "check_output", side_effect=AssertionError("WSL invoked")):
            self.assertEqual(usage.resolve_local_codex_home(), self.home)
            self.assertEqual(usage.resolve_local_codex_home(self.home / "explicit"), self.home / "explicit")
        with patch.dict(os.environ, {}, clear=True), patch.object(usage.Path, "home", return_value=self.home):
            self.assertEqual(usage.resolve_local_codex_home(), self.home / ".codex")

    def test_settings_and_usage_share_snapshot(self):
        self.config('model = "gpt-6.1-sol"\nservice_tier = "ultrafast"\nmodel_reasoning_effort = "high"\nplan_mode_reasoning_effort = "xhigh"\n')
        self.event()
        payload = json.loads(usage.poll_codex_local_usage(self.home))
        self.assertEqual(payload["s"], 23)
        self.assertEqual(payload["w"], 42)
        self.assertEqual(payload["cfg"], {
            "model": "gpt-6.1-sol", "plan_model": "gpt-6.1-sol", "service_tier": "ultrafast",
            "reasoning_effort": "high", "plan_reasoning_effort": "xhigh",
        })

    def test_config_refresh_and_profile_overrides(self):
        self.config('model="old"\nmodel_reasoning_effort="low"\nplan_mode_reasoning_effort="high"\nprofile="work"\n[profiles.work]\nmodel="new"\nservice_tier="fast"\nmodel_reasoning_effort="medium"\nplan_mode_reasoning_effort="xhigh"\n')
        first = usage.read_codex_settings(self.home)
        self.assertEqual(first["model"], "new")
        self.assertEqual(first["plan_model"], "new")
        self.assertEqual(first["service_tier"], "fast")
        self.assertEqual(first["reasoning_effort"], "medium")
        self.assertEqual(first["plan_reasoning_effort"], "xhigh")
        self.config('model="changed"\n')
        self.assertEqual(usage.read_codex_settings(self.home)["model"], "changed")
        self.assertEqual(usage.read_codex_settings(self.home)["service_tier"], "default")
        self.assertEqual(usage.read_codex_settings(self.home)["reasoning_effort"], "")
        self.assertEqual(usage.read_codex_settings(self.home)["plan_reasoning_effort"], "")

    def test_all_speed_values_are_preserved(self):
        for tier in ("priority", "fast", "ultrafast", "default", "auto", "flex", "unexpected"):
            with self.subTest(tier=tier):
                self.config(f'service_tier="{tier}"\n')
                self.assertEqual(usage.read_codex_settings(self.home)["service_tier"], tier)

    def test_missing_usage_sends_only_settings(self):
        self.config('model="example"\n')
        payload = json.loads(usage.poll_codex_local_usage(self.home))
        self.assertNotIn("ok", payload)
        self.assertNotIn("s", payload)
        self.assertEqual(payload["cfg"]["model"], "example")

    def test_missing_or_invalid_config_does_not_hide_usage(self):
        self.event()
        for text in (None, 'model = [broken'):
            with self.subTest(text=text):
                if text is not None:
                    self.config(text)
                warnings = []
                payload = json.loads(usage.poll_codex_local_usage(self.home, on_warning=warnings.append))
                self.assertEqual(payload["s"], 23)
                self.assertEqual(payload["cfg"]["service_tier"], "")
                self.assertEqual(payload["cfg"]["reasoning_effort"], "")
                self.assertEqual(payload["cfg"]["plan_reasoning_effort"], "")
                self.assertTrue(warnings)

    def test_payload_and_model_lengths_are_bounded(self):
        self.config('model="' + 'm' * 1000 + '"\nservice_tier="ultrafast"\nmodel_reasoning_effort="' + 'x' * 1000 + '"\nplan_mode_reasoning_effort="' + 'x' * 1000 + '"\n')
        self.event()
        payload = usage.poll_codex_local_usage(self.home)
        self.assertLess(len(payload.encode()), 512)
        self.assertEqual(len(json.loads(payload)["cfg"]["model"]), 63)
        self.assertEqual(len(json.loads(payload)["cfg"]["reasoning_effort"]), 15)
        self.assertEqual(len(json.loads(payload)["cfg"]["plan_reasoning_effort"]), 15)
        self.assertEqual(usage._display_token("bad name"), "")
        self.assertEqual(usage._display_token(1), "")

    def test_missing_plan_effort_is_unknown_not_inherited(self):
        self.config('model_reasoning_effort="high"\n')
        settings = usage.read_codex_settings(self.home)
        self.assertEqual(settings["reasoning_effort"], "high")
        self.assertEqual(settings["plan_reasoning_effort"], "")

    def test_fractional_percentage_is_not_treated_as_fraction_of_100(self):
        payload = json.loads(usage.build_payload_from_codex_rate_limits({"primary": {"used_percent": 0.5}}))
        self.assertEqual(payload["s"], 0.5)
        self.assertIsNone(payload["w"])

    def test_single_weekly_window_is_not_displayed_as_session(self):
        payload = json.loads(usage.build_payload_from_codex_rate_limits({
            "primary": {"used_percent": 11, "window_minutes": 10080, "resets_at": 4600},
            "secondary": None,
        }, now=1000))
        self.assertIsNone(payload["s"])
        self.assertEqual(payload["sr"], -1)
        self.assertEqual(payload["w"], 11)
        self.assertEqual(payload["wr"], 60)


if __name__ == "__main__":
    unittest.main()
