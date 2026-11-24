"""Exercise the real linked editor on an immutable native image; no hardware."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('emulator',type=Path);p.add_argument('native',type=Path);p.add_argument('out',type=Path)
args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('protocol',Path(__file__).with_name('board-unit-check.py'))
protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)
assert hashlib.sha256(args.emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
scheduled=[];seq=0
def frame(at,kind,payload=()):
    global seq
    seq+=1;scheduled.append((at,protocol.PREFIX+bytes([kind,*protocol.u14(seq),*payload,247])));return seq
frame(1200,1,[0]);expected={}
for groove in range(38):
    at=1600+groove*500;tx=groove+1
    frame(at,0x20,[0,0,0,0,0x10,*protocol.u14(tx)])
    frame(at+90,0x21,[0x10,0,*protocol.u14(0x1100),*protocol.u14(groove)])
    frame(at+180,0x23,protocol.u14(tx))
    expected[frame(at+330,0x10)]=groove
frame(21100,0x40);frame(22100,0x41);frame(22500,0x73)
script=args.out/'usb.txt';script.write_text(';'.join(f'{at}:{b.hex()}' for at,b in scheduled))
flash=args.out/'flash.bin';flash.write_bytes(b'\xff'*(1<<20))
env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
env.update(FM1_STRICT='1',FM1_NO_PLAY='1',FM1_USB_SEND='@'+str(script.resolve()),FM1_DUMPRAM=str((args.out/'ram.bin').resolve()))
run=subprocess.run([str(args.emulator.resolve()),'--bin',str((args.native/'emu-store.xip').resolve()),'--entry','0x02000120',
                   '--midi','none','--flash',str(flash.resolve()),'--usb','--shot','23000'],cwd=args.out,env=env,capture_output=True,text=True,check=True,timeout=180)
(args.out/'run.log').write_text(run.stdout+run.stderr)
messages=[bytes.fromhex(h) for h in re.findall(r'\[\s*\d+ ms\] usb in: +([0-9a-f ]+)',run.stdout+run.stderr)]
seen={};hello=None
for b in messages:
    if not b.startswith(protocol.PREFIX):continue
    kind=b[6];seq=b[7]*128+b[8];body=b[9:-1]
    if kind==2:hello=list(body);assert body[11]*128+body[12]==38
    if kind==0x11:
        assert body[:4]==bytes([0,0,0,1]);raw=protocol.unpack7(body[4:-5])
        import zlib
        assert zlib.crc32(raw)==protocol.read32(body[-5:])
        state=protocol.snapshot_fields(raw);assert state['sections'][0x12][0]==expected[seq]
        seen[seq]=state
assert set(seen)==set(expected) and hello
sections=(args.native/'compas-store.sections').read_text()
symbol=lambda name:int(re.search(r'^([0-9a-f]+).*\s'+name+r'$',sections,re.M)[1],16)-0x1c00000
ram=(args.out/'ram.bin').read_bytes();stats=struct.unpack_from('<11I',ram,symbol('board_stats'))
assert not any(stats[2:5]) and stats[9]/24<=2000 and ram[symbol('view')]==0
assert flash.read_bytes()==b'\xff'*(1<<20)
result={'status':'pass','imageSha256':hashlib.sha256((args.native/'emu-store.xip').read_bytes()).hexdigest(),'grooves':len(seen),
        'hello':hello,'snapshotCrcVerified':True,'callbacks':stats[0],'peakIrqUs':stats[9]/24,'deadlineMisses':stats[2],
        'lateHalves':stats[3],'clips':stats[4],'flashUnchanged':True,'physicalUnitTouched':False}
(args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
