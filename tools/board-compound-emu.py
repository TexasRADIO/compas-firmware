"""Qualify private 6/8, hemiola and 12/8 fixtures on the strict target.

Requires COMP_EMU_COMPOUND=1. This isolated image is never packaged.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

board=Path(__file__).resolve().parents[1]/'build/board'
emulator=Path(sys.argv[1]).resolve()
usb_clock='--usb-clock' in sys.argv[2:]
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(Path(__file__).with_name('board-emu.py')),str(board),'compound','prepare'],check=True,capture_output=True)
image=board/'emu-compound.xip'
sections=(board/'compas-compound.sections').read_text()
def symbol(name):
    return int(re.search(r'^([0-9a-f]+).*\s'+name+r'$',sections,re.M)[1],16)-0x01c00000
out=Path(tempfile.mkdtemp(prefix='compound-',dir=board))
print(out,flush=True)
env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
env.update(FM1_STRICT='1',FM1_NO_PLAY='1',FM1_BENCH_NOKEYS='1',FM1_BENCH_KEYS='',FM1_DUMPRAM=str(out/'ram.bin'))
with (out/'bench.log').open('w') as log:
    subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--usb','--shot' if usb_clock else '--bench','95000'],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
ram=(out/'ram.bin').read_bytes()
stats=struct.unpack_from('<11I',ram,symbol('board_stats'))
done,refused=(struct.unpack_from('<I',ram,symbol(name))[0] for name in ('compas_compound_done','compas_compound_refused'))
fields=('study','bpm','steps','pulses','edited','restored','highStep','phraseFrames','stepLow','stepHigh','pulseMask','countMask','quarterBpm','clickMask')
cases=[dict(zip(fields,struct.unpack_from('<14I',ram,symbol('compas_compound_results')+i*56))) for i in range(6)]
for i,case in enumerate(cases):
    steps=48 if i//2==2 else 24
    pulses=(4,5,8)[i//2]
    bpm=180 if i&1 else 60
    expected=steps*44100*60//(bpm*6)
    case.update(expectedPhraseFrames=expected,phraseErrorFrames=abs(case['phraseFrames']-expected))
    case['passed']=(case['steps']==steps and case['bpm']==bpm and case['pulses']==pulses
        and case['edited']==case['restored']==1 and case['highStep']==(steps>32)
        and case['stepLow']==(1<<min(32,steps))-1 and case['stepHigh']==((1<<16)-1 if steps==48 else 0)
        and case['pulseMask']==(1<<pulses)-1 and case['countMask']==(1<<(4 if steps==48 else 2))-1
        and case['quarterBpm']==bpm*3//2 and case['clickMask']==sum(1<<s for s in range(0,steps//2,6))
        and case['phraseErrorFrames']<=1)
clock_windows=[]
if usb_clock:
    messages=[(int(t),bytes.fromhex(h)) for t,h in re.findall(r'\[\s*(\d+) ms\] usb in: +([0-9a-f ]+)',(out/'bench.log').read_text())]
    states=[(t,b[13],b[11]*128+b[12]) for t,b in messages if len(b)==19 and b.startswith(bytes.fromhex('f00022243501')) and b[14]]
    for case in cases:
        matching=[t for t,study,bpm in states if study==case['study'] and bpm==case['bpm']]
        assert matching
        start=min(matching)+1500
        following=[t for t,study,bpm in states if t>min(matching) and (study,bpm)!=(case['study'],case['bpm'])]
        end=(min(following) if following else 95000)-500
        clocks=[t for t,b in messages if start<=t<=end and b==b'\xf8']
        observed=1000*(len(clocks)-1)/(clocks[-1]-clocks[0])
        expected=case['quarterBpm']*24/60
        clock_windows.append({'study':case['study'],'pulseBpm':case['bpm'],'quarterBpm':case['quarterBpm'],
            'clockPackets':len(clocks),'observedHz':observed,'expectedHz':expected})
        assert len(clocks)>50 and abs(observed-expected)<expected*.02
report={'scope':'private meter fixtures; real-bank manual hits; stopped project round trip, not journal reboot',
    'imageSHA256':hashlib.sha256(image.read_bytes()).hexdigest(),'durationMs':95000,
    'done':done,'refusedCommands':refused,'cases':cases,'callbacks':stats[0],'usbClockWindows':clock_windows,
    'peakCallbackUs':stats[1]/24,'peakIrqUs':stats[9]/24,'deadlineUs':128*544/24,
    'deadlineMisses':stats[2],'lateHalves':stats[3],'clips':stats[4],
    'supervisorWatermarkBytes':struct.unpack_from('<I',ram,symbol('compas_supervisor_watermark'))[0]}
(out/'compound.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
assert done==1 and not refused and all(case['passed'] for case in cases)
assert not any(stats[2:5]) and stats[9]+240<128*544
