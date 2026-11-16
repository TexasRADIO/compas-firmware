"""Exercise the linked Kumbia kit in the pinned FM-1 emulator."""
from array import array
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

board=Path(__file__).resolve().parents[1]/"build/board"
emulator=Path(sys.argv[1]).resolve()
assert sha256(emulator.read_bytes()).hexdigest()=="8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
image=board/"emu-store.xip"
sections=(board/"compas-store.sections").read_text()
symbols={row.split()[-1]:int(row.split()[0],16)-0x01c00000
         for row in sections.splitlines() if re.match(r"^[0-9a-f]{8} ",row)}
sizes={m[1]:int(m[2],16) for m in re.finditer(r"^\s+\d+\s+(\.\S+)\s+([0-9a-f]{8})\s",sections,re.M)}
out=Path(tempfile.mkdtemp(prefix="kumbia-rehearsal-",dir=board))
print(out,flush=True)
seed=bytearray(b"\xff"*(1<<20))
seed[0x92000:0x92008]=b"CMPASIM1"
actions=[(850,"e0:-1"),(1000,"pot:1010")]
def tap(time,key):
    actions.extend(((time,str(key)),(time+90,f"{key}^")))

def turn(time,encoder,steps):
    for i in range(abs(steps)):
        actions.append((time+i*100,f"e{encoder}:{1 if steps>0 else -1:+d}"))

turn(1300,2,100)
turn(1310,3,-95)
turn(1320,4,12)
turn(1330,5,54)
tap(11900,12)
tap(12900,20)
tap(13200,19)
tap(18000,2)
tap(18200,2)
for time in range(22000,132000,4000):
    tap(time,18+(time//4000)%4)
    tap(time+120,14+(time//4000)%4)
turn(47000,3,95)
turn(47030,5,-54)
turn(106000,3,-95)
turn(106030,5,54)
tap(133500,12)
tap(134000,9)
script=",".join(f"{t}:{a}" for t,a in sorted(actions))

def run(label,time,keys=script,strict=False,flash=None):
    folder=out/str(label)
    folder.mkdir()
    nor=folder/"virtual.bin"
    nor.write_bytes(flash if flash is not None else seed)
    env={k:v for k,v in os.environ.items() if not k.startswith("FM1_")}
    env.update(FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(folder/"ram.bin"),FM1_BENCH_PANEL=str(folder/"panel.ppm"))
    if strict:
        env.update(FM1_STRICT="1",FM1_BENCH_WAV=str(folder/"audio.raw"),FM1_BENCH_JSON=str(folder/"bench.json"))
    with (folder/"bench.log").open("w") as log:
        subprocess.run([str(emulator),"--bin",str(image),"--entry","0x02000120","--midi","none",
                        "--usb","--flash",str(nor),"--bench",str(time)],cwd=folder,env=env,
                       stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    return folder,(folder/"ram.bin").read_bytes()

def grab(ram,name,length):
    start=symbols[name]
    return ram[start:start+length]

limits=bytes((100,5,24,54))
with ThreadPoolExecutor(max_workers=3) as pool:
    long=pool.submit(run,"strict",138000,strict=True)
    cases={t:pool.submit(run,t,t,strict=True) for t in (13800,15400,19000,59000)}
    evidence={t:future.result()[1] for t,future in cases.items()}
    assert grab(evidence[13800],"kumbia",4)==limits
    assert grab(evidence[13800],"hanan_pattern",1)==b"\0"
    assert grab(evidence[13800],"hanan_next",1)==b"\1"
    assert grab(evidence[15400],"hanan_pattern",1)==b"\1"
    assert grab(evidence[15400],"hanan_next",1)==b"\4"
    assert grab(evidence[19000],"board_hanan_mode",1)==b"\1"
    assert grab(evidence[59000],"kumbia",4)==bytes((100,100,24,0))
    folder,ram=long.result()

nor=(folder/"virtual.bin").read_bytes()
magic,generation,crc=struct.unpack_from("<4sII",nor,0x90000)
payload=nor[0x90100:0x90100+3780]
assert magic==b"CPS3" and generation==1 and payload[-4:]==limits
assert zlib.crc32(struct.pack("<I",generation)+payload)==crc
assert nor[:0x90000]==seed[:0x90000] and nor[0x91000:]==seed[0x91000:]
_,reboot=run("reboot",1700,keys="850:e0:-1",flash=nor)
assert grab(reboot,"kumbia",4)==limits
assert grab(reboot,"view",1)==b"\0"

stats=struct.unpack("<10I",grab(ram,"board_stats",40))
assert not any(stats[2:5]) and 0<stats[1]<stats[9] and stats[9]+240<128*544,stats
pcm=array("h",(folder/"audio.raw").read_bytes())
assert pcm[::2]==pcm[1::2]
left=pcm[::2]
bench=json.loads((folder/"bench.json").read_text())
offset=bench["emulatedMs"]*44100//1000-len(left)
assert 0<=offset<44100
def peak(start,end):
    return max(map(abs,left[start*44100//1000-offset:end*44100//1000-offset]))
peaks=[peak(a,b) for a,b in ((12000,17000),(19000,45000),(59000,104000),(119000,132000),(136000,137500))]
assert min(peaks[:4])>100 and peaks[-1]==0,peaks
display=struct.unpack("<12I",grab(ram,"compas_display_stats",48))
def gate(index):
    compute,transfer,complete,pixels=display[index*4:index*4+4]
    return dict(computeMs=compute/24000,transferMs=transfer/24000,completeMs=complete/24000,pixels=pixels)

report=dict(durationMs=138000,callbacks=stats[0],peakCallbackUs=stats[1]/24,peakIrqUs=stats[9]/24,
            irqDeadlineUs=128*544/24,irqMeasurementAllowanceUs=10,deadlineMisses=stats[2],
            lateHalves=stats[3],railSamples=stats[4],supervisorStackBytes=struct.unpack("<I",grab(ram,"compas_supervisor_watermark",4))[0],
            staticRamBytes=sum(sizes.get(section,0) for section in (".ram_text",".bss",".data",".pool",".noinit")),
            imageBytes=image.stat().st_size, imageSha256=sha256(image.read_bytes()).hexdigest(),
            peakPCMByPhase=peaks,kitSettingsRebootVerified=True,liveCueVerified=True,R1=gate(0),R2=gate(1),R3=gate(2))
(out/"receipt.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
assert display[2]<=288000 and display[3]<=6000 and display[6]<=960000 and display[10]<=2880000
