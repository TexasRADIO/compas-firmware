"""Strict offline saved rhythm slots, recalled under full fifteen-voice load."""
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
import zlib

board=Path(__file__).resolve().parents[1]/'build/board'
store_mode='--store' in sys.argv[2:]
speed_mode='--speed' in sys.argv[2:]
all_rhythms='--all-rhythms' in sys.argv[2:]
usb_mode='--usb' in sys.argv[2:]
root=board.parents[1]
rhythm_count=(len(json.loads((root/'repertoire/cr8000-conga-excerpts.json').read_text())['patterns'])+
              len(json.loads((root/'repertoire/afro-cuban-727-cells.json').read_text())['presets'])+
              len(json.loads((root/'repertoire/merengue-727-cells.json').read_text())['patterns'])+
              len(json.loads((root/'repertoire/bolero-727-cells.json').read_text())['patterns'])+
              len(json.loads((root/'repertoire/colombian-cumbia-cells.json').read_text())['patterns'])+
              1) if all_rhythms else 8  # Includes Roland Step Write.
extra=(rhythm_count-8)*4000
duration=140000+extra
variant='store' if store_mode else 'show-slots'
emulator=Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(Path(__file__).with_name('board-emu.py')),str(board),variant,'prepare'],check=True,capture_output=True)
image=board/('emu-'+variant+'.xip')
names={'view','board_stats','compas_supervisor_watermark','rack_sequence','rack_pattern','board_rhythm_only','compas_display_stats','active_rhythm','rhythm_slots','speed','kumbia'}
symbols={row.split()[-1]:int(row.split()[0],16)-0x01C00000
         for row in (board/('compas-store.sections' if store_mode else 'compas.sections')).read_text().splitlines()
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
press(17600,7);press(17800,9);press(18000,13);press(18200,11);press(18400,12)
if speed_mode:
    for time,delta in [(20500,-25),(52000,-15),(72000,18),(92000,22)]:
        press(time,8);press(time+200,2) # HOME -> FX/Mix
        # One physical detent at a time. A zero-duration multi-detent jump
        # aliases the quadrature scanner differently in fast/strict emulation.
        for i in range(abs(delta)):
            at=time+400+i*100
            actions.append((at,f'{at}:e5:{1 if delta>0 else -1:+d}'))
        press(time+500+abs(delta)*100,13);press(time+700+abs(delta)*100,11)
press(45000,2);press(65000,2);press(85000,2)
for t in range(100000,132000+extra,4000):actions.append((t,f'{t}:e6:+1'))
press(135000+extra,12);press(135200+extra,7)
actions.append((135400+extra,f'{135400+extra}:e0:+1'))
press(135600+extra,9);press(136000+extra,15);press(136200+extra,8);press(136500+extra,12);press(138000+extra,12)
keys=','.join(value for _,value in sorted(actions))
out=Path(tempfile.mkdtemp(prefix='rebajada-' if speed_mode else 'show-store-' if store_mode else 'show-slots-',dir=os.environ.get('COMP_REHEARSAL_TMP',board)))
print(out,flush=True)
def run(time,strict=False,source_flash=None):
    folder=out/('reboot' if source_flash else 'strict' if strict and time==duration else
                'strict-'+str(time) if strict else str(time));folder.mkdir()
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
    if strict: env.update(FM1_STRICT='1',FM1_BENCH_WAV=str(folder/'audio.raw'),FM1_BENCH_JSON=str(folder/'bench.json'))
    flash_args=[]
    if store_mode:
        flash=folder/'virtual.bin';seed=bytearray(b'\xff'*(1<<20));seed[0x92000:0x92008]=b'CMPASIM1'
        flash.write_bytes(source_flash.read_bytes() if source_flash else seed)
        flash_args=['--flash',str(flash)]
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none',*(['--usb'] if usb_mode else []),*flash_args,'--bench',str(time)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    ram=(folder/'ram.bin').read_bytes()
    return ram

checkpoints=[17500,17900,44000,50000,70000,90000,135850+extra,136350+extra,137100+extra]+list(range(103500,132000+extra,4000))
speed_times=(24000,54500,74800,95200)
if speed_mode:checkpoints += list(speed_times)
def checkpoint(time):
    return run(time,strict=speed_mode and time in speed_times)
with ThreadPoolExecutor(max_workers=4) as pool:
    full=pool.submit(run,duration,True)
    cases=dict(zip(checkpoints,pool.map(checkpoint,checkpoints)))
    ram=full.result()
final=ram[symbols['view']:symbols['view']+40]
stats=struct.unpack_from('<10I',ram,symbols['board_stats'])
stack=struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0]
assert list(struct.unpack_from('<15I',cases[17500],symbols['rack_pattern']))==[0x11111111]*15
assert [cases[t][symbols['rack_sequence']] for t in (44000,50000,70000,90000)]==[1,2,0,1]
assert cases[50000][symbols['board_rhythm_only']]==1
assert not any(stats[2:5]) and 0<stats[1]<stats[9] and stats[9]+240<128*544
assert not final[0] and final[28]==1
assert [cases[t][symbols['active_rhythm']] for t in range(103500,132000+extra,4000)]==list(range(rhythm_count))
assert ram[symbols['active_rhythm']]==255
assert list(struct.unpack_from('<15I',cases[136350+extra],symbols['rack_pattern']))==[0x11111111]*15
assert cases[137100+extra][symbols['view']]==1
if speed_mode:
    assert [cases[t][symbols['speed']] for t in speed_times]==[75,60,78,100]
