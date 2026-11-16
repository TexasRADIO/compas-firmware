"""Loader wrapper/parser checks, including semantic faults with repaired CRCs."""
from pathlib import Path
import hashlib
import json
import runpy
import struct
import unittest

M=runpy.run_path(str(Path(__file__).resolve().parents[1]/'tools/board-loader-package.py'))


def head(crc,a,b,attr):
    body=struct.pack('<HIIBBH16s',crc,a,b,attr,0,0,M['NAME'])
    return struct.pack('<H',M['crc16'](body))+body


def wrap(body):return head(M['crc16'](body),32,len(body),0x41)+body


def fixture():
    decoded=b'TESTTESTTESTTAIL!'
    # Four literals, an overlapping eight-byte match, then five final literals.
    compressed=b'\x44TEST\x04\x00\x50TAIL!'
    body=head(M['crc16'](decoded),len(decoded),M['LOAD'],0)+struct.pack('<II',len(compressed),len(decoded))+compressed
    return decoded,body


def receipt_fixture(xip=b'xip'):
    """Synthetic transaction claims for consumer tests, never release evidence."""
    image,body=fixture();ota=wrap(body);digest=lambda b:hashlib.sha256(b).hexdigest()
    full={'profile':{'profile':'first-show-wire','loadSHA256':digest(image),'loadedBytes':len(image)},
          'packagedLoader':{'loadSHA256':digest(image),'loadedBytes':len(image),'wrapperSHA256':digest(ota),
              'wrapperBytes':len(ota),'blocks':[{'compressedBytes':13,'decodedBytes':17}],
              'stagedBodyMatchesPackage':True,'stagedDecodedMatchesLinked':True,'physicalBootQualified':False},
          'installedBoot':{'xipSHA256':digest(xip),'flashOperations':0,
              'timing':{'deadlines':0,'lateHalves':0,'railSamples':0,'callbacks':1645,
                        'maxIrqWindowUs':350,'irqUnmeasuredAllowanceUs':10,'deadlineUs':2901},
              'pcmPeaks':{'beforePlay':0,'afterStop':0,'playing':486}},
          'alreadyCompleteWrites':0,'unknownTailRefusedBeforeAppWrites':True,'unconfirmedShowUnchanged':True,
          'wireChecks':{k:True for k in ('sameLoaderDifferentShow','wrongAppZeroWrites','corruptBodyZeroWrites')},
          'interruptionCases':[{'cut':k,'attemptedOperations':k,'resumeOperations':22 if k<22 else 1} for k in (19,20,22)],
          'prewriteChecks':[{'case':name,'operations':0,'applicationInitiallyDifferent':True,'noAppWritePassRequested':True}
                           for name in ('prewrite-corrupt-body','prewrite-wrong-app')]}
    raw=json.dumps(full).encode()
    inv={'loaderLoadSHA256':digest(image),'evidence':{'fullLoaderReceiptSHA256':digest(raw)},
         'packagedLoader':{k:full['packagedLoader'][k] for k in ('wrapperSHA256','wrapperBytes','loadSHA256','loadedBytes')}}
    return inv,raw,ota,xip


