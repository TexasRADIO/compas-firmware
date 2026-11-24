"""Power-cycle the release journal on both FM-1 flash chips, including cuts.

Uses official fm1-emu 0.5.1 and virtual NOR only. Each interrupted commit is
followed by a fresh boot, not inferred merely from a checksum.
"""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

root=Path(__file__).resolve().parents[1];board=root/'build/board'
emulator=Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='58e4d6b12d70d4e3f8a3cb692fc86939899ff125570cc4b6f296a4732242f56e'
subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),'store','prepare'],check=True,capture_output=True)
image=board/'emu-store.xip';sections=(board/'compas-store.sections').read_text()
def symbol(name):return int(re.search(r'^([0-9a-f]+).*\s'+name+r'$',sections,re.M)[1],16)-0x1c00000
assert 'compas_unit_store_end' in sections
has_level='kumbia_level' in sections
out=Path(tempfile.mkdtemp(prefix='release-store-',dir=board));print(out,flush=True)
seed=b'\xff'*(1<<20)
def record(flash,base):
    magic,gen,crc=struct.unpack_from('<4sII',flash,base)
    size={b'CPS4':3792,b'CPS5':3796,b'CPS6':3797}.get(magic,0)
    data=flash[base+256:base+256+size]
    return (gen,data) if size and zlib.crc32(struct.pack('<I',gen)+data)==crc else None
def run(chip,name,flash,keys='',cut=None):
    folder=out/chip/name;folder.mkdir(parents=True)
    nor=folder/'flash.bin';nor.write_bytes(flash)
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_STRICT='1',FM1_NO_PLAY='1',FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'))
    if cut:env['FM1_FLASH_CUT']=str(cut)
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--flash-chip',chip,
            '--flash',str(nor),'--midi','none','--usb','--shot','2600'],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    ram=(folder/'ram.bin').read_bytes();after=nor.read_bytes()
    assert after[:0x91000]==flash[:0x91000] and after[0x93000:]==flash[0x93000:]
    assert not ram[symbol('view')+14]
    return after,ram,(folder/'bench.log').read_text()
def check_chip(chip):
    keys=('850:2,920:2^,1000:e3:+1,1050:0,1120:0^,1200:9,1290:9^' if has_level else
          '1000:e3:+1,1200:9,1290:9^')
    first,ram,_=run(chip,'first',seed,keys)
    one=record(first,0x91000);assert one and one[0]==1 and not record(first,0x92000)
    second,_,_=run(chip,'second',first,keys)
    two=record(second,0x92000);assert two and two[0]==2 and record(second,0x91000)==one
    assert two[1][27]==one[1][27]+1
    if has_level:assert first[0x91000:0x91004]==b'CPS6' and one[1][-1]==95 and two[1][-1]==90
    third,_,_=run(chip,'third',second,keys)
    assert record(third,0x91000)[0]==3 and record(third,0x92000)==two
    _,boot,_=run(chip,'boot-second',second)
    assert boot[symbol('view')+7]==two[1][27]
    if has_level:assert boot[symbol('view')+83]==90
    cuts=[]
    for cut in range(1,18):
        torn,_,log=run(chip,f'cut-{cut}',first,keys,cut)
        assert f'power cut at op {cut}' in log
        assert record(torn,0x91000)==one and record(torn,0x92000) is None
        _,boot,_=run(chip,f'reboot-{cut}',torn)
        assert boot[symbol('view')+7]==one[1][27]
        if has_level:assert boot[symbol('view')+83]==95
        cuts.append(cut)
    damaged=bytearray(second);damaged[0x92100+27]^=1
    _,boot,_=run(chip,'corrupt-latest',bytes(damaged))
    assert boot[symbol('view')+7]==one[1][27]
    if has_level:assert boot[symbol('view')+83]==95
    return {'chip':chip,'generations':[1,2,3],'payloadBytes':len(one[1]),'savedWithoutFixtureMarker':True,
        'powerCycleRestoredShow':True,'cutOperationsRebootedToPrevious':cuts,
        'corruptLatestBootedPrevious':True,'outsideJournalUnchanged':True,
        'independentKumbiaLevelPowerCycleAndCutFallback':has_level}
with ThreadPoolExecutor(max_workers=2) as pool:chips=list(pool.map(check_chip,('p25q80','uc25hq80')))
report={'imageSHA256':hashlib.sha256(image.read_bytes()).hexdigest(),
    'emulatorSHA256':hashlib.sha256(emulator.read_bytes()).hexdigest(),'physicalUnitTouched':False,'chips':chips}
(out/'store.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
