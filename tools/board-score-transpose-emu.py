"""Strict score transposition: cue/cancel/recall and every registered score."""
import array, hashlib, json, os, runpy, struct, subprocess, sys, tempfile
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
root=Path(__file__).resolve().parents[1]
board=root/'build/board'
emulator=Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
for variant in ('offline','store'):
 subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),variant,'prepare'],check=True,capture_output=True)
out=Path(tempfile.mkdtemp(prefix='score-transpose-',dir=board));print(out,flush=True)
ready='900:pot:760,1100:7,1190:7^,1600:23,1690:23^,1950:8,2040:8^'
play=ready+',2250:12,2340:12^,2800:e3:+1,3100:e5:-1,3350:e5:-1'
recall=play+',7000:12,7090:12^,7400:7,7490:7^,7700:9,7790:9^,8000:8,8090:8^,8300:e6:-1,8600:e6:+1,9300:12,9390:12^'
registry=json.loads((root/'repertoire/board-scores.json').read_text())
shifts=[(-6,-3,-1,1,3,5)[i] if i<6 else 0 for i in range(len(registry))]
duration=len(registry)*22000+6000
prepared={'format':1,'songs':[dict(title='SCORE '+str(i+1),study=s['id'],bpm=180,guide=True,transpose=shifts[i]) for i,s in enumerate(registry)]}
(out/'All-Scores.json').write_text(json.dumps(prepared,indent=2)+'\n')
compiler=runpy.run_path(str(root/'tools/board-set.py'))
seed=compiler['virtual_flash'](compiler['compile_set'](prepared))
actions=['900:pot:760','1100:12','1190:12^']
for time in range(22000,len(registry)*22000,22000): actions.append(f'{time}:e6:+1')
# Live requests include cancellation, both range limits, and foreground page changes.
for i in range(len(registry)):
 time=5000+i*22000
 first=-1 if shifts[i]==5 else 1
 actions.extend([f'{time}:e5:{first:+d}',f'{time+250}:e5:{-first:+d}',f'{time+1000}:e5:-1',f'{time+2500}:e0:+1',f'{time+3500}:e0:-1'])
actions.extend([f'{duration-2000}:12',f'{duration-1910}:12^'])
def run(item):
 name,time,keys,expected=item
 folder=out/name;folder.mkdir()
 storage=name=='load';variant='store' if storage else 'offline'
 image=board/f'emu-{variant}.xip'
 names={'view','board_stats','compas_display_stats','compas_supervisor_watermark'}
 symbols={row.split()[-1]:int(row.split()[0],16)-0x01c00000 for row in (board/('compas-store.sections' if storage else 'compas.sections')).read_text().splitlines() if row.split() and row.split()[-1] in names}
 env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
 env.update(FM1_STRICT='1',FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'),FM1_BENCH_WAV=str(folder/'audio.raw'),FM1_BENCH_JSON=str(folder/'bench.json'))
 command=[str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--bench',str(time)]
 if storage:
  flash=folder/'virtual.bin';flash.write_bytes(seed);command.extend(['--flash',str(flash)])
 with (folder/'bench.log').open('w') as log:
  subprocess.run(command,env=env,cwd=folder,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1200)
 ram=(folder/'ram.bin').read_bytes();v=ram[symbols['view']:symbols['view']+76]
 stats=struct.unpack_from('<10I',ram,symbols['board_stats']);display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
 assert not any(stats[2:5]) and stats[9]+240<128*544
 assert (v[75]&15,v[75]>>4)==expected,(name,list(v))
 assert v[31]==(len(registry) if storage else 2) and v[36]==1,(name,list(v))
 if storage: assert v[28]==len(registry)-1 and not v[0]
 else: assert v[0] and v[28]==9
 pcm=array.array('h',(folder/'audio.raw').read_bytes())
 assert max(map(abs,pcm))>100
 if storage:
  # Each score's steady window must contain music and the final stop must silence it.
  bench=json.loads((folder/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(pcm)//2
  for start in range(10,len(registry)*22,22):
   lo=(start*44100-offset)*2;hi=lo+44100*2
   assert lo>=0 and max(map(abs,pcm[lo:hi]))>100,(name,start)
  assert not any(pcm[-44100:])
 gates={k:dict(zip(('computeMs','transferMs','completeMs'),[n/24000 for n in display[i*4:i*4+3]]),pixels=display[i*4+3]) for i,k in enumerate(('R1','R2','R3'))}
 assert display[0]<=96000 and display[2]<=288000 and display[3]<=6000,(name,gates)
 assert display[6]<=960000 and display[8]<=960000 and display[10]<=2880000,(name,gates)
 return dict(name=name,durationMs=time,key=v[75]&15,nextKey=v[75]>>4,score=v[31],callbacks=stats[0],maxCallbackUs=stats[1]/24,irqUs=stats[9]/24,deadlineMisses=stats[2],lateHalves=stats[3],rails=stats[4],imageSHA256=hashlib.sha256(image.read_bytes()).hexdigest(),**gates)
cases=[('queued',3700,play,(0,10)),('landed',6500,play,(10,12)),('cancelled',4200,play+',3750:e5:+2',(0,12)),('recall',10200,recall,(10,12)),('load',duration,','.join(actions),(max(-6,shifts[-1]-1)%12,12))]
if '--load-only' in sys.argv[2:]: cases=cases[-1:]
with ThreadPoolExecutor(max_workers=2) as pool: results=list(pool.map(run,cases))
report={'results':results,'physicalUnitTouched':False,'irqUnmeasuredAllowanceUs':10,'R2ClockStartsAt':'debounced input accepted by foreground scan'}
(out/'score-transpose.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
