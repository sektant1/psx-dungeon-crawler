import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import visual_test


class VisualTestUnitTests(unittest.TestCase):
    def test_deterministic_environment_sets_capture_controls(self):
        env = visual_test.deterministic_environment(
            {}, frame=42, fixed_dt="0.02", seed=7, preset="ps1"
        )
        self.assertEqual(env["SDL_VIDEODRIVER"], "x11")
        self.assertEqual(env["RAVEN_FIXED_DT"], "0.02")
        self.assertEqual(env["RAVEN_GEN_SEED"], "7")
        self.assertEqual(env["RAVEN_RENDER_PRESET"], "ps1")
        self.assertEqual(env["RAVEN_SCREENSHOT_FRAME"], "42")
        self.assertNotIn("RAVEN_FULLSCREEN", env)

    def test_parse_frame_stats_returns_named_metrics(self):
        text = "[info] FrameStats: n=120 p50=2.125ms p95=4.500ms p99=6.000ms max=8.250ms"
        self.assertEqual(
            visual_test.parse_frame_stats(text),
            {
                "n": 120,
                "p50_ms": 2.125,
                "p95_ms": 4.5,
                "p99_ms": 6.0,
                "max_ms": 8.25,
            },
        )

    def test_validators_reject_empty_and_accept_signatures(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            png = root / "shot.png"
            rdc = root / "frame.rdc"
            png.write_bytes(b"\x89PNG\r\n\x1a\npayload")
            rdc.write_bytes(b"RDOCpayload")
            self.assertIsNone(visual_test.validate_png(png))
            self.assertIsNone(visual_test.validate_rdc(rdc))
            rdc.write_bytes(b"")
            self.assertEqual(visual_test.validate_rdc(rdc).code, "empty_artifact")

    def test_latest_artifact_uses_manifest_then_newest_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            older = root / "a" / "frame.rdc"
            newer = root / "b" / "frame.rdc"
            older.parent.mkdir()
            newer.parent.mkdir()
            older.write_bytes(b"RDOC-old")
            newer.write_bytes(b"RDOC-new")
            older.touch()
            newer.touch()
            manifest = root / "latest.json"
            manifest.write_text(
                json.dumps({"artifacts": [{"kind": "capture", "path": str(older)}]})
            )
            self.assertEqual(
                visual_test.latest_artifact(root, "capture"), older.resolve()
            )
            manifest.unlink()
            self.assertEqual(
                visual_test.latest_artifact(root, "capture"), newer.resolve()
            )

    def test_result_schema_has_stable_required_fields(self):
        result = visual_test.VisualResult(operation="probe")
        payload = result.as_dict()
        self.assertEqual(payload["schema"], "raven-visual-test/v1")
        for key in (
            "status",
            "operation",
            "started_at",
            "duration_seconds",
            "command",
            "environment",
            "artifacts",
            "metrics",
            "capabilities",
            "errors",
        ):
            self.assertIn(key, payload)

    @mock.patch("tools.visual_test.shutil.which", return_value=None)
    def test_display_without_probe_is_not_assumed_usable(self, _which):
        self.assertFalse(visual_test._display_usable({"DISPLAY": ":0"}))

    def test_explicit_current_display_skips_xvfb(self):
        self.assertEqual(
            visual_test._display_prefix({"DISPLAY": ":0"}, "current"), []
        )


if __name__ == "__main__":
    unittest.main()
