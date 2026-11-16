"""Play two complete Sonidero animation cycles in strict FM-1 emulation."""
import argparse
from pathlib import Path
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

board = Path(__file__).resolve().parents[1] / "build/board"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator", type=Path)
parser.add_argument("--storage-receipt", type=Path,
                    help="test the already linked storage image bound to this full-load receipt")
args = parser.parse_args()
emulator = args.emulator.resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest() == (
    "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
)
if args.storage_receipt:
    image = board / "emu-store.xip"
    symbols = board / "compas-store.sections"
else:
    subprocess.run([sys.executable,str(Path(__file__).with_name("board-emu.py")),str(board),
                    "demo","prepare"],check=True,capture_output=True)
    image = board / "emu-demo.xip"
    symbols = board / "compas.sections"
image_sha = hashlib.sha256(image.read_bytes()).hexdigest()
if args.storage_receipt and json.loads(args.storage_receipt.read_text())["imageSHA256"] != image_sha:
    parser.error("The full-load receipt belongs to a different storage image.")
out=Path(tempfile.mkdtemp(prefix="animations-",dir=board))
print(out,flush=True)
edges=[(1450,12),(1970,7),(2300,15),(2700,8),(23000,10),(23250,26),(27000,11),
       (40000,12),(66000,10),(66250,26),(72000,11)]
# The first sign-off leaves the next slot ready. Re-select the opening groove
# while stopped so the second cycle cues the same live song change.
enc=[(8000,1,1),(13000,1,1),(17500,1,-2),
     (39500,6,-1),(41100,6,1),(46000,1,1),(53000,1,1),(59000,1,-2)]
actions=[(900,"900:pot:760")]
for at,key in edges:
    actions.extend([(at,f"{at}:{key}"),(at+85,f"{at+85}:{key}^")])
for at,index,delta in enc:
    actions.append((at,f"{at}:e{index}:{delta:+}"))
keys=",".join(item for _,item in sorted(actions))
env={k:v for k,v in os.environ.items() if not k.startswith("FM1_")}
env.update(FM1_STRICT="1",FM1_BENCH_KEYS=keys,FM1_DUMPRAM=str(out/"ram.bin"),
           FM1_BENCH_PANEL=str(out/"panel.ppm"))
with (out/"bench.log").open("w") as log:
    subprocess.run([str(emulator),"--bin",str(image),"--entry","0x02000120","--midi","none",
                    "--bench","138000"],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,
                   check=True,timeout=600)
sections=symbols.read_text()
ram=(out/"ram.bin").read_bytes()

def read(name,count):
    address=int(re.search(r"^([0-9a-f]+).*\s"+name+r"$",sections,re.M)[1],16)-0x01c00000
    return struct.unpack_from(f"<{count}I",ram,address)

counts=read("compas_animation_counts",5)
stats=read("board_stats",10)
timing=read("compas_animation_ticks",8)
def frame(offset):
    c,t,e,p=timing[offset:offset+4]
    return {"computeMs":c/24000,"transferMs":t/24000,"completeMs":e/24000,"pixels":p}
assert hashlib.sha256(image.read_bytes()).hexdigest() == image_sha
receipt={"imageSHA256":image_sha,"variant":"store" if args.storage_receipt else "demo",
         "animationTriggerCounts":dict(zip(("A1","A2","A3","A4","A5"),counts)),
         "A1Frame0":frame(0),"A2Frame0":frame(4),"durationMs":138000,"callbacks":stats[0],
         "maxCallbackUs":stats[1]/24,"peakIrqUs":stats[9]/24,"deadlineMisses":stats[2],"lateHalves":stats[3],"clips":stats[4],
         "keys":keys}
(out/"animations.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt,indent=2))
assert all(n>=2 for n in counts)
assert not any(stats[2:5]) and stats[9]+240<128*544
assert 0<timing[3]<=13000 and 0<timing[7]<=13000
