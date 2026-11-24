"""Validate compact packing against the pinned BSP packager, with inert SDK blobs."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import struct
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
layout=runpy.run_path(str(ROOT/'tools/board-show-layout.py'))

class ShowLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        bsp=os.environ.get('COMP_BSP')
        if not bsp: raise unittest.SkipTest('Set COMP_BSP for pinned-package integration tests')
        file=Path(bsp)/'tools/fm1pkg_make.py'
        pins=json.loads((ROOT/'board/BSP.json').read_text())['files']
        assert hashlib.sha256(file.read_bytes()).hexdigest()==pins['tools/fm1pkg_make.py']
        cls.pack=runpy.run_path(str(ROOT/'tools/board-updater-preflight.py'))['packager_at'](file)

    def test_compact_crc_bounds_and_exact_payload(self):
        for size in (1,65536,layout['APP_SLOT']):
            with self.subTest(size=size):
                app=bytes((i*13+7)&255 for i in range(size))
                flash=layout['compact_flash'](self.pack,app,0x1234)
                report=layout['inspect_flash'](self.pack,flash,0x1234,app)
                self.assertEqual(len(flash),0x90000)
                self.assertLessEqual(report['configEnd'],0x90000)
                self.assertEqual(report['appSlotFreeBytes'],layout['APP_SLOT']-size)
                self.assertEqual((self.pack.APP_SLOT,self.pack.FLASH_SIZE),(0x8DFBC,0x93000))

    def test_does_not_truncate_legacy_or_oversize(self):
        app=b'APP'*512
        with self.assertRaisesRegex(ValueError,'exceeds'):
            layout['compact_flash'](self.pack,b'X'*(layout['APP_SLOT']+1),0x1234)
        legacy=self.pack.flash_image(app,0x1234)
        for flash in (legacy,legacy[:0x90000]):
            with self.assertRaises(ValueError):layout['inspect_flash'](self.pack,flash,0x1234,app)
        flash=bytearray(layout['compact_flash'](self.pack,app,0x1234));flash[0x4120]^=1
        with self.assertRaises(ValueError):layout['inspect_flash'](self.pack,flash,0x1234,app)
        flash=bytearray(layout['compact_flash'](self.pack,app,0x1234))
        with self.assertRaisesRegex(ValueError,'declared slot'):
            layout['inspect_flash'](self.pack,flash,0x1234,b'X'*(layout['APP_SLOT']+1))

    def test_complete_show_crc_wrap_and_torn_record(self):
        flash=bytearray(b'\xff'*0x100000);flash[0x92000:0x92008]=b'CMPASIM1'
        def seal(offset,gen,value):
            payload=bytes([value])*3792
            flash[offset+256:offset+256+3792]=payload
            crc=zlib.crc32(struct.pack('<I',gen)+payload)
            struct.pack_into('<III',flash,offset,0x34535043,gen,crc)
            return payload
        old=seal(0x90000,0xffffffff,1);new=seal(0x91000,0,2)
        self.assertEqual(layout['newest_show'](flash),(0,new))
        flash[0x91100]^=1
        self.assertEqual(layout['newest_show'](flash),(0xffffffff,old))
        flash[0x90100]^=1
        with self.assertRaisesRegex(ValueError,'No complete'):layout['newest_show'](flash)
        with self.assertRaisesRegex(ValueError,'one-megabyte'):layout['newest_show'](flash[:-1])

if __name__=='__main__':unittest.main()
