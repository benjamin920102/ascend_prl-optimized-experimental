"""Pure-host tests for safe GitHub Actions profile selection."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from ci_profiles import make_matrix  # noqa: E402


class TestBuildProfiles(unittest.TestCase):
    def test_baseline(self):
        cfg = make_matrix("baseline")["include"][0]
        self.assertEqual((cfg["rank"], cfg["mbatch"], cfg["hit_epoch"]), (128, 1, 0))

    def test_experiments(self):
        cfgs = make_matrix("matrix")["include"]
        self.assertEqual([p["id"] for p in cfgs], ["baseline", "mbatch8_legacy", "mbatch8"])
        self.assertEqual([p["mbatch"] for p in cfgs], [1, 8, 8])
        self.assertEqual([p["hit_epoch"] for p in cfgs], [0, 0, 1])

    def test_custom(self):
        p = make_matrix("custom", dict(mbatch="4", bufs="4", vec2x="2", backoff="500", basen="128", hit_epoch="1"))
        self.assertEqual(p["include"][0]["mbatch"], 4)

    def test_reject_incompatible_flags(self):
        with self.assertRaises(ValueError):
            make_matrix("custom", dict(mbatch="8", bufs="3", vec2x="2", backoff="500", basen="128", hit_epoch="1"))

    def test_reject_invalid_mtile(self):
        with self.assertRaises(ValueError):
            make_matrix("custom", dict(mbatch="1", bufs="2", vec2x="2", backoff="500", basen="256", hit_epoch="1"))

    def test_reject_shell_injection(self):
        with self.assertRaises(ValueError):
            make_matrix("custom", dict(mbatch="8; rm -rf /", bufs="2", vec2x="2", backoff="500", basen="128", hit_epoch="1"))

    def test_reject_bad_name(self):
        with self.assertRaises(ValueError):
            make_matrix("unknown")


if __name__ == "__main__":
    unittest.main()
