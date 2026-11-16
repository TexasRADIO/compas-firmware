"""Strict offline 727 editor: program all fifteen lanes, layer/solo/bypass, pages and STOP."""
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

board=Path(__file__).resolve().parents[1]/'build/board'
emulator=Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(Path(__file__).with_name('board-emu.py')),str(board),'rack-editor','prepare'],check=True,capture_output=True)
image=board/'emu-rack-editor.xip'
names={'view','board_stats','compas_supervisor_watermark','rack_sequence','rack_pattern','board_rhythm_only','compas_display_stats'}
symbols={row.split()[-1]:int(row.split()[0],16)-0x01C00000
         for row in (board/'compas.sections').read_text().splitlines()
         if row.split() and row.split()[-1] in names}
actions=[(1000,'1000:pot:760')]
def press(time,key):
    actions.extend([(time,f'{time}:{key}'),(time+90,f'{time+90}:{key}^')])
press(850,7);press(1250,15);press(1450,8)
for key in (22,24,27,29):press(1650,key)
press(1850,13);press(2050,11)
for sound in range(15):
    time=2300+sound*1000
    for key in (14,18,22,26):press(time,key)
    press(time+250,1)
    for key in (14,18,22,26):press(time+450,key)
    press(time+650,0)
    if sound<14:actions.append((time+850,f'{time+850}:e2:+1'))
press(18000,12)
press(45000,2);press(65000,2);press(85000,2)
press(100000,11)
for key in (14,16,18,19,21,23,25,28):press(101000,key)
for time in (104000,107000,110000,113000):actions.append((time,f'{time}:e0:+1'))
press(117000,11)
press(135000,12)
keys=','.join(value for _,value in sorted(actions))
out=Path(tempfile.mkdtemp(prefix='rack-editor-',dir=board))
print(out,flush=True)
def run(time,strict=False):
    folder=out/('strict' if strict else str(time));folder.mkdir()
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
    if strict: env.update(FM1_STRICT='1',FM1_BENCH_WAV=str(folder/'audio.raw'),FM1_BENCH_JSON=str(folder/'bench.json'))
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--bench',str(time)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    ram=(folder/'ram.bin').read_bytes()
    return ram

checkpoints=[17500,44000,50000,70000,90000,118000]
with ThreadPoolExecutor(max_workers=4) as pool:
    full=pool.submit(run,138000,True)
    cases=dict(zip(checkpoints,pool.map(run,checkpoints)))
    ram=full.result()
final=ram[symbols['view']:symbols['view']+40]
stats=struct.unpack_from('<10I',ram,symbols['board_stats'])
stack=struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0]
assert list(struct.unpack_from('<15I',ram,symbols['rack_pattern']))==[0x11111111]*15
assert [cases[t][symbols['rack_sequence']] for t in (44000,50000,70000,90000)]==[1,2,0,1]
assert cases[50000][symbols['board_rhythm_only']]==1
assert not any(stats[2:5]) and 0<stats[1]<stats[9] and stats[9]+240<128*544
assert not final[0] and final[28]==1
strict=out/'strict';raw=(strict/'audio.raw').read_bytes();pcm=array.array('h',raw);left=pcm[::2]
assert left==pcm[1::2]
bench=json.loads((strict/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(left)
assert 0<=offset<44100
peaks=[]
for start,end in [(20000,44000),(46000,64000),(66000,84000),(86000,99000)]:
    peaks.append(max(map(abs,left[start*44100//1000-offset:end*44100//1000-offset])))
assert min(peaks)>100
assert not any(left[136000*44100//1000-offset:])
with wave.open(str(out/'rack-editor.wav'),'wb') as wav:
    wav.setparams((2,2,44100,0,'NONE','not compressed'));wav.writeframes(bytes(offset*4)+raw)
display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
def gate(i):
    a,b,c,px=display[i*4:i*4+4]
    return dict(computeMs=a/24000,transferMs=b/24000,completeMs=c/24000,pixels=px)
result=dict(durationMs=138000,strictCallbacks=stats[0],maxCallbackUs=stats[1]/24,peakIrqWindowUs=stats[9]/24,deadlineUs=128*544/24,irqUnmeasuredAllowanceUs=10,deadlines=stats[2],lateHalves=stats[3],rails=stats[4],supervisorStackBytes=stack,allFifteenLanesVerified=True,layerSoloBypassVerified=True,stereoPeaks=peaks,R1=gate(0),R2=gate(1),R3=gate(2),imageSHA256=hashlib.sha256(image.read_bytes()).hexdigest(),keyScript=keys)
(out/'rack-editor.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
assert 0<display[2]<=288000 and display[3]<=6000
assert 0<display[6]<=960000 and 0<display[10]<=2880000
