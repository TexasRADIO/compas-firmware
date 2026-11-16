"""Saved-file planning against pinned packing code and the real show validator."""
import copy
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
P=runpy.run_path(str(ROOT/'tools/board-install-plan.py'))


class InstallPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.environ.get('COMP_BSP'):raise unittest.SkipTest('Set COMP_BSP for saved-file integration tests')
        cls.bsp=Path(os.environ['COMP_BSP'])
        cls.temp=tempfile.TemporaryDirectory();cls.base=Path(cls.temp.name)
        cls.pack=P['pinned_packager'](cls.bsp)
        cls.pack.sdk_file=lambda name:b'HOST ONLY: '+name.encode()
        app=b'HOST-TEST-APP'*128
        legacy=cls.pack.flash_image(b'PRIOR-HOST-APP'*128,0x1234)
        compact=P['LAYOUT']['compact_flash'](cls.pack,app,0x1234)
        baseline=legacy+bytes((i*17+3)&255 for i in range(0x100000-len(legacy)))
        raw=cls.pack.ufw(compact,b'INERT OTA; NOT BOOTABLE','COMPAS-FIXTURE')
        logical=b''.join(raw[i:i+47] for i in range(0,960,48))+raw[960:]
        xip=b'\xff'*0x120+app
        receipt={'imageSHA256':P['sha'](xip),'durationMs':164000,'strictCallbacks':56407,
                 'deadlines':0,'lateHalves':0,'rails':0,'peakIrqWindowUs':900,
                 'irqUnmeasuredAllowanceUs':10,'deadlineUs':2901.333}
        subprocess.run(['node',str(ROOT/'tests/show-flash.mjs'),str(cls.base)],check=True,capture_output=True)
        cls.data={'baseline':baseline,'repeat':baseline,'legacy':legacy,'compact':compact,
                  'logical':logical,'xip':xip,'rehearsal':json.dumps(receipt).encode(),
                  'show':(cls.base/'show.cpshow').read_bytes(),
                  'wasm':(ROOT/'build/board/browser-board.wasm').read_bytes()}

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def setUp(self):
        self.tempdir=tempfile.TemporaryDirectory();self.addCleanup(self.tempdir.cleanup)
        self.folder=Path(self.tempdir.name);self.out=self.folder/'review'
        self.manifest={'format':'compas-first-show-review-1','inputs':{}}
        for name,data in self.data.items():self.put(name,data)

    def put(self,name,data):
        path=self.folder/name;path.write_bytes(data)
        self.manifest['inputs'][name]={'path':name,'sha256':P['sha'](data)}

    def run_plan(self):
        source=self.folder/'manifest.json';source.write_text(json.dumps(self.manifest))
        return P['prepare'](source,self.bsp,self.out)

    def test_saved_file_plan_and_no_input_mutation(self):
        before={p.name:P['sha'](p.read_bytes()) for p in self.folder.iterdir()}
        report=self.run_plan()
        self.assertEqual(report['provisionRecordBytes'],16128)
        self.assertEqual(report['inputSHA256']['baseline'],P['sha'](self.data['baseline']))
        self.assertEqual(report['gates']['hardwareValidation'],'open')
        self.assertEqual(report['gates']['currentPhysicalIdentityAndState'],'not-observed')
        self.assertTrue(report['separateMatchingCaptures'])
        self.assertIs(report['physicalWriteAuthorized'],False)
        self.assertEqual(report['physicalWriteGate'],{
            'requiredRomUsbVIDPID':'4C4A:8057',
            'romEntryAndToolCommunication':'not-observed',
            'appIndependentRecoveryAcrossWriteRanges':'not-verified',
            'independentFullFlashCapturesOnTarget':'not-verified',
            'targetBoundReturnImageAndRestore':'not-verified',
            'protectedRegionsLibraryAndCalibration':'not-verified',
            'installedImageRomReentryAndNormalBoot':'not-performed',
        })
        self.assertEqual(report,json.loads((self.out/'plan.json').read_text()))
        self.assertEqual(P['sha']((self.out/'provision-record.bin').read_bytes()),report['provisionRecordSHA256'])
        for name,digest in before.items():self.assertEqual(P['sha']((self.folder/name).read_bytes()),digest)
        with self.assertRaisesRegex(ValueError,'never overwritten'):self.run_plan()

    def test_alias_mismatch_and_manifest_binding(self):
        (self.folder/'repeat').unlink();os.link(self.folder/'baseline',self.folder/'repeat')
        with self.assertRaisesRegex(ValueError,'not aliases'):self.run_plan()
        (self.folder/'repeat').unlink();self.put('repeat',b'X'*0x100000)
        with self.assertRaisesRegex(ValueError,'captures differ'):self.run_plan()
        self.put('repeat',self.data['repeat']);(self.folder/'xip').write_bytes(b'wrong')
        with self.assertRaisesRegex(ValueError,'review manifest'):self.run_plan()
        self.assertFalse(self.out.exists())

    def test_wrong_key_and_application_envelope(self):
        wrong=self.pack.flash_image(b'PRIOR-HOST-APP'*128,0x5678)+self.data['baseline'][0x93000:]
        self.put('baseline',wrong);self.put('repeat',wrong)
        with self.assertRaisesRegex(ValueError,'directory header'):self.run_plan()
        self.put('baseline',self.data['baseline']);self.put('repeat',self.data['repeat'])
        compact=bytearray(self.data['compact']);compact[0x4120]^=1;self.put('compact',compact)
        with self.assertRaisesRegex(ValueError,'checksum'):self.run_plan()
        self.assertFalse(self.out.exists())

    def test_receipt_faults_and_missing_metrics(self):
        for key,value in [('imageSHA256','0'*64),('deadlines',1),('lateHalves',1),('rails',1),
                          ('durationMs',5000),('strictCallbacks',100),('peakIrqWindowUs',2900),
                          ('deadlineUs',None),('irqUnmeasuredAllowanceUs',float('nan'))]:
            receipt=json.loads(self.data['rehearsal']);receipt[key]=value
            self.put('rehearsal',json.dumps(receipt).encode())
            with self.subTest(key=key),self.assertRaises(ValueError):self.run_plan()
            self.assertFalse(self.out.exists())

    def test_native_show_rejection_and_bad_package(self):
        self.put('show',(self.base/'invalid.cpshow').read_bytes())
        with self.assertRaisesRegex(ValueError,'Native show validation'):self.run_plan()
        self.put('show',self.data['show'])
        package=bytearray(self.data['logical']);package[-1]^=1;self.put('logical',package)
        with self.assertRaisesRegex(ValueError,'Package data checksum'):self.run_plan()
        self.assertFalse(self.out.exists())

    def test_package_duplicate_and_overlap_checks(self):
        for duplicate in (True,False):
            logical=bytearray(self.data['logical']);h=bytearray(logical[:64]);self.pack.enc(h,0,64)
            row=bytearray(logical[144:224]);self.pack.enc(row,0,80)
            if duplicate:row[:2]=b'\0\0'
            else:row[8:12]=(0x400).to_bytes(4,'little')
            self.pack.enc(row,0,80);logical[144:224]=row
            h[2:4]=self.pack.crc16(logical[64:224]).to_bytes(2,'little')
            h[:2]=self.pack.crc16(h[2:]).to_bytes(2,'little');self.pack.enc(h,0,64);logical[:64]=h
            self.put('logical',logical)
            with self.subTest(duplicate=duplicate),self.assertRaisesRegex(ValueError,'entry|overlap'):self.run_plan()
        self.assertFalse(self.out.exists())

    def test_optional_candidate_inventory_binding(self):
        from test_board_candidate_inventory import fixture
        receipt=json.loads(self.data['rehearsal'])
        receipt.update({k:{'computeMs':1,'transferMs':2,'completeMs':3,'pixels':100}
                        for k in ('R1','R2','R3')})
        raw=json.dumps(receipt).encode();self.put('rehearsal',raw)
        inventory,_,_,_=fixture(self.data['xip'],self.data['wasm'],raw)
        wrong=copy.deepcopy(inventory);wrong['artifacts']['wasm']['sha256']='0'*64
        self.put('inventory',json.dumps(wrong).encode())
        with self.assertRaisesRegex(ValueError,'inventory mismatch: wasm'):self.run_plan()
        self.assertFalse(self.out.exists())
        self.put('inventory',json.dumps(inventory).encode());report=self.run_plan()
        self.assertEqual(report['sourceCommit'],'c'*40)
        self.assertEqual(report['gates']['validatorAndAppReleaseProvenance'],'paired-build-bound')
        self.assertEqual(report['gates']['releaseReview'],'open')

    def test_loader_receipt_requires_inventory(self):
        self.put('loaderRehearsal',b'{}')
        with self.assertRaisesRegex(ValueError,'complete Compas'):self.run_plan()
        self.assertFalse(self.out.exists())

    def test_loader_package_is_checked_before_output(self):
        from test_board_candidate_inventory import fixture
        from test_board_loader_package import receipt_fixture
        receipt=json.loads(self.data['rehearsal'])
        receipt.update({k:{'computeMs':1,'transferMs':2,'completeMs':3,'pixels':100} for k in ('R1','R2','R3')})
        audio=json.dumps(receipt).encode();self.put('rehearsal',audio)
        inventory,_,_,_=fixture(self.data['xip'],self.data['wasm'],audio)
        loader,raw,ota,_=receipt_fixture(self.data['xip'])
        inventory['evidence'].update(loader.pop('evidence'));inventory.update(loader)
        self.put('inventory',json.dumps(inventory).encode());self.put('loaderRehearsal',raw)
        # The prior inert OTA has a valid package CRC, but is not this loader.
        with self.assertRaisesRegex(ValueError,'OTA wrapper differs'):self.run_plan()
        self.assertFalse(self.out.exists())
        package=self.pack.ufw(self.data['compact'],ota,'COMPAS-FIXTURE')
        logical=b''.join(package[i:i+47] for i in range(0,960,48))+package[960:]
        self.put('logical',logical);report=self.run_plan()
        self.assertEqual(report['gates']['loaderPackage'],'staged-and-decoded-bound')
        self.assertEqual(report['loaderBinding']['wrapperSHA256'],P['sha'](ota))
        self.assertEqual(report['gates']['loaderBootQualification'],'open')
        self.assertEqual(report['gates']['hardwareValidation'],'open')
        self.assertIs(report['physicalWriteAuthorized'],False)


if __name__=='__main__':unittest.main()
