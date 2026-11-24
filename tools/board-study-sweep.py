"""Exercise every production study/part in a separate strict target test image.

Build with COMP_EMU_SWEEP=1 in addition to the full release flags. The test
image injects normal queued commands; it is never a packaging input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "build/board"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator", type=Path)
parser.add_argument("--smoke", action="store_true", help="verify only the first study before the full run")
parser.add_argument('--production-elf',type=Path,default=BOARD/'compas-store.elf',help='pin the matching retained production ELF when the current output folder holds the other language')
args = parser.parse_args()
emulator = args.emulator.resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest() == "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
subprocess.run([sys.executable, str(ROOT / "tools/board-emu.py"), str(BOARD), "sweep", "prepare"],
               check=True, capture_output=True)
image = BOARD / "emu-sweep.xip"
production_digest=hashlib.sha256(args.production_elf.read_bytes()).hexdigest()
sections = (BOARD / "compas-sweep.sections").read_text()
symbol = lambda name: int(re.search(r"^([0-9a-f]+).*\s" + name + r"$", sections, re.M)[1], 16) - 0x01c00000
studies = [study for study in json.loads((ROOT / "src/score.json").read_text())["studies"] if not study.get("testOnly")]
count = len(studies)
expected = 1 if args.smoke else count
# The driver spends 7,500 callbacks per study. The fixed 750-second window
# covered 34 entries but truncates a larger bank before its last parts land.
# Include the phase-reset callback and 1,000 warm-up/tail callbacks explicitly.
duration = 24000 if args.smoke else max(750000, ((count * 7501 + 1000) * 128 * 544 + 23999) // 24000)
out = Path(tempfile.mkdtemp(prefix="study-sweep-", dir=BOARD))
shutil.copy2(image,out/'sweep.xip');image=out/'sweep.xip'
print(out, flush=True)
env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
env.update(FM1_STRICT="1", FM1_BENCH_NOKEYS="1", FM1_BENCH_KEYS="",
           FM1_DUMPRAM=str(out / "ram.bin"), FM1_BENCH_JSON=str(out / "bench.json"))
with (out / "bench.log").open("w") as log:
    subprocess.run([str(emulator), "--bin", str(image), "--entry", "0x02000120",
                    "--midi", "none", "--usb", "--bench", str(duration)],
                   cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
ram = (out / "ram.bin").read_bytes()
stats = struct.unpack_from("<11I", ram, symbol("board_stats"))
parts = list(ram[symbol("compas_sweep_parts"):symbol("compas_sweep_parts") + count])
blocks = struct.unpack_from(f"<{count}I", ram, symbol("compas_sweep_blocks"))
done = struct.unpack_from("<I", ram, symbol("compas_sweep_done"))[0]
refused = struct.unpack_from("<I", ram, symbol("compas_sweep_refused"))[0]
stack = struct.unpack_from("<I", ram, symbol("compas_supervisor_watermark"))[0]
fx_size = int(re.search(r"^[0-9a-f]+.*\s([0-9a-f]+)\s+fx$", sections, re.M)[1], 16)
params = list(ram[symbol("fx") + fx_size - 12:symbol("fx") + fx_size - 7])
view = ram[symbol("view"):symbol("view") + 80]
report = {"scope": "target test image; normal queued commands; all six arrangement parts",
          "imageSHA256": hashlib.sha256(image.read_bytes()).hexdigest(),
          "productionImageSHA256": production_digest,
          "productionImageFormat": "ELF", "durationMs": duration, "smokeOnly": args.smoke,
          "callbacks": stats[0], "peakCallbackUs": stats[1] / 24, "peakIrqUs": stats[9] / 24,
          "deadlineUs": 128 * 544 / 24, "irqAllowanceUs": 10,
          "deadlineMisses": stats[2], "lateHalves": stats[3], "clips": stats[4],
          "rackPeak": stats[6], "totalPcmPeak": stats[8], "effectsShedBlocks": stats[10],
          "supervisorWatermarkBytes": stack, "done": done, "refusedCommands": refused,
          "observedFxParams": params, "observedTempoBpm": view[6], "finalStudyId": view[1],
          "finalResult": view[13], "finalFault": view[14],
          "studies": [{"id": study["id"], "partsMask": parts[i], "playingObservations": blocks[i]}
                      for i, study in enumerate(studies)]}
(out / "sweep.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2), flush=True)
assert not any(stats[2:5]) and not refused and not stats[10]
assert params == [100, 100, 5, 100, 90] and view[6] == 180 and not view[14]
assert stats[9] / 24 <= 2000 and stats[9] + 240 < 128 * 544
assert all(parts[i] == 63 and blocks[i] > 4000 for i in range(expected))
assert args.smoke or done == 1
