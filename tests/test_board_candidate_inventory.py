"""Inventory consumer checks. Fixtures describe a mock build, not a release."""
import copy
import json
from pathlib import Path
import runpy
import unittest

ROOT=Path(__file__).resolve().parents[1]
M=runpy.run_path(str(ROOT/'tools/board-candidate-inventory.py'))


def fixture(xip=b'xip',wasm=b'wasm',receipt=None):
    if receipt is None:
        receipt={'imageSHA256':M['sha'](xip),'durationMs':164000,'strictCallbacks':56407,
                 'deadlines':0,'lateHalves':0,'rails':0,'peakIrqWindowUs':900,
                 'irqUnmeasuredAllowanceUs':10,'deadlineUs':2901.333,
                 'R1':{'computeMs':2,'transferMs':3,'completeMs':5,'pixels':2000},
                 'R2':{'computeMs':2,'transferMs':15,'completeMs':20,'pixels':8000},
                 'R3':{'computeMs':15,'transferMs':80,'completeMs':110,'pixels':57600}}
        receipt=json.dumps(receipt).encode()
    files={name:'a'*64 for name in M['REQUIRED_SOURCES']}
    result={'format':M['FORMAT'],'releaseReady':False,
        'source':{'commit':'c'*40,'archiveSHA256':'b'*64,'files':files,'filesSHA256':M['sha'](M['canonical'](files))},
        'artifacts':{k:{'sha256':M['sha'](v),'bytes':len(v)} for k,v in [('xip',xip),('wasm',wasm)]},
        'evidence':{'audioReceiptSHA256':M['sha'](receipt)},
        'checks':{'committedSourceUnchanged':True,'loaderMatchesFullTransaction':True}}
    return result,xip,wasm,receipt


class CandidateInventoryTest(unittest.TestCase):
    def test_exact_pair_and_receipt(self):
        inv,xip,wasm,receipt=fixture()
        self.assertEqual(M['verify_binding'](inv,xip,wasm,receipt),'c'*40)
        for args in ((xip+b'x',wasm,receipt),(xip,wasm+b'x',receipt),(xip,wasm,receipt+b' ')):
            with self.assertRaisesRegex(ValueError,'mismatch'):M['verify_binding'](inv,*args)

    def test_source_and_rebuild_claims(self):
        for change in ('source','hash','commit','check','release'):
            inv,xip,wasm,receipt=fixture()
            if change=='source':del inv['source']['files']['src/engine.c']
            if change=='hash':inv['source']['filesSHA256']='0'*64
            if change=='commit':inv['source']['commit']='HEAD'
            if change=='check':inv['checks']['loaderMatchesFullTransaction']=False
            if change=='release':inv['releaseReady']=True
            with self.subTest(change=change),self.assertRaises(ValueError):M['verify_binding'](inv,xip,wasm,receipt)

    def test_timing_gates_not_just_matching_receipt_hash(self):
        for gate,key,value in [('R1','pixels',6001),('R1','computeMs',4.1),('R1','completeMs',12.1),
                               ('R2','completeMs',40.1),('R3','computeMs',40.1),('R3','completeMs',120.1),
                               ('R2','transferMs',None)]:
            inv,xip,wasm,raw=fixture();receipt=json.loads(raw);receipt[gate][key]=value
            raw=json.dumps(receipt).encode();inv['evidence']['audioReceiptSHA256']=M['sha'](raw)
            with self.subTest(gate=gate,key=key),self.assertRaises(ValueError):M['verify_binding'](inv,xip,wasm,raw)


if __name__=='__main__':unittest.main()
