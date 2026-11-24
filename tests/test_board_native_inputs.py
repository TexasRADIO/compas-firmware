import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('native_inputs',Path(__file__).resolve().parents[1]/'tools/board-native-inputs.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class NativeInputs(unittest.TestCase):
    def seed(self,root):
        (root/'build/board').mkdir(parents=True)
        for name in ('score.inc','score-test.inc','panel-score.inc','board-grooves.inc'):(root/'build'/name).write_bytes(name.encode())
        for name in ('727.pack','727.inc','set.inc','hanan-data.inc'):(root/'build/board'/name).write_bytes(name.encode())
        (root/'build/board/piano.inc').write_bytes(b'static const uint32_t piano_increments[] = {1};')

    def test_native_snapshot_survives_browser_generator_and_pins_every_byte(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.seed(root)
            target=module.freeze(root,compact=True,hanan=True)
            original=(target/'piano.inc').read_bytes()
            (root/'build/board/piano.inc').write_bytes(b'static const uint32_t increments_0[] = {1};')
            self.assertEqual((target/'piano.inc').read_bytes(),original)
            report=json.loads((target/'INPUTS.json').read_text())
            self.assertEqual(len(report['files']),9)
            for name,pin in report['files'].items():
                data=(target/name).read_bytes();self.assertEqual(len(data),pin['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(),pin['sha256'])
            with self.assertRaisesRegex(ValueError,'compact score'):module.freeze(root,compact=True)

    def test_missing_bank_is_not_silently_replaced(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.seed(root);(root/'build/board/727.pack').unlink()
            with self.assertRaises(FileNotFoundError):module.freeze(root,compact=True)

if __name__=='__main__':unittest.main()
