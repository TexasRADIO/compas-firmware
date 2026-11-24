"""Boot actual bilingual .fwsc packages on both virtual FM-1 flash chips."""
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import struct
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('emulator',type=Path);parser.add_argument('packages',nargs=2,type=Path)
parser.add_argument('--symbols-root',type=Path,default=root/'build/qualification-final-20261121')
args=parser.parse_args();emulator=args.emulator.resolve();packages=[p.resolve() for p in args.packages]
assert len(packages)==2
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='58e4d6b12d70d4e3f8a3cb692fc86939899ff125570cc4b6f296a4732242f56e'
api=runpy.run_path(str(root/'tools/board-usb-emu.py'))
out=Path(tempfile.mkdtemp(prefix='package-smoke-',dir=root/'build/board'));print(out,flush=True)
def run(case):
    package,chip=case;meta=json.loads(package.with_suffix('.json').read_text());lang=meta['language']
    assert hashlib.sha256(package.read_bytes()).hexdigest()==meta['sha256']
    symbols=args.symbols_root/lang/'compas-store.sections'
    if not symbols.exists():symbols=args.symbols_root/lang/'build/board/compas-store.sections'
    sections=symbols.read_text()
    def symbol(name):return int(re.search(r'^([0-9a-f]+).*\s'+name+r'$',sections,re.M)[1],16)-0x1c00000
    folder=out/(lang+'-'+chip);folder.mkdir();script=folder/'usb.txt'
    script.write_text(';'.join(f'{t}:{api["wire"]["frame"](0x11,p).hex()}' for t,p in ((1800,b''),(2000,b'\1'))))
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_STRICT='1',FM1_NO_PLAY='1',FM1_BENCH_KEYS='900:pot:760,1200:12,1290:12^,3000:12,3090:12^',
        FM1_USB_SEND='@'+str(script),FM1_DUMPRAM=str(folder/'ram.bin'))
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--fwsc',str(package),'--flash-chip',chip,'--midi','none','--usb','--shot','3600'],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    ram=(folder/'ram.bin').read_bytes();log=(folder/'bench.log').read_text();ids=[];states=[]
    for t,h in re.findall(r'\[\s*(\d+) ms\] usb in: +([0-9a-f ]+)',log):
        msg=bytes.fromhex(h);decoded=api['decode_app_message'](msg)
        if decoded is not None:ids.append(decoded[1].rstrip(b'\0').decode())
        elif len(msg)==19:states.append((int(t),msg[9],msg[14]))
    stats=struct.unpack_from('<11I',ram,symbol('board_stats'))
    expected=0 if lang=='en' else 1
    assert ids==['FM-1_922','FM-1_92210003'] and any(s[2] for s in states) and not states[-1][2]
    assert all(s[1]==expected for s in states) and not ram[symbol('view')+14]
    assert not any(stats[2:5]) and stats[9]+240<128*544
    return {'package':package.name,'sha256':meta['sha256'],'language':lang,'chip':chip,
        'identities':ids,'playAndStop':True,'callbacks':stats[0],'peakIrqUs':stats[9]/24,
        'misses':stats[2],'lateHalves':stats[3],'clips':stats[4]}
with ThreadPoolExecutor(max_workers=2) as pool:cases=list(pool.map(run,[(p,c) for p in packages for c in ('p25q80','uc25hq80')]))
report={'scope':'actual package boot and virtual USB; no physical unit','cases':cases}
(out/'package-smoke.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
