"""Pure-host tests for safe GitHub Actions profile selection."""
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from ci_profiles import make_matrix  # noqa: E402


class TestBuildProfiles(unittest.TestCase):
    def test_cli_defaults(self):
        script = Path(__file__).resolve().parents[1] / "scripts" / "ci_profiles.py"
        env = {k: v for k, v in os.environ.items()
               if not k.startswith("INPUT_") and k != "GITHUB_OUTPUT"}
        for profile in (None, "custom"):
            with self.subTest(profile=profile):
                if profile:
                    env["INPUT_PROFILE"] = profile
                output = subprocess.check_output([sys.executable, str(script)], env=env, text=True)
                cfg = json.loads(output)["include"][0]
                self.assertEqual(cfg["id"], profile or "default")
                self.assertEqual(
                    (cfg["bufs"], cfg["vec2x"], cfg["backoff"], cfg["hit_epoch"]),
                    (4, 2, 100, 1))

    def test_matched_epoch_comparison(self):
        legacy = make_matrix("legacy")["include"][0]
        epoch = make_matrix("epoch")["include"][0]
        self.assertEqual({k: v for k, v in legacy.items() if k not in ("id", "hit_epoch")},
                         {k: v for k, v in epoch.items() if k not in ("id", "hit_epoch")})

    def test_baseline(self):
        cfg = make_matrix("baseline")["include"][0]
        self.assertEqual((cfg["rank"], cfg["hit_epoch"]), (128, 0))

    def test_experiments(self):
        cfgs = make_matrix("matrix")["include"]
        self.assertEqual([p["id"] for p in cfgs], ["baseline", "legacy", "epoch"])
        self.assertEqual([p["hit_epoch"] for p in cfgs], [0, 0, 1])

    def test_custom(self):
        p = make_matrix("custom", dict(bufs="4", vec2x="2", backoff="100", basen="128", hit_epoch="1"))
        self.assertEqual(p["include"][0]["bufs"], 4)

    def test_reject_incompatible_flags(self):
        with self.assertRaises(ValueError):
            make_matrix("custom", dict(bufs="3", vec2x="2", backoff="100", basen="128", hit_epoch="1"))

    def test_reject_invalid_mtile(self):
        with self.assertRaises(ValueError):
            make_matrix("custom", dict(bufs="2", vec2x="2", backoff="100", basen="256", hit_epoch="1"))

    def test_reject_shell_injection(self):
        with self.assertRaises(ValueError):
            make_matrix("custom", dict(bufs="2; rm -rf /", vec2x="2", backoff="100", basen="128", hit_epoch="1"))

    def test_reject_bad_name(self):
        with self.assertRaises(ValueError):
            make_matrix("unknown")


if __name__ == "__main__":
    unittest.main()
