"""Check real target count-in numerals against filled cones at 60/180 BPM.

Run separately after each language's full release build. The saved show is
in virtual NOR; this script has no hardware access.
"""
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
board=root/'build/board'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('emulator',type=Path)
parser.add_argument('--study',type=int,default=1)
args=parser.parse_args();emulator=args.emulator.resolve()
studies=[s for s in json.loads((root/'src/score.json').read_text())['studies'] if not s.get('testOnly')]
study=studies[args.study]
compound=isinstance(study['meter'],list) and study['meter'][1]==8
beats=study['meter'][0]//3 if compound else study['meter']
steps_per_pulse=6 if compound else 4
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),'store','prepare'],check=True,capture_output=True)
image=board/'emu-store.xip'
sections=(board/'compas-store.sections').read_text()
def symbol(name):return int(re.search(r'^([0-9a-f]+).*\s'+name+r'$',sections,re.M)[1],16)-0x01c00000
compiler=runpy.run_path(str(root/'tools/board-set.py'))
out=Path(tempfile.mkdtemp(prefix='count-in-',dir=board))
print(out,flush=True)
def run(case):
    bpm,beat=case
    folder=out/f'{bpm}-{beat+1}';folder.mkdir()
    flash=folder/'flash.bin'
    flash.write_bytes(compiler['virtual_flash'](compiler['compile_set']({'format':1,'songs':[
        {'title':'COUNT-IN','study':study['id'],'bpm':bpm,'countIn':True}]}),unit_store=True))
    original=flash.read_bytes()
    duration=1000+int((beat+.5)*60000/bpm)
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_STRICT='1',FM1_BENCH_KEYS='850:pot:760,1000:12,1090:12^',
        FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none',
            '--flash',str(flash),'--usb','--bench',str(duration)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    ram=(folder/'ram.bin').read_bytes();v=ram[symbol('view'):symbol('view')+80]
    stats=struct.unpack_from('<11I',ram,symbol('board_stats'))
    ppm=(folder/'panel.ppm').read_bytes();header,pixels=ppm.split(b'\n255\n',1)
    assert header in (b'P6\n240 240',b'P6\n1280 820')
    # The pinned native player records its whole panel: the 240px LCD is
    # scaled exactly 2x at (370,42). Keep native LCD captures supported too.
    width=1280 if header.endswith(b'1280 820') else 240
    def pink(x,y):
        if width==1280:x,y=370+x*2,42+y*2
        r,g,b=pixels[(y*width+x)*3:(y*width+x)*3+3]
        return r>220 and g<80 and 80<b<180
    numerals=[sum(pink(138-beats*25+i*50+x,31+y) for y in range(18) for x in range(16)) for i in range(beats)]
    # d=9 is lit on a filled cone and dark on a hollow upcoming cone.
    cones=[pink(18+22*i,117) for i in range(beats)]
    record={'bpm':bpm,'beat':beat+1,'position':v[5],'countFlags':v[33],
        'numeralPinkPixels':numerals,'filledPinkCones':cones,'language':ram[symbol('panel_lang')],
        'deadlineMisses':stats[2],'lateHalves':stats[3],'clips':stats[4],'peakIrqUs':stats[9]/24,
        'virtualFlashUnchanged':flash.read_bytes()==original}
    record['passed']=bool(v[0] and v[1]==args.study and v[6]==bpm and v[33]&2 and v[5]//steps_per_pulse==beat
        and [n>0 for n in numerals]==[i==beat for i in range(beats)] and cones==[i==beat for i in range(beats)]
        and not any(stats[2:5]) and record['virtualFlashUnchanged'])
    return record
with ThreadPoolExecutor(max_workers=3) as pool:
    cases=list(pool.map(run,[(bpm,beat) for bpm in (60,180) for beat in range(beats)]))
report={'imageSHA256':hashlib.sha256(image.read_bytes()).hexdigest(),'study':study['id'],'beatsPerBar':beats,'cases':cases,
        'scope':'strict target rendered pixels at mid-beat; first-boundary latency is covered by display gates'}
(out/'count-in.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
assert all(c['passed'] for c in cases)
