"""Exercise the restore plan CLI with disposable raw NOR captures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools/board-restore-plan.py"


class RestorePlanTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.flash = b"\xA5" * 0x100000
        self.baseline = self.save("baseline", self.flash)
        self.repeat = self.save("repeat", self.flash)

    def save(self, name, contents):
        path = self.root / name
        path.write_bytes(contents)
        return path

    def plan(self, *args, repeat=None):
        return subprocess.run(
            [sys.executable, str(TOOL), self.baseline, repeat or self.repeat, *args],
            capture_output=True, text=True, check=False,
        )

    def test_restore_includes_both_app_edges(self):
        current = bytearray(self.flash)
        current[0x4000] = current[0x92FFF] = 0xFF
        result = self.plan("--current", self.save("current", current),
                           "--restore-window", self.save("app", self.flash[0x4000:0x93000]))
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads(result.stdout)
        self.assertEqual([s["offset"] for s in receipt["sectors"]], [0x4000, 0x92000])
        self.assertTrue(receipt["candidateAppMatchesBaseline"])
        self.assertEqual(receipt["simulatedRestoreSHA256"], receipt["baselineSHA256"])

    def test_protected_bytes_fail_closed(self):
        for offset in (0x3FFF, 0x93000, 0xA0000, 0xE4000, 0xFC000):
            with self.subTest(offset=offset):
                current = bytearray(self.flash)
                current[offset] = 0xFF
                result = self.plan("--current", self.save("current", current))
                self.assertEqual(result.returncode, 1)
                self.assertIn("outside the app window", result.stderr)
                self.assertEqual(result.stdout, "")

    def test_requires_separate_full_matching_baselines(self):
        alias = self.root / "alias"
        os.link(self.baseline, alias)
        result = self.plan(repeat=alias)
        self.assertIn("separately saved file", result.stderr)
        for contents, message in ((self.flash[:-1], "regular 1048576-byte file"),
                                  (self.flash + b"\xff", "regular 1048576-byte file"),
                                  (self.flash[:-1] + b"\xff", "baseline captures differ")):
            result = self.plan(repeat=self.save("repeat", contents))
            self.assertEqual(result.returncode, 1)
            self.assertIn(message, result.stderr)

    def test_matching_current_has_empty_plan(self):
        result = self.plan("--current", self.save("current", self.flash))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["sectors"], [])

    def test_unmatched_candidate_is_refused(self):
        result = self.plan("--restore-window", self.save("app", b"\xff" * 0x8F000))
        self.assertEqual(result.returncode, 1)
        self.assertIn("candidate app window differs", result.stderr)


if __name__ == "__main__":
    unittest.main()