class LoaderPackageTest(unittest.TestCase):
    def test_exact_decode_and_overlap(self):
        expected,body=fixture()
        decoded,blocks=M['decode_package'](wrap(body))
        self.assertEqual(decoded,expected)
        self.assertEqual(blocks,[{'compressedBytes':13,'decodedBytes':17}])

    def test_every_truncation_and_corruption(self):
        _,body=fixture();ota=wrap(body)
        for i in range(len(ota)):
            with self.subTest(cut=i),self.assertRaises(ValueError):M['decode_package'](ota[:i])
            bad=bytearray(ota);bad[i]^=1
            with self.subTest(flip=i),self.assertRaises(ValueError):M['decode_package'](bad)

    def test_rechecksummed_inner_bounds_and_output_crc(self):
        expected,body=fixture()
        for crc,size,address,attr in [(0,len(expected),M['LOAD'],0),
                (M['crc16'](expected),len(expected),M['LOAD']+4,0),
                (M['crc16'](expected),0,M['LOAD'],0),
                (M['crc16'](expected),M['LIMIT']+1,M['LOAD'],0),
                (M['crc16'](expected),len(expected),M['LOAD'],1)]:
            with self.subTest(size=size,address=address,attr=attr),self.assertRaises(ValueError):
                M['decode_package'](wrap(head(crc,size,address,attr)+body[32:]))

    def test_rechecksummed_block_bounds_and_trailing_data(self):
        _,body=fixture()
        for compressed,decoded in [(0,17),(4097,17),(100,17),(13,18),(13,0)]:
            with self.subTest(compressed=compressed,decoded=decoded),self.assertRaises(ValueError):
                M['decode_package'](wrap(body[:32]+struct.pack('<II',compressed,decoded)+body[40:]))
        with self.assertRaises(ValueError):M['decode_package'](wrap(body+b'\0'))

    def test_lz4_malformed_streams(self):
        cases=[(b'\xf0',20),(b'\xf0\xff',20),(b'\x40ABC',4),
               (b'\x10A\x00\x00',5),(b'\x10A\x02\x00',5),
               (b'\x10A\x01',5),(b'\x1fA\x01\x00\xff',5),
               (b'\x10A\x01\x00',4),(b'\x10A',2)]
        for data,size in cases:
            with self.subTest(data=data,size=size),self.assertRaises(ValueError):M['block'](data,size)

    def test_receipt_inventory_and_package_binding(self):
        inv,raw,ota,xip=receipt_fixture()
        report=M['verify_binding'](inv,raw,ota,xip)
        self.assertFalse(report['physicalBootQualified'])
        for r,o,x in [(raw+b' ',ota,xip),(raw,ota+b'X',xip),(raw,ota,xip+b'X')]:
            with self.assertRaises(ValueError):M['verify_binding'](inv,r,o,x)
        del inv['packagedLoader']
        with self.assertRaisesRegex(ValueError,'candidate inventory'):M['verify_binding'](inv,raw,ota,xip)

    def test_bad_claims_with_rebound_receipt(self):
        changes=[('packagedLoader','stagedDecodedMatchesLinked',False),('packagedLoader','physicalBootQualified',True),
                 ('installedBoot','flashOperations',1),('wireChecks','sameLoaderDifferentShow',False),
                 ('profile','loadSHA256','0'*64)]
        for parent,key,value in changes:
            inv,raw,ota,xip=receipt_fixture();full=json.loads(raw);full[parent][key]=value
            raw=json.dumps(full).encode();inv['evidence']['fullLoaderReceiptSHA256']=hashlib.sha256(raw).hexdigest()
            with self.subTest(parent=parent,key=key),self.assertRaises(ValueError):M['verify_binding'](inv,raw,ota,xip)
        for field in ('interruptionCases','prewriteChecks'):
            inv,raw,ota,xip=receipt_fixture();full=json.loads(raw);full[field].pop()
            raw=json.dumps(full).encode();inv['evidence']['fullLoaderReceiptSHA256']=hashlib.sha256(raw).hexdigest()
            with self.subTest(field=field),self.assertRaises(ValueError):M['verify_binding'](inv,raw,ota,xip)
        for field,value in [('deadlines',1),('callbacks',0),('maxIrqWindowUs',3000),('deadlineUs',float('nan'))]:
            inv,raw,ota,xip=receipt_fixture();full=json.loads(raw);full['installedBoot']['timing'][field]=value
            raw=json.dumps(full).encode();inv['evidence']['fullLoaderReceiptSHA256']=hashlib.sha256(raw).hexdigest()
            with self.subTest(field=field),self.assertRaises(ValueError):M['verify_binding'](inv,raw,ota,xip)

    def test_decoder_runs_even_with_rebound_wrapper_hash(self):
        inv,raw,ota,xip=receipt_fixture();bad=bytearray(ota);bad[-1]^=1
        full=json.loads(raw);full['packagedLoader']['wrapperSHA256']=hashlib.sha256(bad).hexdigest()
        inv['packagedLoader']['wrapperSHA256']=full['packagedLoader']['wrapperSHA256']
        raw=json.dumps(full).encode();inv['evidence']['fullLoaderReceiptSHA256']=hashlib.sha256(raw).hexdigest()
        with self.assertRaisesRegex(ValueError,'outer extent/CRC'):M['verify_binding'](inv,raw,bad,xip)


if __name__=='__main__':unittest.main()
