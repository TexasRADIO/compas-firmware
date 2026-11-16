"""Compile and exercise the excerpt outside the production score registry."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BoardPiano(unittest.TestCase):
    def test_cielito_full_piano_preserves_rolls_ties_and_final_chord(self):
        self.compile_and_run([{
            "backing": "cielito-lindo-songbag-left-hand.json",
            "right_hand": "cielito-lindo-songbag-right-hand.json",
            "guide": "cielito-lindo-songbag-canto.json",
            "form": [{"name": name, "bar": bar} for name, bar in
                     zip(("INTRO", "VERSE", "REFRAIN", "CODA"), (1, 2, 18, 32))],
        }], test_file="piano-cielito-full.c")

    def test_cielito_three_beat_score_with_separate_canto(self):
        # Test-only form anchors: pickup, first full bar, chorus, final cadence.
        # This does not register the unfinished full-piano arrangement on board.
        self.compile_and_run([{
            "backing": "cielito-lindo-songbag-left-hand.json",
            "guide": "cielito-lindo-songbag-canto.json",
            "form": [{"name": name, "bar": bar} for name, bar in
                     zip(("INTRO", "VERSE", "REFRAIN", "CODA"), (1, 2, 18, 32))],
        }], test_file="piano-cielito.c")

    def compile_and_run(self, registry, *, excerpt=False, test_file="piano-voices.c"):
        with tempfile.TemporaryDirectory(prefix="compas-score-") as temp:
            folder = Path(temp)
            config = folder / "registry.json"
            config.write_text(json.dumps(registry))
            subprocess.run([sys.executable, "-B", str(ROOT / "tools/board-piano.py"),
                            "--registry", str(config), "--output", str(folder / "piano.inc")], check=True)
            cc = shlex.split(os.environ.get("CC", "cc"))
            test = folder / "test"
            flags = ["-DTEST_KEMPTON_EXCERPT"] if excerpt else []
            subprocess.run(cc + flags + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                 "-I" + temp, str(ROOT / "tests" / test_file),
                                 "-o", str(test)], check=True)
            subprocess.run([str(test)], check=True)

    def test_score_transpose_pitch_and_printed_pcm(self):
        self.compile_and_run(json.loads((ROOT / "repertoire/board-scores.json").read_text()),
                             test_file="score-transpose.c")

    def test_kempton_independent_releases_and_bar_jump(self):
        registry = [{
            "backing": "el-manisero-kempton-calls.json",
            "guide_part": 0,
            "form": [{"name": name, "bar": bar} for name, bar in
                     zip(("INTRO", "VERSE", "REFRAIN", "CODA"), (1, 3, 5, 7))],
        }]
        self.compile_and_run(registry, excerpt=True)

    def test_complete_kempton_guide_releases_after_long_tie(self):
        self.compile_and_run(json.loads((ROOT / "repertoire/board-scores.json").read_text()))


if __name__ == "__main__":
    unittest.main()
