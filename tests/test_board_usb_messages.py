"""Updater receipts must tolerate telemetry without hiding corrupt replies."""
from pathlib import Path
import runpy
import unittest

API = runpy.run_path(str(Path(__file__).resolve().parents[1] / "tools/board-usb-emu.py"))
decode = API["decode_app_message"]
frame = API["wire"]["frame"]


class MixedUsbMessages(unittest.TestCase):
    def test_updater_reply_is_preserved(self):
        self.assertEqual(decode(frame(0x11, b"FM-1")), (0x11, b"FM-1"))

    def test_valid_sideman_is_separate(self):
        self.assertIsNone(decode(bytes.fromhex("f00022243501020000007f005e0000060000f7")))

    def test_realtime_is_separate(self):
        for status in (0xF8, 0xFA, 0xFB, 0xFC):
            self.assertIsNone(decode(bytes([status])))

    def test_invalid_sideman_is_not_ignored(self):
        for message in ("f0002224350102f7", "f00022243501020000007f005e0000060080f7"):
            with self.assertRaises(ValueError):
                decode(bytes.fromhex(message))

    def test_corrupt_or_unknown_reply_still_fails(self):
        bad = bytearray(frame(0x11, b"FM-1"))
        bad[-2] ^= 1
        for message in (bytes(bad), b"\xf9"):
            with self.assertRaises(SystemExit):
                decode(message)


if __name__ == "__main__":
    unittest.main()
