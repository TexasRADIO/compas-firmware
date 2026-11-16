"""Prepare a first-show review from saved files only; no device or flash command."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import runpy
import stat
import struct
import subprocess
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
LAYOUT=runpy.run_path(str(ROOT/'tools/board-show-layout.py'))
WIRE=runpy.run_path(str(ROOT/'tools/board-provision-wire.py'))
INVENTORY=runpy.run_path(str(ROOT/'tools/board-candidate-inventory.py'))
LOADER=runpy.run_path(str(ROOT/'tools/board-loader-package.py'))
LIMITS={'baseline':0x100000,'repeat':0x100000,'legacy':0x93000,
        'compact':0x90000,'logical':0x100000,'xip':0x90000,
        'rehearsal':1024*1024,'show':65536,'wasm':4*1024*1024}
EXACT={'baseline','repeat','legacy','compact'}


def sha(data):return hashlib.sha256(data).hexdigest()


def read_saved(path,limit,exact=False):
    with path.open('rb') as f:
        import os
        before=os.fstat(f.fileno())
        if not stat.S_ISREG(before.st_mode) or not 0<before.st_size<=limit or (exact and before.st_size!=limit):
            raise ValueError(f'Invalid saved-file size or type: {path.name}')
        data=f.read(limit+1);after=os.fstat(f.fileno())
        if (before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns,before.st_ctime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns) or len(data)!=before.st_size:
            raise ValueError(f'Saved file changed during read: {path.name}')
    return data,(before.st_dev,before.st_ino)


def pinned_packager(bsp):
    name='tools/fm1pkg_make.py';path=bsp/name
    data=path.read_bytes();pin=json.loads((ROOT/'board/BSP.json').read_text())['files'][name]
    if sha(data)!=pin:raise ValueError('Package parser source differs from the pinned BSP')
    spec=importlib.util.spec_from_file_location('compas_plan_packager',path)
    pack=importlib.util.module_from_spec(spec);exec(compile(data,str(path),'exec'),pack.__dict__)
    return pack


def capture_key(pack,flash):
    def decoded(data):
        b=bytearray(data);pack.enc(b,0,len(b));return b
    head=decoded(flash[:32])
    if pack.crc16(head[2:])!=struct.unpack_from('<H',head)[0]:raise ValueError('Flash-head checksum mismatch')
    for off in range(32,0x400,32):
        row=decoded(flash[off:off+32])
        if pack.crc16(row[2:])!=struct.unpack_from('<H',row)[0]:raise ValueError('Flash directory checksum mismatch')
        if row[16:].split(b'\0',1)[0]==b'isd_config.ini':
            address,size=struct.unpack_from('<II',row,4)
            if size<34 or address<0xa0 or address+size>0x4000:raise ValueError('Chip-key file lies outside the flash head')
            blob=flash[address:address+size]
            if pack.crc16(blob)!=struct.unpack_from('<H',row,2)[0] or pack.crc16(blob[:32])!=struct.unpack_from('<H',blob,32)[0]:
                raise ValueError('Chip-key file checksum mismatch')
            return pack.chipkey_decode(blob[:32])
        if struct.unpack_from('<H',row,14)[0]:break
    raise ValueError('No checked chip-key record in the full capture')


def inspect_logical(pack,logical,compact):
    if len(logical)<0x400:raise ValueError('Truncated logical package')
    h=bytearray(logical[:64]);pack.enc(h,0,64)
    total=struct.unpack_from('<I',h,4)[0];count=struct.unpack_from('<H',h,8)[0]
    if count!=2 or total!=len(logical) or h[16:22]!=b'AC791N' or pack.crc16(h[2:])!=struct.unpack_from('<H',h)[0]:
        raise ValueError('Invalid logical package header')
    rows=logical[64:64+count*80]
    if pack.crc16(rows)!=struct.unpack_from('<H',h,2)[0]:raise ValueError('Invalid package directory checksum')
    entries={};ranges=[]
    for i in range(count):
        e=bytearray(rows[i*80:(i+1)*80]);pack.enc(e,0,80)
        kind,index,crc,reserved,offset,size,padded=struct.unpack_from('<HHHHIII',e)
        name=bytes(e[64:]).split(b'\0',1)[0]
        if kind not in (0,100) or kind in entries or index!=i or reserved or name!={0:b'flash.bin',100:b'ota.bin'}[kind] or offset<0x400 or offset%256 or not size or offset+size>len(logical) or padded!=((size+31)&~31):
            raise ValueError('Invalid package entry or extent')
        if any(offset<end and start<offset+size for start,end in ranges):raise ValueError('Package entries overlap')
        ranges.append((offset,offset+size));data=logical[offset:offset+size]
        if pack.crc16(data)!=crc:raise ValueError('Package data checksum mismatch')
        entries[kind]=(offset,data)
    if entries[0]!=(0x400,compact):raise ValueError('Package does not contain the exact compact envelope')
    return entries[100][1]


def check_rehearsal(receipt,xip):
    if receipt.get('imageSHA256')!=sha(xip):raise ValueError('Candidate differs from its strict rehearsal')
    if any(receipt.get(k)!=0 for k in ('deadlines','lateHalves','rails')):raise ValueError('Rehearsal reports audio faults or omits their counters')
    if receipt.get('durationMs',0)<138000 or receipt.get('strictCallbacks',0)<47000:
        raise ValueError('Need the full strict set rehearsal')
    fields=('peakIrqWindowUs','irqUnmeasuredAllowanceUs','deadlineUs')
    values=[receipt.get(k) for k in fields]
    if not all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in values) or values[0]+values[1]>=values[2]:
        raise ValueError('Rehearsal does not meet the full audio deadline')


def prepare(manifest_path,bsp,output):
    manifest_bytes=read_saved(manifest_path,65536)[0]
    manifest=json.loads(manifest_bytes)
    names=set(manifest.get('inputs',{}))
    if manifest.get('format')!='compas-first-show-review-1' or names not in (set(LIMITS),set(LIMITS)|{'inventory'},set(LIMITS)|{'inventory','loaderRehearsal'}):
        raise ValueError('Expected a complete Compas first-show review manifest')
    if output.exists():raise ValueError('Choose a new review folder; existing output is never overwritten')
    blobs={};paths={};identities={}
    limits=LIMITS|{name:1024*1024 for name in ('inventory','loaderRehearsal') if name in names}
    for name,limit in limits.items():
        item=manifest['inputs'][name]
        path=(manifest_path.parent/item['path']).resolve()
        blobs[name],identities[name]=read_saved(path,limit,name in EXACT);paths[name]=path
        if sha(blobs[name])!=item.get('sha256'):raise ValueError(f'{name} differs from the review manifest')
    if identities['baseline']==identities['repeat']:raise ValueError('Baseline and repeat must be separately saved files, not aliases')
    if blobs['baseline']!=blobs['repeat']:raise ValueError('Baseline captures differ')
    pack=pinned_packager(bsp)
    key=capture_key(pack,blobs['baseline'])
    xip=blobs['xip']
    if len(xip)<0x120 or xip[:0x120]!=b'\xff'*0x120:raise ValueError('Expected the rehearsed XIP entry padding')
    check_rehearsal(json.loads(blobs['rehearsal']),xip)
    source_commit=None
    if 'inventory' in blobs:
        source_commit=INVENTORY['verify_binding'](json.loads(blobs['inventory']),xip,blobs['wasm'],blobs['rehearsal'])
    compact=LAYOUT['inspect_flash'](pack,blobs['compact'],key,xip[0x120:])
    old=bytearray(blobs['legacy'][0x4000:]);pack.sfc(old,0,len(old),0,key)
    LAYOUT['inspect_flash'](pack,blobs['legacy'],key,old[0x120:0x120+0x8DFBC],app_end=0x93000,app_slot=0x8DFBC)
    ota=inspect_logical(pack,blobs['logical'],blobs['compact'])
    loader_binding=None
    if 'loaderRehearsal' in blobs:
        loader_binding=LOADER['verify_binding'](json.loads(blobs['inventory']),blobs['loaderRehearsal'],ota,xip)
    validation=subprocess.run(['node',str(ROOT/'tools/board-show-validate.mjs')],
        input=json.dumps({'wasm':str(paths['wasm']),'wasmSHA256':sha(blobs['wasm']),'show':json.loads(blobs['show'])}),
        text=True,capture_output=True,timeout=30)
    if validation.returncode:raise ValueError('Native show validation failed: '+validation.stderr[-500:])
    payload=bytes.fromhex(json.loads(validation.stdout)['payload'])
    if len(payload)!=3776:raise ValueError('Unexpected native show payload size')
    show=bytearray(b'\xff'*0x100000);show[0x90100:0x90100+3776]=payload;show[0x92000:0x92008]=b'CMPASIM1'
    struct.pack_into('<III',show,0x90000,0x32535043,1,zlib.crc32(struct.pack('<I',1)+payload))
    updated=blobs['baseline'][:0x4000]+blobs['compact'][0x4000:]+blobs['baseline'][0x90000:]
    served,expected=WIRE['attach'](blobs['logical'],blobs['baseline'],blobs['repeat'],updated,blobs['legacy'],blobs['compact'],show)
    record=served[0x100000:]
    report={'format':'compas-first-show-plan-1','scope':'saved-file review; not installation authorization',
        'physicalWriteAuthorized':False,
        'physicalWriteGate':{
            'requiredRomUsbVIDPID':'4C4A:8057',
            'romEntryAndToolCommunication':'not-observed',
            'appIndependentRecoveryAcrossWriteRanges':'not-verified',
            'independentFullFlashCapturesOnTarget':'not-verified',
            'targetBoundReturnImageAndRestore':'not-verified',
            'protectedRegionsLibraryAndCalibration':'not-verified',
            'installedImageRomReentryAndNormalBoot':'not-performed',
        },
        'manifestSHA256':sha(manifest_bytes),
        'inputSHA256':{k:sha(v) for k,v in blobs.items()},'separateMatchingCaptures':True,
        'compactEnvelope':compact,'showNativeValidated':True,'showPayloadSHA256':sha(payload),
        'provisionRecordBytes':len(record),'provisionRecordSHA256':sha(record),
        'servedLogicalSHA256':sha(served),'servedLogicalBytes':len(served),
        'expectedAfterShowBeforeCleanupSHA256':sha(expected),'otaEntrySHA256':sha(ota),
        'gates':{'fileInputs':'pass','audioReceiptBinding':'pass','musicalShow':'pass',
                 'loaderBootQualification':'open','validatorAndAppReleaseProvenance':'paired-build-bound' if source_commit else 'review-required',
                 'releaseReview':'open',
                 'loaderPackage':'staged-and-decoded-bound' if loader_binding else 'not-bound',
                 'currentPhysicalIdentityAndState':'not-observed','hardwareValidation':'open'}}
    if source_commit:report['sourceCommit']=source_commit
    if loader_binding:report['loaderBinding']=loader_binding
    output.mkdir(parents=False)
    # Receipt is published last, after exact readback of the inert record.
    with (output/'provision-record.bin').open('xb') as f:f.write(record)
    if (output/'provision-record.bin').read_bytes()!=record:raise ValueError('Provision record readback differs')
    with (output/'plan.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--bsp',type=Path,required=True);args=parser.parse_args()
    try:report=prepare(args.manifest.resolve(),args.bsp.resolve(),args.output.resolve())
    except (ValueError,OSError,KeyError,TypeError,subprocess.TimeoutExpired) as error:parser.exit(1,str(error)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
