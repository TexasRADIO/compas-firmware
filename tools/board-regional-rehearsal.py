"""Strict offline rehearsal of the prepared regional bank, with live energy and layers."""
import array
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

root=Path(__file__).resolve().parents[1]
board=root/'build/board'
set_path=root/'repertoire/regional-audition-set.json'
songs=json.loads(set_path.read_text())['songs']
studies=json.loads((root/'src/score.json').read_text())['studies']
study_ids={study['id']:i for i,study in enumerate(studies)}
emulator=Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(Path(__file__).with_name('board-emu.py')),str(board),'store','prepare'],check=True,capture_output=True)
image=board/'emu-store.xip'
names={'view','board_stats','compas_supervisor_watermark'}
symbols={row.split()[-1]:int(row.split()[0],16)-0x01C00000
         for row in (board/'compas-store.sections').read_text().splitlines()
         if row.split() and row.split()[-1] in names}
cues=[1600+13000*i for i in range(len(songs))]
duration=138000
stop=cues[-1]+13000
assert stop+1000<duration
actions=[(1000,'1000:pot:760')]
def press(time,key):
    actions.extend([(time,f'{time}:{key}'),(time+90,f'{time+90}:{key}^')])
press(cues[0],12)
for i,time in enumerate(cues):
    if i: actions.append((time,f'{time}:e6:+1'))
    press(time+6600,28) # G4, the white Break key
for key in (22,24,27,29): press(3500,key)
press(stop,12)
keys=','.join(value for _,value in sorted(actions))
out=Path(tempfile.mkdtemp(prefix='compas-regional-'))
seed=out/'Regional.seed.bin'
subprocess.run([sys.executable,str(root/'tools/board-set.py'),'--virtual-flash',str(set_path),str(seed)],check=True)
print(out,flush=True)
def run(time,strict=False):
    folder=out/('strict' if strict else str(time));folder.mkdir()
    flash=folder/'virtual.bin';flash.write_bytes(seed.read_bytes())
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
    if strict: env.update(FM1_STRICT='1',FM1_BENCH_WAV=str(folder/'audio.raw'),FM1_BENCH_JSON=str(folder/'bench.json'))
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--flash',str(flash),'--bench',str(time)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    ram=(folder/'ram.bin').read_bytes()
    return (ram[symbols['view']:symbols['view']+76],struct.unpack_from('<10I',ram,symbols['board_stats']),struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0])
checkpoints=[t+6100 for t in cues]+[t+12500 for t in cues]
with ThreadPoolExecutor(max_workers=4) as pool:
    full=pool.submit(run,duration,True)
    cases=dict(zip(checkpoints,pool.map(run,checkpoints)))
    final,stats,stack=full.result()
assert [cases[t+6100][0][28] for t in cues]==list(range(len(songs)))
assert [cases[t+6100][0][1] for t in cues]==[study_ids[s['study']] for s in songs]
assert all(cases[t+12500][0][0] and cases[t+12500][0][44]==4 for t in cues), 'Break did not land'
assert all(cases[t+6100][0][0] for t in cues)
assert not any(stats[2:5]) and 0<stats[1]<stats[9] and stats[9]+240<128*544
assert not final[0] and final[28]==len(songs)-1
strict=out/'strict';raw=(strict/'audio.raw').read_bytes();pcm=array.array('h',raw);left=pcm[::2]
assert left==pcm[1::2]
bench=json.loads((strict/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(left)
assert 0<=offset<44100
peaks=[]
for time in cues:
    peaks.append(max(map(abs,left[(time+6100)*44100//1000-offset:(time+12500)*44100//1000-offset])))
assert min(peaks)>100
assert not any(left[(stop+1000)*44100//1000-offset:])
with wave.open(str(out/'regional.wav'),'wb') as wav:
    wav.setparams((2,2,44100,0,'NONE','not compressed'));wav.writeframes(bytes(offset*4)+raw)
result=dict(durationMs=duration,strictCallbacks=stats[0],maxCallbackUs=stats[1]/24,peakIrqWindowUs=stats[9]/24,deadlineUs=128*544/24,irqUnmeasuredAllowanceUs=10,deadlines=stats[2],lateHalves=stats[3],rails=stats[4],supervisorStackBytes=stack,slotsAtNativeCheckpoints=list(range(1,len(songs)+1)),studies=[s['study'] for s in songs],breakLandedForEveryStyle=True,stereoPeakBySlotWindowInStrictRun=peaks,imageSHA256=hashlib.sha256(image.read_bytes()).hexdigest(),keyScript=keys)
(out/'regional.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
