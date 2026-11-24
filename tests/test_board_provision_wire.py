"""Host record structure and binding tests; musical validation belongs to WASM."""
import runpy
from pathlib import Path
import struct
import unittest
import zlib

root=Path(__file__).resolve().parents[1]
wire=runpy.run_path(str(root/'tools/board-provision-wire.py'))


class WireTest(unittest.TestCase):
    def setUp(self):
        before=bytes((i*17+3)&255 for i in range(0x100000))
        compact=before[:0x4000]+b'C'*(0x90000-0x4000)
        updated=compact+before[0x90000:]
        self.payload=bytes((i*7+2)&255 for i in range(3792))
        show=bytearray(b'\xff'*0x100000)
        show[0x92000:0x92008]=b'CMPASIM1'
        show[0x90100:0x90100+3792]=self.payload
        struct.pack_into('<III',show,0x90000,0x34535043,4,
                         zlib.crc32(struct.pack('<I',4)+self.payload))
        self.args=dict(logical=b'H'*0x400+compact,before=before,repeated=before,
                       updated=updated,legacy=before[:0x93000],compact=compact,prepared_show=show)

    def test_record_and_unchanged_package(self):
        served,final=wire['attach'](**self.args)
        self.assertEqual(len(served),0x100000+16144)
        self.assertEqual(served[:len(self.args['logical'])],self.args['logical'])
        header=served[0x100000:0x100040];body=served[0x100040:]
        self.assertEqual(header[:8],b'CPPRV001')
        fields=struct.unpack('<14I',header[8:])
        self.assertEqual(fields[:5],(1,16144,0x90000,12288,3792))
        self.assertEqual(fields[5:8],(zlib.crc32(self.args['before'][:0x4000]),
                                     zlib.crc32(self.args['compact'][0x4000:]),zlib.crc32(body)))
        self.assertEqual(fields[8:10],(zlib.crc32(body[:12288]),zlib.crc32(body[12288:])))
        self.assertEqual(fields[10:],(0x4000,0x90000,zlib.crc32(header[:56]),0))
        self.assertEqual(body,self.args['before'][0x90000:0x93000]+self.payload)
        self.assertEqual(wire['model']['layout']['newest_show'](final),(1,self.payload))

    def test_reject_mismatch_and_overlap(self):
        for key,value in [('logical',b''),('logical',b'X'*(0x100001)),
                          ('logical',b'H'*0x400+b'X'*0x90000),
                          ('repeated',b'X'*0x100000),('prepared_show',bytes(0x100000))]:
            with self.subTest(key=key),self.assertRaises(ValueError):
                wire['attach'](**(self.args|{key:value}))


if __name__=='__main__':unittest.main()
