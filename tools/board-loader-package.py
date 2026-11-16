"""Rehearse real loader compression and staging on synthetic NOR, never USB."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import runpy
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
LOAD=0x01C0A800
LIMIT=0x14000
NAME=b'usb_hid_ota.bin'.ljust(16,b'\0')


def sha(data):return hashlib.sha256(data).hexdigest()


def crc16(data):
    c=0
    for b in data:
        c^=b<<8
        for _ in range(8):c=((c<<1)^0x1021 if c&0x8000 else c<<1)&65535
    return c


def header(data):
    if len(data)<32 or crc16(data[2:32])!=int.from_bytes(data[:2],'little'):
        raise ValueError('Loader header CRC/length')
    h=struct.unpack_from('<HHIIBBH16s',data)
    if h[5:7]!=(0,0) or h[7]!=NAME:raise ValueError('Loader header name/reserved fields')
    return h


def block(data,size):
    """Independent bounded LZ4 block decoder; blocks have no shared dictionary."""
    out=bytearray();i=0
    def length(value):
        nonlocal i
        if value==15:
            while True:
                if i>=len(data):raise ValueError('Truncated LZ4 length')
                b=data[i];i+=1;value+=b
                if value>size:raise ValueError('LZ4 length exceeds output')
                if b!=255:break
        return value
    while i<len(data):
        token=data[i];i+=1;literal=length(token>>4)
        if literal>len(data)-i or literal>size-len(out):raise ValueError('LZ4 literal bounds')
        out+=data[i:i+literal];i+=literal
        if i==len(data):break
        if i+2>len(data):raise ValueError('Truncated LZ4 offset')
        offset=int.from_bytes(data[i:i+2],'little');i+=2
        if not 0<offset<=len(out):raise ValueError('LZ4 offset bounds')
        count=length(token&15)+4
        if count>size-len(out):raise ValueError('LZ4 match bounds')
        for _ in range(count):out.append(out[-offset])
    if len(out)!=size:raise ValueError('LZ4 decoded length')
    return bytes(out)


def decode_body(body):
    h=header(body)
    if h[3]!=LOAD or h[4]!=0 or not 0<h[2]<=LIMIT:raise ValueError('Loader load bounds')
    i=32;out=bytearray();blocks=[]
    while len(out)<h[2]:
        if i+8>len(body):raise ValueError('Truncated loader block header')
        compressed,decoded=struct.unpack_from('<II',body,i);i+=8
        if not 0<compressed<=4096 or decoded!=min(4096,h[2]-len(out)) or compressed>len(body)-i:
            raise ValueError('Loader block bounds')
        out+=block(body[i:i+compressed],decoded);i+=compressed
        blocks.append({'compressedBytes':compressed,'decodedBytes':decoded})
    if i!=len(body) or crc16(out)!=h[1]:raise ValueError('Loader decoded CRC/trailing bytes')
    return bytes(out),blocks


def decode_package(ota):
    h=header(ota)
    if h[2]!=32 or h[3]!=len(ota)-32 or h[4]!=0x41 or h[3]>0x5000-0x200 or crc16(ota[32:])!=h[1]:
        raise ValueError('Loader outer extent/CRC')
    return decode_body(ota[32:])


def check_receipt(full,load_sha,xip_sha):
    """Check the offline transaction claims; the caller must bind/review the receipt."""
    profile=full.get('profile',{});packed=full.get('packagedLoader',{})
    boot=full.get('installedBoot',{});timing=boot.get('timing',{})
    if (profile.get('profile')!='first-show-wire' or profile.get('loadSHA256')!=load_sha or
        packed.get('loadSHA256')!=load_sha or packed.get('loadedBytes')!=profile.get('loadedBytes') or
        boot.get('xipSHA256')!=xip_sha):raise ValueError('Packaged loader/app receipt mismatch')
    if (packed.get('stagedBodyMatchesPackage') is not True or packed.get('stagedDecodedMatchesLinked') is not True or
        packed.get('physicalBootQualified') is not False):raise ValueError('Missing offline staging evidence')
    if (full.get('alreadyCompleteWrites')!=0 or full.get('unknownTailRefusedBeforeAppWrites') is not True or
        full.get('unconfirmedShowUnchanged') is not True or
        any(full.get('wireChecks',{}).get(k) is not True for k in ('sameLoaderDifferentShow','wrongAppZeroWrites','corruptBodyZeroWrites'))):
        raise ValueError('Missing loader transaction checks')
    cuts=full.get('interruptionCases',[]);refusals=full.get('prewriteChecks',[])
    if (len(cuts)!=3 or {c.get('cut') for c in cuts}!={19,20,22} or
        any(c.get('attemptedOperations')!=c.get('cut') or type(c.get('resumeOperations')) is not int or not 0<c['resumeOperations']<=22 for c in cuts) or
        len(refusals)!=2 or {c.get('case') for c in refusals}!={'prewrite-corrupt-body','prewrite-wrong-app'} or
        any(c.get('operations')!=0 or c.get('applicationInitiallyDifferent') is not True or c.get('noAppWritePassRequested') is not True for c in refusals)):
        raise ValueError('Missing interruption/prewrite evidence')
    if (boot.get('flashOperations')!=0 or any(timing.get(k)!=0 for k in ('deadlines','lateHalves','railSamples')) or
        timing.get('callbacks',0)<1400 or boot.get('pcmPeaks',{}).get('beforePlay')!=0 or
        boot.get('pcmPeaks',{}).get('afterStop')!=0 or boot.get('pcmPeaks',{}).get('playing',0)<=0):
        raise ValueError('Installed application playback evidence fails')
    times=[timing.get(k) for k in ('maxIrqWindowUs','irqUnmeasuredAllowanceUs','deadlineUs')]
    if not all(type(t) in (int,float) and math.isfinite(t) and t>=0 for t in times) or times[0]+times[1]>=times[2]:
        raise ValueError('Installed application deadline evidence fails')
    return packed


def verify_binding(inventory,receipt,ota,xip):
    if sha(receipt)!=inventory.get('evidence',{}).get('fullLoaderReceiptSHA256'):
        raise ValueError('Loader receipt differs from candidate inventory')
    packed=check_receipt(json.loads(receipt),inventory.get('loaderLoadSHA256'),sha(xip))
    if inventory.get('packagedLoader')!={k:packed.get(k) for k in ('wrapperSHA256','wrapperBytes','loadSHA256','loadedBytes')}:
        raise ValueError('Packaged loader differs from candidate inventory')
    if packed.get('wrapperSHA256')!=sha(ota) or packed.get('wrapperBytes')!=len(ota):
        raise ValueError('OTA wrapper differs from staged loader receipt')
    decoded,blocks=decode_package(ota)
    if sha(decoded)!=inventory.get('loaderLoadSHA256') or len(decoded)!=packed.get('loadedBytes') or blocks!=packed.get('blocks'):
        raise ValueError('Decoded OTA loader differs from reviewed candidate')
    return {'wrapperSHA256':sha(ota),'wrapperBytes':len(ota),'loadSHA256':sha(decoded),
            'loadedBytes':len(decoded),'receiptSHA256':sha(receipt),'physicalBootQualified':False}


def package(work,bsp):
    pins=json.loads((ROOT/'board/BSP.json').read_text())['files']
    sources=('tools/build.py','tools/lz4blk.py','tools/fm1pkg_make.py',
             'tests/ota_test.c','firmware/src/ota.c')
    for name in sources:
        data=(bsp/name).read_bytes()
        if sha(data)!=pins[name]:raise ValueError('BSP drift: '+name)
        dest=work/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    # Import the pinned builder, invoking only its pure loader wrapper.
    spec=importlib.util.spec_from_file_location('compas_pinned_builder',work/'tools/build.py')
    builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
    image=(work/'loader.bin').read_bytes()
    receipt=json.loads((work/'loader-target.json').read_text())
    if sha(image)!=receipt['loadSHA256'] or b'ota-COMPAS-FIXTURE\0' not in image:
        raise ValueError('Expected the inspected offline loader')
    ota=builder.ldr_wrap(image);decoded,blocks=decode_package(ota)
    if decoded!=image:raise ValueError('Packaged loader differs from linked bytes')
    (work/'ota.bin').write_bytes(ota)
    pre=runpy.run_path(str(ROOT/'tools/board-updater-preflight.py'))
    pack=pre['packager_at'](work/'tools/fm1pkg_make.py')
    layout=runpy.run_path(str(ROOT/'tools/board-show-layout.py'))
    xip=(ROOT/'build/board/emu-store.xip').read_bytes()
    compact=layout['compact_flash'](pack,xip[0x120:],pre['FIXTURE_KEY'])
    raw=pack.ufw(compact,ota,'COMPAS-FIXTURE')
    (work/'package.fixture').write_bytes(raw)
    (work/'tests/board-ota-package.c').write_bytes((ROOT/'tests/board-ota-package.c').read_bytes())
    exe=work/'stage-package'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-g','-Wall','-Wextra',
                    '-fsanitize=address,undefined',str(work/'tests/board-ota-package.c'),'-o',str(exe)],check=True,capture_output=True)
    env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    stage=subprocess.run([str(exe),str(work/'package.fixture'),str(work/'staged.bin')],check=True,capture_output=True,text=True,env=env)
    (work/'stage-package.log').write_text(stage.stdout+stage.stderr)
    staged=(work/'staged.bin').read_bytes()
    # Stage 1 rewrites only the outer directory header; its body is byte exact.
    h=staged[0xE0000:0xE0020]
    if crc16(h[2:])!=int.from_bytes(h[:2],'little') or h[16:26]!=b'LOADER.BIN':
        raise ValueError('Staged directory header')
    if struct.unpack_from('<II',h,4)!=(0xE0000,len(ota)):raise ValueError('Staged directory extent')
    body=staged[0xE0020:0xE0000+len(ota)]
    staged_image,staged_blocks=decode_body(body)
    if body!=ota[32:] or staged_image!=image or staged_blocks!=blocks:
        raise ValueError('Staged loader differs')
    (work/'staged-loader.bin').write_bytes(staged_image)
    return {'scope':'synthetic package/staging and host decompression; physical SPL unverified',
            'sourceSHA256':{n:pins[n] for n in sources},'wrapperBytes':len(ota),
            'wrapperSHA256':sha(ota),'loadedBytes':len(image),'loadSHA256':sha(image),
            'blocks':blocks,'fixturePackageSHA256':sha(raw),'stagedNorSHA256':sha(staged),
            'stagedBodyMatchesPackage':True,'stagedDecodedMatchesLinked':True,
            'stageLog':stage.stdout.strip().splitlines(),'physicalBootQualified':False}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('work',type=Path);p.add_argument('bsp',type=Path)
    a=p.parse_args()
    try:result=package(a.work.resolve(),a.bsp.resolve())
    except subprocess.CalledProcessError as e:
        sys.stderr.write(str(e)+'\n')
        for output in (e.stdout,e.stderr):
            if output:sys.stderr.write(output.decode() if isinstance(output,bytes) else output)
        raise SystemExit(1)
    print(json.dumps(result,indent=2))
