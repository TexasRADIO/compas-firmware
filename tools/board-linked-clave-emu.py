"""Strict phrase-locked source/clave control on a saved son/salsa show."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
root=Path(__file__).resolve().parents[1];board=root/'build/board'
emulator=Path(sys.argv[1]).resolve();show=Path(sys.argv[2]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),'store','prepare'],check=True,capture_output=True)
out=Path(tempfile.mkdtemp(prefix='linked-clave-',dir=board));print(out,flush=True)
subprocess.run(['node',str(root/'tools/board-show-flash.mjs'),str(show),str(out/'seed.bin')],check=True,capture_output=True)
keys='900:pot:760,1000:5,1090:5^,1200:12,1290:12^,2200:1,2290:1^,6800:0,6890:0^,7100:1,7190:1^,8200:12,8290:12^,8500:0,8590:0^,9000:7,9090:7^,9300:9,9390:9^,9600:8,9690:8^,10000:e6:+1,10400:e6:-1,10800:5,10890:5^,11400:12,11490:12^,14000:12,14090:12^'
names={'view','board_stats','compas_display_stats','compas_supervisor_watermark','rack_pattern','rack_pattern_accents','rhythm_origin'}
symbols={r.split()[-1]:int(r.split()[0],16)-0x01c00000 for r in (board/'compas-store.sections').read_text().splitlines() if r.split() and r.split()[-1] in names}
def run(t):
 folder=out/str(t);folder.mkdir();(folder/'virtual.bin').write_bytes((out/'seed.bin').read_bytes())
 env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
 env.update(FM1_STRICT='1',FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/'ram.bin'),FM1_BENCH_PANEL=str(folder/'panel.ppm'))
 with (folder/'bench.log').open('w') as log:
  subprocess.run([str(emulator),'--bin',str(board/'emu-store.xip'),'--entry','0x02000120','--midi','none','--bench',str(t),'--flash',str(folder/'virtual.bin')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
 return (folder/'ram.bin').read_bytes()
times=[3000,6500,7600,11200,15500]
with ThreadPoolExecutor(max_workers=2) as pool: states=list(pool.map(run,times))
rows=[]
for t,ram,direction,pending,pattern,playing in zip(times,states,[0,1,1,0,0],[1,2,2,2,2],[13,12,12,13,13],[1,1,1,0,0]):
 v=ram[symbols['view']:symbols['view']+76];stats=struct.unpack_from('<10I',ram,symbols['board_stats']);display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
 assert (v[0],v[1],v[28],v[39]&1,v[39]>>1,v[70])==(playing,6,0,direction,pending,pattern),(t,list(v))
 assert ram[symbols['rhythm_origin']]==pattern
 hits=struct.unpack_from('<15I',ram,symbols['rack_pattern']);accents=struct.unpack_from('<15I',ram,symbols['rack_pattern_accents'])
 assert hits==((0x45554555,0x10001000,0x04150555,0x50005000,0x01400000) if direction else (0x45554555,0x10001000,0x05550415,0x50005000,0x00000140))+(0,)*10
 assert accents==(0,0x10001000,0x00100010,0x50005000,0x01400000 if direction else 0x00000140)+(0,)*10
 assert not any(stats[2:5]) and stats[9]+240<128*544
 assert display[0]<=96000 and display[2]<=288000 and display[3]<=6000 and display[6]<=960000 and display[8]<=960000 and display[10]<=2880000
 gates={k:dict(zip(('computeMs','transferMs','completeMs'),[n/24000 for n in display[i*4:i*4+3]]),pixels=display[i*4+3]) for i,k in enumerate(('R1','R2','R3'))}
 rows.append(dict(timeMs=t,clave=direction,nextClave=pending,rhythm=pattern,playing=bool(playing),callbacks=stats[0],peakCallbackUs=stats[1]/24,peakIrqUs=stats[9]/24,deadlineMisses=stats[2],lateHalves=stats[3],rails=stats[4],gates=gates,supervisorStackBytes=struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0]))
result=dict(imageSHA256=hashlib.sha256((board/'emu-store.xip').read_bytes()).hexdigest(),states=rows,keyScript=keys,irqAllowanceUs=10,deadlineUs=128*544/24,R2ClockStartsAt='debounced foreground acceptance',physicalUnitTouched=False)
(out/'linked-clave.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
