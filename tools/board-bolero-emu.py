"""Strict audition of the saved bolero show, including restored editor selection."""
import array, hashlib, json, os, struct, subprocess, sys, tempfile, wave
from pathlib import Path

root=Path(__file__).resolve().parents[1];board=root/'build/board'
emulator=Path(sys.argv[1]).resolve();show=Path(sys.argv[2]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),'store','prepare'],check=True,capture_output=True)
out=Path(tempfile.mkdtemp(prefix='bolero-live-',dir=board));print(out,flush=True)
subprocess.run(['node',str(root/'tools/board-show-flash.mjs'),str(show),str(out/'virtual.bin')],check=True,capture_output=True)
keys='900:pot:760,1100:12,1190:12^,7500:e6:+1,16000:13,16090:13^,17000:11,17090:11^,20000:12,20090:12^'
env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
env.update(FM1_STRICT='1',FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(out/'ram.bin'),FM1_BENCH_PANEL=str(out/'panel.ppm'),FM1_BENCH_WAV=str(out/'audio.raw'),FM1_BENCH_JSON=str(out/'bench.json'))
with (out/'bench.log').open('w') as log:
    subprocess.run([str(emulator),'--bin',str(board/'emu-store.xip'),'--entry','0x02000120','--midi','none','--bench','22000','--flash',str(out/'virtual.bin')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
names={'view','board_stats','compas_display_stats','compas_supervisor_watermark','rhythm_origin','panel'}
symbols={r.split()[-1]:int(r.split()[0],16)-0x01c00000 for r in (board/'compas-store.sections').read_text().splitlines() if r.split() and r.split()[-1] in names}
ram=(out/'ram.bin').read_bytes();v=ram[symbols['view']:symbols['view']+76]
stats=struct.unpack_from('<10I',ram,symbols['board_stats']);display=struct.unpack_from('<12I',ram,symbols['compas_display_stats'])
assert not any(stats[2:5]) and not v[14]
assert not v[0] and v[1]==5 and v[28]==1 and v[8]==16
assert ram[symbols['rhythm_origin']]==17
assert ram[symbols['panel']+3]==2  # Saved conga lane, not empty high bongo.
assert stats[9]+240<128*544
assert display[0]<=96000 and display[2]<=288000 and display[3]<=6000
assert display[6]<=960000 and display[8]<=960000 and display[10]<=2880000
raw=(out/'audio.raw').read_bytes();pcm=array.array('h',raw)
assert pcm[::2]==pcm[1::2] and not any(pcm[-44100:])
bench=json.loads((out/'bench.json').read_text());offset=bench['emulatedMs']*44100//1000-len(pcm)//2
peaks=[max(map(abs,pcm[(t*44100-offset)*2:((t+1)*44100-offset)*2])) for t in (3,14)]
assert min(peaks)>100
with wave.open(str(out/'Bolero-Live.wav'),'wb') as wav:
    wav.setparams((2,2,44100,0,'NONE','not compressed'));wav.writeframes(bytes(offset*4)+raw)
gates={k:dict(zip(('computeMs','transferMs','completeMs'),[n/24000 for n in display[i*4:i*4+3]]),pixels=display[i*4+3]) for i,k in enumerate(('R1','R2','R3'))}
result=dict(durationMs=22000,callbacks=stats[0],peakIrqUs=stats[9]/24,deadlineMisses=stats[2],lateHalves=stats[3],railSamples=stats[4],slot=v[28],study=v[1],muted=v[8],sourceOrigin=17,editorSound=2,stopped=True,stereoPeaks=peaks,supervisorStackBytes=struct.unpack_from('<I',ram,symbols['compas_supervisor_watermark'])[0],gates=gates,imageSHA256=hashlib.sha256((board/'emu-store.xip').read_bytes()).hexdigest(),physicalUnitTouched=False,keyScript=keys,R2ClockStartsAt='debounced foreground acceptance')
(out/'bolero-live.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
