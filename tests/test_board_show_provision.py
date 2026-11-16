"""Fault coverage for the offline first-install plan; not a flash driver test."""
import runpy
from pathlib import Path
import struct
import unittest
import zlib

root=Path(__file__).resolve().parents[1]
p=runpy.run_path(str(root/'tools/board-show-provision.py'))

class ProvisionTest(unittest.TestCase):
    def setUp(self):
        self.before=bytes((i*17+3)&255 for i in range(0x100000))
        self.legacy=self.before[:0x93000]
        self.compact=b'C'*0x90000
        self.updated=self.before[:0x4000]+self.compact[0x4000:]+self.before[0x90000:]
        self.show=bytearray(b'\xff'*0x100000)
        self.payload=bytes((i*7+2)&255 for i in range(3776))
        self.show[0x92000:0x92008]=b'CMPASIM1'
        self.show[0x90100:0x90100+3776]=self.payload
        struct.pack_into('<III',self.show,0x90000,0x32535043,4,zlib.crc32(struct.pack('<I',4)+self.payload))

    def plan(self, **change):
        args=dict(before=self.before,repeated=self.before,updated=self.updated,
                  legacy=self.legacy,compact=self.compact,prepared_show=self.show)
        args.update(change)
        return p['prepare'](**args)

    def test_all_byte_prefix_cuts_and_resume(self):
        plan=self.plan();final=plan.run();total=plan.receipt()['bytePrefixCuts']
        self.assertEqual(total,16084)
        self.assertEqual(p['layout']['newest_show'](final),(1,self.payload))
        self.assertEqual(final[0x91000:0x92000],b'\xff'*4096)
        for cut in range(total):
            torn=plan.run(cut)
            self.assertEqual(torn[:0x90000],self.updated[:0x90000])
            self.assertEqual(torn[0x93000:],self.updated[0x93000:])
            self.assertNotEqual(torn[0x92000:0x92008],b'CMPASIM1')
            self.assertEqual(plan.resume(torn),final)
        self.assertEqual(plan.resume(final),final)

    def test_refuse_wrong_ownership_and_unknown_resume(self):
        for name,value in [('repeated',b'X'*0x100000),('legacy',self.legacy[:-1]),
                           ('compact',self.compact[:-1]),('updated',self.before),
                           ('prepared_show',bytes(0x100000))]:
            with self.subTest(name=name),self.assertRaises(ValueError):self.plan(**{name:value})
        for off in (0,0x4000,0x90000,0x92010,0x93000,0xe4000,0xfffff):
            bad=bytearray(self.updated);bad[off]^=1
            with self.subTest(offset=off),self.assertRaises(ValueError):self.plan(updated=bad)
        plan=self.plan();bad=bytearray(plan.run(12800));bad[0x910a0]=0
        with self.assertRaises(ValueError):plan.resume(bad)
        marked=bytearray(self.before);marked[0x92000:0x92008]=b'CMPASIM1'
        now=bytes(marked[:0x4000])+self.compact[0x4000:]+bytes(marked[0x90000:])
        with self.assertRaisesRegex(ValueError,'Already provisioned'):
            self.plan(before=marked,repeated=marked,legacy=marked[:0x93000],updated=now)

if __name__=='__main__':unittest.main()
