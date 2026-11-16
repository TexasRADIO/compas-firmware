"""Verify a continuous ten-slot set on the exact linked FM-1 app."""
import array
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

board = Path(__file__).resolve().parents[1] / "build/board"
emulator = Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest() == (
    "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
), "Expected fm1-live v0.4.0 for Apple Silicon"
prepare = Path(__file__).with_name("board-emu.py")
subprocess.run([sys.executable, str(prepare), str(board), "rehearsal", "prepare"],
               capture_output=True, check=True)
image = board / "emu-rehearsal.xip"
names = {"view", "board_stats", "compas_supervisor_watermark"}
symbols = {row.split()[-1]: int(row.split()[0], 16)-0x01C00000
           for row in (board / "compas.sections").read_text().splitlines()
           if row.split() and row.split()[-1] in names}
duration = 138000
cues = [1250, 11000, 20500, 31000, 42000, 53000, 62500, 73500, 83500, 108000]
buttons = [(850, 7), (1500, 12), (6500, 10), (26000, 10), (70000, 10), (86000, 8), (86300, 22),
           (88000, 10), (97000, 10), (101000, 13), (104000, 7), (112600, 8), (113000, 22), (120600, 7), (121000, 11)]
hits = [(102000, 14+pad) for pad in (0, 2, 4, 5, 6, 7, 9, 11)]
actions = [(time, f"{time}:{key}")
           for time, key in buttons + hits + [(time, 14+slot) for slot, time in enumerate(cues)]]
actions += [(time+90, f"{time+90}:{key}^")
            for time, key in buttons + hits + [(time, 14+slot) for slot, time in enumerate(cues)]]
actions += [(1000, "1000:pot:760"), (91000, "91000:e2:+14"), (115000, "115000:e2:+14")]
keys = ",".join(value for _, value in sorted(actions))
out = Path(tempfile.mkdtemp(prefix="rehearsal-", dir=board))
print(out, flush=True)


def run(time, strict=False):
    run_dir = out / ("strict" if strict else str(time))
    run_dir.mkdir()
    env = {**os.environ, "FM1_BENCH_KEYS": keys, "FM1_DUMPRAM": str(run_dir / "ram.bin"),
           "FM1_BENCH_PANEL": str(run_dir / "panel.ppm")}
    env.pop("FM1_BENCH_NOKEYS", None)
    env.pop("FM1_STRICT", None)
    if strict:
        env.update(FM1_STRICT="1", FM1_BENCH_WAV=str(run_dir / "audio.raw"),
                   FM1_BENCH_JSON=str(run_dir / "bench.json"))
    command = [str(emulator), "--bin", str(image), "--entry", "0x02000120", "--midi", "none",
               "--bench", str(time)]
    with (run_dir / "bench.log").open("w") as log:
        subprocess.run(command, cwd=run_dir, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=600)
    ram = (run_dir / "ram.bin").read_bytes()
    state = ram[symbols["view"]:symbols["view"]+40]
    counters = struct.unpack_from("<10I", ram, symbols["board_stats"])
    stack = struct.unpack_from("<I", ram, symbols["compas_supervisor_watermark"])[0]
    return state, counters, stack


checkpoints = [4500, 18000, 28000, 38500, 49000, 59000, 69000, 80500, 94500, 116000,
               6600, 89700, 100000, 102100, 122000, 127000]
with ThreadPoolExecutor(max_workers=4) as pool:
    strict_run = pool.submit(run, duration, True)
    cases = dict(zip(checkpoints, pool.map(run, checkpoints)))
    final, stats, stack = strict_run.result()
order = [cases[time][0][28] for time in checkpoints[:10]]
assert order == list(range(10)), order
assert cases[6600][0][4] in (1, 2)
assert cases[89700][0][34] and cases[94500][0][34], "Score hold dropped"
assert not cases[100000][0][34] and cases[100000][0][6] > 88, "Hold release or tempo failed"
assert cases[102100][1][6] >= 8, "Live 727 overlay did not reach eight rack voices"
assert cases[116000][0][36] and cases[116000][0][6] > 84, "Guide or tempo failed"
assert cases[122000][0][35] or cases[122000][0][32] == 4, "SEQ did not queue the coda from GLO"
assert cases[127000][0][32] == 4
assert 47400 < stats[0] < 47600 and not any(stats[2:5]) and not final[0] and final[28] == 9, (stats, list(final))
assert 0 < stats[1] < stats[9] and stats[9]+240 < 128*544
strict = out / "strict"
raw = (strict / "audio.raw").read_bytes()
pcm = array.array("h", raw)
left = pcm[::2]
assert left == pcm[1::2]
bench = json.loads((strict / "bench.json").read_text())
offset = bench["emulatedMs"] * 44100 // 1000 - len(left)
assert 0 <= offset < 44100


def peak(start, end):
    lo, hi = (time * 44100 // 1000 - offset for time in (start, end))
    assert 0 <= lo < hi <= len(left)
    return max(map(abs, left[lo:hi]))


windows = [(4500, 8500), (18000, 19500), (28000, 30000), (38500, 41000), (49000, 52000),
           (59000, 61000), (69000, 72000), (80500, 82500), (94500, 96500), (116000, 120000)]
peaks = [peak(start, end) for start, end in windows]
assert min(peaks) > 100 and peak(500, 1250) == 0 and peak(135500, 137500) == 0
with wave.open(str(out / "rehearsal.wav"), "wb") as wav:
    wav.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
    wav.writeframes(bytes(offset*4)+raw)
result = {
    "durationMs": duration, "strictCallbacks": stats[0], "maxCallbackUs": stats[1]/24, "peakIrqWindowUs": stats[9]/24,
    "irqUnmeasuredAllowanceUs": 10,
    "deadlineUs": 128*544/24, "deadlines": stats[2], "lateHalves": stats[3], "rails": stats[4],
    "rackPeak": stats[6], "totalPcmPeak": stats[8], "supervisorStackBytes": stack,
    "bootAndCodaTailSilent": True, "slotsAtNativeCheckpoints": [slot+1 for slot in order],
    "stereoPeakBySlotWindowInStrictRun": peaks, "holdReleaseTempoGuideAndCodaVerified": True,
    "imageSHA256": hashlib.sha256(image.read_bytes()).hexdigest(), "keyScript": keys,
}
(out / "rehearsal.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
