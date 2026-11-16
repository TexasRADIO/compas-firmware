"""Strict offline Cielito controls, mixed score/groove set and display budgets."""
import array, hashlib, json, os, runpy, struct, subprocess, sys, tempfile
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
prepared=json.loads((root/'repertoire/cielito-rehearsal-set.json').read_text())
seed=compiler['virtual_flash'](compiler['compile_set'](prepared))
actions=[(900,'900:pot:760')]
def press(t,key):actions.extend([(t,f'{t}:{key}'),(t+90,f'{t+90}:{key}^')])
def turn(t,index,delta):actions.append((t,f'{t}:e{index}:{delta:+d}'))
press(1100,12)
for t,key in [(2000,22),(2600,22),(3200,24),(5000,24),
              (7000,26),(10500,23),(21000,21),(26000,30)]:press(t,key)
for t,delta in [(14000,-1),(14200,-1),(14400,-1),(17000,1),(17200,1),(17400,1)]:turn(t,5,delta)
turn(32000,6,1);press(33000,12)
turn(50000,6,1);turn(72000,6,1);turn(90000,6,-1)
# Foreground page changes; rack finger-drumming during the groove.
for t in (36000,56000,76000,96000):
 turn(t,0,1);turn(t+1000,0,1);turn(t+2000,0,-2)
press(80000,13)
for t in range(80300,84000,300):press(t,14+(t//300)%15)
press(85000,8)
press(112000,30);press(120000,12);press(136000,12)
keys=','.join(value for _,value in sorted(actions))
out=Path(tempfile.mkdtemp(prefix='cielito-live-',dir=board));print(out,flush=True)
(out/'Prepared-Set.json').write_text(json.dumps(prepared,indent=2)+'\n')
def run(item):
 name,time,strict=item;folder=out/name;folder.mkdir()
 flash=folder/'virtual.bin';flash.write_bytes(seed)
 env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
 env.update(FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
 if strict:env.update(FM1_STRICT='1',FM1_BENCH_WAV=str(folder/'audio.raw'),FM1_BENCH_JSON=str(folder/'bench.json'))
 with (folder/'bench.log').open('w') as log:
  subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--bench',str(time),'--flash',str(flash)],env=env,cwd=folder,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1200)
 ram=(folder/'ram.bin').read_bytes();v=ram[symbols['view']:symbols['view']+76]
 stats=struct.unpack_from('<10I',ram,symbols['board_stats'])
 display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
 # Pinned BSP scan diagnostics: 35 words precede enc_moves[7]/enc_lost[7].
 scan=struct.unpack_from('<59I',ram,symbols['fm1_in_stat'])
 assert not any(stats[2:5]) and not v[14],(name,stats,list(v))
 result=dict(name=name,timeMs=time,playing=v[0],slot=v[28],nextSlot=v[29],score=v[31],form=v[32],bar=v[33],nextPart=v[45],guide=v[36],beats=v[37],key=v[75]&15,
             encoderTransitions=list(scan[35:42]),encoderMissedTransitions=list(scan[42:49]))
 if strict and time==138000:
  assert stats[9]+240<128*544
  gates={k:dict(zip(('computeMs','transferMs','completeMs'),[n/24000 for n in display[i*4:i*4+3]]),pixels=display[i*4+3]) for i,k in enumerate(('R1','R2','R3'))}
  assert display[0]<=96000 and display[2]<=288000 and display[3]<=6000,gates
  assert display[6]<=960000 and display[8]<=960000 and display[10]<=2880000,gates
  pcm=array.array('h',(folder/'audio.raw').read_bytes())
  bench=json.loads((folder/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(pcm)//2
  peaks=[]
  for start in (10,40,60,82,100,130):
   lo=(start*44100-offset)*2;hi=lo+44100*2
   assert lo>=0;peaks.append(max(map(abs,pcm[lo:hi])))
  assert min(peaks)>100 and not any(pcm[-44100:]),peaks
  result.update(callbacks=stats[0],maxCallbackUs=stats[1]/24,irqUs=stats[9]/24,
                deadlineMisses=stats[2],lateHalves=stats[3],rails=stats[4],windowPeaks=peaks,
                supervisorStackBytes=struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0],**gates)
 return result
cases=[('chorus',9000,False),('verse',15000,False),('sing',40000,False),
       ('lower',60000,False),('groove',82000,False),('back-to-score',100000,False),('strict',138000,True)]
if '--diagnose' in sys.argv:
 cases=[('sing',40000,True),('lower',60000,True),('groove',82000,True),('back-to-score',100000,True)]
 for arg in sys.argv:
  if arg.startswith('--probe='):cases=[('probe-'+t,int(t),True) for t in arg.split('=',1)[1].split(',')]
with ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(run,cases))
if '--diagnose' in sys.argv:
 print(json.dumps(results,indent=2));sys.exit(0)
assert [(r['slot'],r['score']) for r in results]==[(0,7),(0,7),(1,7),(2,7),(3,0),(2,7),(2,7)],results
assert results[0]['form']==3 and results[1]['form']==2,results
assert results[2]['guide']==0 and results[3]['key']==9 and results[5]['key']==9,results
assert not results[-1]['playing'] and all(r['beats']==3 for r in results if r['score']),results
assert results[-1]['encoderTransitions'][6]==16 and results[-1]['encoderMissedTransitions'][6]==0,results[-1]
report=dict(results=results,imageSHA256=hashlib.sha256(image.read_bytes()).hexdigest(),physicalUnitTouched=False,
            irqUnmeasuredAllowanceUs=10,R2ClockStartsAt='debounced input accepted by foreground scan',keys=keys)
(out/'cielito-live.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
