"""Virtual shows must be accepted by the same CPS4 journal layout as firmware."""
from pathlib import Path
import runpy
import struct
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
compiler = runpy.run_path(str(ROOT / "tools/board-set.py"))
reader = runpy.run_path(str(ROOT / "tools/board-show-layout.py"))


class SetFlashTest(unittest.TestCase):
    def setUp(self):
        self.records = compiler["compile_set"]({"format": 1, "songs": [
            {"title": "PARA REVIEW", "study": "carimbo-marapanim"}]})

    def test_fixture_has_complete_crc_checked_show(self):
        flash = compiler["virtual_flash"](self.records)
        generation, payload = reader["newest_show"](flash)
        self.assertEqual(generation, 1)
        self.assertEqual(len(payload), 3792)
        self.assertEqual(payload[:19], b"PARA REVIEW".ljust(19, b"\0"))
        self.assertEqual(payload[86], 127)
        for slot in range(16):
            attachment = payload[1376+slot*151:1376+(slot+1)*151]
            self.assertEqual(attachment, bytes(140)+b"\xff"+bytes(10))

    def test_release_layout_uses_own_sectors_and_no_fixture_marker(self):
        flash = compiler["virtual_flash"](self.records, unit_store=True)
        magic, generation, crc = struct.unpack_from("<4sII", flash, 0x91000)
        payload = flash[0x91100:0x91100+3792]
        self.assertEqual(magic, b"CPS4")
        self.assertEqual(zlib.crc32(struct.pack("<I", generation)+payload), crc)
        self.assertEqual(flash[:0x91000], b"\xff"*0x91000)
        self.assertEqual(flash[0x92000:], b"\xff"*(0x100000-0x92000))


if __name__ == "__main__":
    unittest.main()
