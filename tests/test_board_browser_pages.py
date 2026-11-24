import importlib.util
from pathlib import Path
import tempfile
import unittest

path = Path(__file__).resolve().parents[1] / "tools/check-browser-pages.py"
spec = importlib.util.spec_from_file_location("browser_pages", path)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class BrowserPagePackaging(unittest.TestCase):
    def check_source(self, source):
        with tempfile.TemporaryDirectory() as directory:
            page = Path(directory) / "page.html"
            page.write_text(f'<script type="module">{source}</script>')
            return checker.check(page)

    def test_duplicate_helper_blocks_module_startup(self):
        with self.assertRaisesRegex(ValueError, "already been declared"):
            self.check_source("function crc32(){}; function crc32(){}")

    def test_private_helpers_can_share_names_without_colliding(self):
        self.assertEqual(self.check_source(
            "function crc32(){}; const pack=(()=>{function crc32(){}; return crc32;})();"), 1)

    def test_truncated_module_is_rejected(self):
        with self.assertRaises(ValueError):
            self.check_source("const broken =")


if __name__ == "__main__":
    unittest.main()
