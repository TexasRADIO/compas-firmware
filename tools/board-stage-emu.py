"""Capture the Sonidero pages and read the T2-R TIMER4 gates from strict FM-1."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

board = Path(__file__).resolve().parents[1] / "build/board"
emulator = Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest() == (
    "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
)
subprocess.run([sys.executable, str(Path(__file__).with_name("board-emu.py")), str(board),
                "demo", "prepare"], check=True, capture_output=True)
image = board / "emu-demo.xip"
sections = (board / "compas.sections").read_text()
symbol = lambda name: int(re.search(r"^([0-9a-f]+).*\s"+name+r"$", sections, re.M)[1],16)-0x01c00000
ram_bytes = sum(int(re.search(r"^\s*\d+\s+"+re.escape(section)+r"\s+([0-9a-f]+)", sections, re.M)[1],16)
                for section in (".bss", ".data", ".noinit", ".ram_text"))
slot_bytes=int(re.search(r"^[0-9a-f]+.*\s([0-9a-f]+)\s+rhythm_slots$",sections,re.M)[1],16)
stripe_bytes=int(re.search(r"^[0-9a-f]+.*\s([0-9a-f]+)\s+stripe$",sections,re.M)[1],16)
out = Path(tempfile.mkdtemp(prefix="stage-", dir=board))
print(out, flush=True)
live = "900:pot:760,1050:e6:+1,1450:12,1540:12^"
cue = live + ",2300:26,2390:26^"
probe = cue + (",2560:26,2650:26^,5100:e1:+1,5450:e1:-1,6150:e5:-17,"
               "7200:e0:+1,8200:e0:+1,9200:e0:+1,10200:7,10290:7^")
scenes = {
    "Band-Ready": (1080, ""),
    "Band-Live": (3000, cue),
    "Bass": (8000, probe),
    "Percussion": (9000, probe),
    "727-Rack": (10000, probe),
    "Set-List": (11000, probe),
    "Score": (5300, "900:pot:760,1100:7,1190:7^,1600:22,1690:22^,1950:8,2040:8^,"
                    "2250:12,2340:12^,3500:22,3590:22^,4100:24,4190:24^"),
    "strict": (11500, probe),
}
score_ready="900:pot:760,1100:7,1190:7^,1600:23,1690:23^,1950:8,2040:8^"
score_play=score_ready+",2250:12,2340:12^,2800:e3:+1,3100:e4:+1"
scenes.update({
    "Score-Ready": (2600,score_ready),
    "Score-Knobs": (3700,score_play),
    "Score-Coda": (4400,score_play+",3800:11,3890:11^,4100:e4:+1"),
    "Score-Bass": (4300,score_play+",3800:e0:+1"),
    "Score-Percussion": (5000,score_play+",3800:e0:+1,4500:e0:+1"),
})

def run(item):
    name, (duration, keys) = item
    folder = out / name
    folder.mkdir()
    env = {k:v for k,v in os.environ.items() if not k.startswith("FM1_")}
    env.update(FM1_BENCH_KEYS=keys, FM1_BENCH_PANEL=str(folder/"panel.ppm"),
               FM1_DUMPRAM=str(folder/"ram.bin"))
    if name=="strict" or name.startswith("Score-"):
        env["FM1_STRICT"]="1"
    with (folder/"bench.log").open("w") as log:
        subprocess.run([str(emulator),"--bin",str(image),"--entry","0x02000120","--midi","none",
                        "--bench",str(duration)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,
                       check=True,timeout=240)
    ram = (folder/"ram.bin").read_bytes()
    return ram

with ThreadPoolExecutor(max_workers=4) as pool:
    results = dict(zip(scenes, pool.map(run, scenes.items())))
score_receipts={}
for name in (name for name in scenes if name.startswith("Score-")):
    ram=results[name]
    # Byte layout is asserted by verify_score_controls in board-host.c.
    view=ram[symbol("view"):symbol("view")+76]
    assert view[31]==2 and view[73]==1, (name,"wrong score or missing bolero")
    assert bool(view[36])==(name!="Score-Ready"), (name,"guide encoder")
    assert bool(view[34])==(name not in ("Score-Ready","Score-Coda")), (name,"hold encoder")
    if name=="Score-Coda": assert view[35] and not view[34], "hold defeated coda"
    sample=struct.unpack_from("<10I",ram,symbol("board_stats"))
    gates=struct.unpack_from("<12I",ram,symbol("compas_display_stats"))
    assert not any(sample[2:5]) and sample[9]+240<128*544
    assert gates[0]<=96000 and gates[2]<=288000 and gates[3]<=6000
    assert gates[6]<=960000 and gates[8]<=960000 and gates[10]<=2880000
    score_receipts[name]={"callbacks":sample[0],"peakIrqUs":sample[9]/24,
        "guide":bool(view[36]),"hold":bool(view[34]),"bolero":bool(view[73]),
        "R1ComputeTransferCompleteMs":[n/24000 for n in gates[:3]],
        "R2ComputeTransferCompleteMs":[n/24000 for n in gates[4:7]],
        "R3ComputeTransferCompleteMs":[n/24000 for n in gates[8:11]]}
strict = results["strict"]
stats = struct.unpack_from("<10I",strict,symbol("board_stats"))
display = struct.unpack_from("<12I",strict,symbol("compas_display_stats"))
def gate(index):
    compute,transfer,complete,pixels=display[index*4:(index+1)*4]
    return {"computeMs":compute/24000,"transferMs":transfer/24000,"completeMs":complete/24000,
            "pixels":pixels}

receipt={"imageSHA256":hashlib.sha256(image.read_bytes()).hexdigest(),
         "durationMs":scenes["strict"][0],"strictCallbacks":stats[0],"peakIrqUs":stats[9]/24,
         "lateHalves":stats[3],"deadlineMisses":stats[2],"clips":stats[4],"linkedStaticRam":ram_bytes,
         "rhythmSlotBytes":slot_bytes,"drawingStripeBytes":stripe_bytes,
         "nonSlotStaticRam":ram_bytes-slot_bytes,
         "R1":gate(0),"R2":gate(1),"R3":gate(2),
         "scoreControls":score_receipts,
         "R2ClockStartsAt":"debounced input accepted by foreground scan",
         "measurement":"Each column is an independent maximum. Transfer includes LCD wait and window setup.",
         "keyScript":probe}
(out/"stage.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt,indent=2))
assert not any(stats[2:5]) and stats[9]+240<128*544
# The display's original <600 B growth gate excludes the explicitly measured
# musical slot bank. Keep the existing drawing strip, and check total SRAM too.
assert slot_bytes==16*151 and stripe_bytes==6720
assert ram_bytes-slot_bytes-13076<600 and ram_bytes<72*1024
assert 0<display[0]<=96000 and 0<display[2]<=288000 and 0<display[3]<=6000
assert 0<display[6]<=960000
assert 0<display[8]<=960000 and 0<display[10]<=2880000