slots=ram[symbols['rhythm_slots']:symbols['rhythm_slots']+16*151]
assert slots[151:155]==b'C7R1' and slots[2*151:2*151+4]==b'C7R1'
assert slots[151+140]==255 and slots[2*151+140]==rhythm_count-1
assert slots[151+10]==1 and slots[2*151+10]==1
if store_mode:
    stored=(out/'strict/virtual.bin').read_bytes()
    magic,generation,crc=struct.unpack_from('<4sII',stored,0x91000)
    size=3780 if 'kumbia' in symbols else 3776
    saved=stored[0x91100:0x91100+size]
    assert magic==(b'CPS3' if 'kumbia' in symbols else b'CPS2')
    assert generation==2 and zlib.crc32(struct.pack('<I',generation)+saved)==crc
    assert saved[1360:3776]==ram[symbols['rhythm_slots']:symbols['rhythm_slots']+16*151]
    if 'kumbia' in symbols:
        assert saved[3776:]==bytes((0,100,12,0))
    reboot=run(1600,source_flash=out/'strict/virtual.bin')
    assert reboot[symbols['view']+28]==1 and reboot[symbols['rack_sequence']]==1
    assert list(struct.unpack_from('<15I',reboot,symbols['rack_pattern']))==[0x11111111]*15
    assert 'flash: 0 erases / programs' in (out/'reboot/bench.log').read_text()
strict=out/'strict';raw=(strict/'audio.raw').read_bytes();pcm=array.array('h',raw);left=pcm[::2]
assert left==pcm[1::2]
bench=json.loads((strict/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(left)
assert 0<=offset<44100
peaks=[]
for start,end in [(20000,44000),(46000,64000),(66000,84000),(86000,99000)]:
    peaks.append(max(map(abs,left[start*44100//1000-offset:end*44100//1000-offset])))
assert min(peaks)>100
assert not any(left[(139000+extra)*44100//1000-offset:])
with wave.open(str(out/'saved-show.wav'),'wb') as wav:
    wav.setparams((2,2,44100,0,'NONE','not compressed'));wav.writeframes(bytes(offset*4)+raw)
display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
def gate(i):
    a,b,c,px=display[i*4:i*4+4]
    return dict(computeMs=a/24000,transferMs=b/24000,completeMs=c/24000,pixels=px)
result=dict(virtualFlash=store_mode,rebootedAllFifteenLanes=store_mode,durationMs=duration,strictCallbacks=stats[0],maxCallbackUs=stats[1]/24,peakIrqWindowUs=stats[9]/24,deadlineUs=128*544/24,irqUnmeasuredAllowanceUs=10,deadlines=stats[2],lateHalves=stats[3],rails=stats[4],supervisorStackBytes=stack,savedSlotRecallVerified=True,allFifteenLanesVerified=True,allEightCongaCuesVerified=True,rhythmCuesVerified=rhythm_count,layerSoloBypassVerified=True,stereoPeaks=peaks,R1=gate(0),R2=gate(1),R3=gate(2),imageSHA256=hashlib.sha256(image.read_bytes()).hexdigest(),keyScript=keys)
if speed_mode:result['strictSpeedCheckpoints']=[75,60,78,100]
(out/'saved-show.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
assert 0<display[0]<=96000 and 0<display[2]<=288000 and display[3]<=6000
assert 0<display[6]<=960000 and 0<display[8]<=960000 and 0<display[10]<=2880000
