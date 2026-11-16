"""Strict Bass-page key cue, phrase landing and explicit save/recall."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
board=Path(__file__).resolve().parents[1]/'build/board'
emulator=Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(Path(__file__).with_name('board-emu.py')),str(board),'offline','prepare'],check=True,capture_output=True)
image=board/'emu-offline.xip'
names={'view','board_stats','compas_display_stats','compas_supervisor_watermark'}
symbols={row.split()[-1]:int(row.split()[0],16)-0x01C00000 for row in (board/'compas.sections').read_text().splitlines() if row.split() and row.split()[-1] in names}
actions=['850:13','940:13^','1050:e6:+1','1150:4','1240:4^','1350:pot:760',
 '1450:1','1540:1^','1650:12','1740:12^','2300:1','2390:1^','2500:1','2590:1^',
 '7000:0','7090:0^','7200:1','7290:1^','8000:12','8090:12^','8300:7','8390:7^',
 '8500:e0:+1','8700:9','8790:9^','9000:8','9090:8^','9300:e6:+1','9500:e6:-1',
 '9800:4','9890:4^','10200:12','10290:12^']
out=Path(tempfile.mkdtemp(prefix='key-cue-',dir=board));print(out,flush=True)
def run(name,time):
 folder=out/name;folder.mkdir()
 env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
 env.update(FM1_STRICT='1',FM1_BENCH_KEYS=','.join(actions),FM1_DUMPRAM=str(folder/'ram.bin'),
  FM1_BENCH_PANEL=str(folder/'panel.ppm'),FM1_BENCH_WAV=str(folder/'audio.raw'))
 with (folder/'bench.log').open('w') as log:
  subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--bench',str(time)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
 ram=(folder/'ram.bin').read_bytes();view=ram[symbols['view']:symbols['view']+76]
 stats=struct.unpack_from('<10I',ram,symbols['board_stats']);display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
 assert view[0] and not any(stats[2:5]) and stats[9]+240<128*544
 assert max(abs(n[0]) for n in struct.iter_unpack('<h',(folder/'audio.raw').read_bytes()))>100
 assert view[1]==1 and not view[31] and not view[68] and view[74]==15
 assert view[75]==(0x31 if name=='queued' else 0xc3), (name,view[75])
 assert view[28]==(1 if name=='recall' else 16)
 gates={k:dict(zip(('computeMs','transferMs','completeMs'),[n/24000 for n in display[i*4:i*4+3]]),pixels=display[i*4+3]) for i,k in enumerate(('R1','R2','R3'))}
 assert display[0]<=96000 and display[2]<=288000 and display[3]<=6000
 assert display[6]<=960000 and display[8]<=960000 and display[10]<=2880000
 return dict(key=view[75]&15,nextKey=view[75]>>4,song=view[28],study=view[1],nextGroove=view[74],repeat=view[30],rackMode=view[68],callbacks=stats[0],maxCallbackUs=stats[1]/24,irqUs=stats[9]/24,deadlineMisses=stats[2],lateHalves=stats[3],rails=stats[4],**gates)
with ThreadPoolExecutor(max_workers=2) as pool:
 results=list(pool.map(lambda item:run(*item),[('queued',3500),('landed',6800),('cancelled',7600),('recall',11500)]))
report=dict(imageSHA256=hashlib.sha256(image.read_bytes()).hexdigest(),queued=results[0],landed=results[1],cancelled=results[2],recall=results[3],keyScript=','.join(actions))
(out/'key-cue.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
