"""Strict offline merengue backing: source variation, Break and meter-changing cues."""
import array, hashlib, json, os, runpy, struct, subprocess, sys, tempfile, wave
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

root=Path(__file__).resolve().parents[1];board=root/'build/board'
emulator=Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),'store','prepare'],check=True,capture_output=True)
image=board/'emu-store.xip'
names={'view','board_stats','compas_display_stats','compas_supervisor_watermark','fm1_in_stat'}
symbols={r.split()[-1]:int(r.split()[0],16)-0x01c00000 for r in (board/'compas-store.sections').read_text().splitlines() if r.split() and r.split()[-1] in names}
compiler=runpy.run_path(str(root/'tools/board-set.py'))
prepared=json.loads((root/'repertoire/merengue-audition-set.json').read_text())
seed=compiler['virtual_flash'](compiler['compile_set'](prepared))
actions=[(900,'900:pot:760')]
def press(t,key):actions.extend([(t,f'{t}:{key}'),(t+90,f'{t+90}:{key}^')])
def turn(t,index,delta):actions.append((t,f'{t}:e{index}:{delta:+d}'))
press(1100,12);turn(6000,1,1);turn(14000,1,1)
for t in (10500,11500,12500,13500):turn(t,0,1)
press(22000,10);press(23500,8)
turn(26000,6,1);turn(36000,6,1);press(46000,12)
keys=','.join(value for _,value in sorted(actions))
out=Path(tempfile.mkdtemp(prefix='merengue-live-',dir=board));print(out,flush=True)
def run(item):
    name,time,strict=item;folder=out/name;folder.mkdir()
    flash=folder/'virtual.bin';flash.write_bytes(seed)
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
    if strict:env.update(FM1_STRICT='1',FM1_BENCH_WAV=str(folder/'audio.raw'),FM1_BENCH_JSON=str(folder/'bench.json'))
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--bench',str(time),'--flash',str(flash)],env=env,cwd=folder,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    ram=(folder/'ram.bin').read_bytes();v=ram[symbols['view']:symbols['view']+76]
    stats=struct.unpack_from('<10I',ram,symbols['board_stats'])
    assert not any(stats[2:5]) and not v[14],(name,stats,list(v))
    result=dict(name=name,timeMs=time,playing=v[0],study=v[1],energy=v[2],slot=v[28])
    if strict:
        assert stats[9]+240<128*544
        display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
        gates={k:dict(zip(('computeMs','transferMs','completeMs'),[n/24000 for n in display[i*4:i*4+3]]),pixels=display[i*4+3]) for i,k in enumerate(('R1','R2','R3'))}
        assert display[0]<=96000 and display[2]<=288000 and display[3]<=6000,gates
        assert display[6]<=960000 and display[8]<=960000 and display[10]<=2880000,gates
        raw=(folder/'audio.raw').read_bytes();pcm=array.array('h',raw)
        assert pcm[::2]==pcm[1::2]
        bench=json.loads((folder/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(pcm)//2
        peaks=[max(map(abs,pcm[(t*44100-offset)*2:((t+1)*44100-offset)*2])) for t in (3,12,20,32,43)]
        assert min(peaks)>100 and not any(pcm[-44100:]),peaks
        scan=struct.unpack_from('<59I',ram,symbols['fm1_in_stat'])
        assert list(scan[35:42])==[16,8,0,0,0,0,8] and not any(scan[42:49]),scan
        with wave.open(str(out/'Merengue-Live.wav'),'wb') as wav:
            wav.setparams((2,2,44100,0,'NONE','not compressed'));wav.writeframes(bytes(offset*4)+raw)
        result.update(callbacks=stats[0],irqUs=stats[9]/24,deadlineMisses=stats[2],lateHalves=stats[3],rails=stats[4],peaks=peaks,supervisorStackBytes=struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0],encoderTransitions=list(scan[35:42]),**gates)
    return result
cases=[('base',4000,False),('lift',13800,False),('break',21000,False),('variation',33000,False),('pambiche',43000,False),('strict',48000,True)]
with ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(run,cases))
assert [(r['slot'],r['study'],r['energy']) for r in results]==[(0,15,0),(0,15,1),(0,15,2),(1,15,1),(2,8,0),(2,8,0)],results
assert all(r['playing'] for r in results[:-1]) and not results[-1]['playing']
report=dict(results=results,imageSHA256=hashlib.sha256(image.read_bytes()).hexdigest(),physicalUnitTouched=False,irqUnmeasuredAllowanceUs=10,R2ClockStartsAt='debounced foreground acceptance',keys=keys)
(out/'merengue-live.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
