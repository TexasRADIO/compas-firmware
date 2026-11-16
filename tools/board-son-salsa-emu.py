"""Saved source-percussion arrangements: atomic clave/part recall under strict load."""
import array, hashlib, json, os, struct, subprocess, sys, tempfile, wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
root=Path(__file__).resolve().parents[1]; board=root/'build/board'
emulator=Path(sys.argv[1]).resolve(); show=Path(sys.argv[2]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),'store','prepare'],check=True,capture_output=True)
out=Path(tempfile.mkdtemp(prefix='son-salsa-live-',dir=board)); print(out,flush=True)
subprocess.run(['node',str(root/'tools/board-show-flash.mjs'),str(show),str(out/'seed.bin')],check=True,capture_output=True)
keys='900:pot:760,1100:12,1190:12^,8000:e6:+1,20000:e6:+1,32000:e6:+1,44000:13,44090:13^,45000:11,45090:11^,49000:12,49090:12^'
names={'view','board_stats','compas_display_stats','compas_supervisor_watermark','rhythm_origin','panel'}
symbols={r.split()[-1]:int(r.split()[0],16)-0x01c00000 for r in (board/'compas-store.sections').read_text().splitlines() if r.split() and r.split()[-1] in names}
def run(duration,strict=False):
 folder=out/str(duration);folder.mkdir();(folder/'virtual.bin').write_bytes((out/'seed.bin').read_bytes())
 env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
 env.update(FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
 if strict:env.update(FM1_STRICT='1',FM1_BENCH_WAV=str(folder/'audio.raw'),FM1_BENCH_JSON=str(folder/'bench.json'))
 with (folder/'bench.log').open('w') as log:
  subprocess.run([str(emulator),'--bin',str(board/'emu-store.xip'),'--entry','0x02000120','--midi','none','--bench',str(duration),'--flash',str(folder/'virtual.bin')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
 ram=(folder/'ram.bin').read_bytes();return ram
with ThreadPoolExecutor(max_workers=3) as pool:
 full=pool.submit(run,52000,True)
 checkpoints=list(pool.map(run,[7000,18000,30000,42000]))
 ram=full.result()
rows=[]
for i,data in enumerate(checkpoints):
 v=data[symbols['view']:symbols['view']+76]
 assert v[0] and v[28]==i and v[1]==[6,6,2,2][i]
 assert v[8]==[18,18,2,2][i] and v[39]&1==i%2
 assert v[70]==data[symbols['rhythm_origin']]==[13,12,10,9][i]
 assert v[68]==1 and not v[14]
 rows.append(dict(slot=i,study=v[1],muted=v[8],clave=v[39]&1,origin=data[symbols['rhythm_origin']],layer=True))
v=ram[symbols['view']:symbols['view']+76];stats=struct.unpack_from('<10I',ram,symbols['board_stats']);display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
assert not v[0] and v[28]==3 and not any(stats[2:5]) and not v[14]
assert stats[9]+240<128*544
assert display[0]<=96000 and display[2]<=288000 and display[3]<=6000
assert display[6]<=960000 and display[8]<=960000 and display[10]<=2880000
assert ram[symbols['panel']]==3 and ram[symbols['panel']+3]==2 and ram[symbols['panel']+18]
folder=out/'52000';raw=(folder/'audio.raw').read_bytes();pcm=array.array('h',raw)
assert pcm[::2]==pcm[1::2] and not any(pcm[-44100:])
bench=json.loads((folder/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(pcm)//2
assert 0<=offset<44100
peaks=[max(map(abs,pcm[(t*44100-offset)*2:((t+1)*44100-offset)*2])) for t in [3,16,28,40]];assert min(peaks)>100
with wave.open(str(out/'Son-Salsa-Live.wav'),'wb') as wav:
 wav.setparams((2,2,44100,0,'NONE','not compressed'));wav.writeframes(bytes(offset*4)+raw)
gates={k:dict(zip(('computeMs','transferMs','completeMs'),[n/24000 for n in display[i*4:i*4+3]]),pixels=display[i*4+3]) for i,k in enumerate(('R1','R2','R3'))}
result=dict(durationMs=52000,callbacks=stats[0],peakCallbackUs=stats[1]/24,peakIrqUs=stats[9]/24,irqAllowanceUs=10,deadlineUs=128*544/24,deadlineMisses=stats[2],lateHalves=stats[3],railSamples=stats[4],checkpoints=rows,stopped=True,editorSound=2,stereoPeaks=peaks,supervisorStackBytes=struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0],gates=gates,imageSHA256=hashlib.sha256((board/'emu-store.xip').read_bytes()).hexdigest(),physicalUnitTouched=False,keyScript=keys,R2ClockStartsAt='debounced foreground acceptance')
(out/'son-salsa-live.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
