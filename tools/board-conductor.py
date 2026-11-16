"""Exercise the linked Compás Band and Songs workflow in the FM-1 emulator."""
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
subprocess.run([sys.executable, str(Path(__file__).with_name("board-emu.py")),
                str(board), "demo", "prepare"], capture_output=True, check=True)
image = board / "emu-demo.xip"
names = {"view", "board_stats", "compas_supervisor_watermark"}
symbols = {row.split()[-1]: int(row.split()[0], 16)-0x01C00000
           for row in (board / "compas.sections").read_text().splitlines()
           if row.split() and row.split()[-1] in names}
keys = ("1000:pot:760,1250:12,1340:12^,1900:e6:+1,2070:e6:+1,"
        "7500:25,7590:25^,14000:e6:+1,14180:e6:+1,14360:e6:+1,"
        "14540:e6:+1,14720:e6:+1,14900:e6:+1,20400:26,20490:26^")
# The 15 physical pads remain playable while a written section runs.
pad_keys = (*range(14,21), *range(32,40))
keys += "," + ",".join(f"22400:{key},22490:{key}^" for key in pad_keys)
out = Path(tempfile.mkdtemp(prefix="conductor-", dir=board))
print(out, flush=True)


def run(ms, strict=False, script=keys, name=None):
    folder = out / (name or ("strict" if strict else str(ms)))
    folder.mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith("FM1_")}
    env.update(FM1_BENCH_KEYS=script, FM1_DUMPRAM=str(folder / "ram.bin"),
               FM1_BENCH_PANEL=str(folder / "panel.ppm"))
    if strict:
        env.update(FM1_STRICT="1", FM1_BENCH_WAV=str(folder / "audio.raw"),
                   FM1_BENCH_JSON=str(folder / "bench.json"))
    cmd = [str(emulator), "--bin", str(image), "--entry", "0x02000120", "--midi", "none",
           "--bench", str(ms)]
    with (folder / "bench.log").open("w") as log:
        subprocess.run(cmd, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=240)
    ram = (folder / "ram.bin").read_bytes()
    view = ram[symbols["view"]:symbols["view"]+57]
    stats = struct.unpack_from("<10I", ram, symbols["board_stats"])
    stack, = struct.unpack_from("<I", ram, symbols["compas_supervisor_watermark"])
    return view, stats, stack


with ThreadPoolExecutor(max_workers=4) as pool:
    strict_run = pool.submit(run, 23500, True)
    boot, song_queue, verse_queue, verse, score, score_part = [
        state for state, _, _ in pool.map(run, (850, 2350, 7900, 13300, 19900, 22700))
    ]
    late_fill = ("1000:pot:760,1080:25,1170:25^,1250:12,1340:12^,"
                 "6100:10,6100:26,6190:10^,6190:26^")
    late_check = pool.submit(run, 7000, script=late_fill)
    after_check = pool.submit(run, 11800, script=late_fill)
    late, _, _ = late_check.result()
    after, _, _ = after_check.result()
    final, stats, stack = strict_run.result()
# Cumbia, two bass tones, all four black-key overlays, then the bass mute.
fx_keys = ("850:e6:+1,1000:pot:760,1250:12,1340:12^,2200:e0:+1,"
           "2450:e5:+1,2750:e2:+18,3000:e3:+18,3250:e0:+1,"
           "3500:22,3590:22^,3500:24,3590:24^,3500:27,3590:27^,3500:29,3590:29^,"
           "3800:e2:-10,3930:e3:-15,4060:e4:-20,4190:e5:-25,"
           "5400:31,5490:31^,6200:31,6290:31^,"
           "6800:22,6890:22^,6800:24,6890:24^,6800:27,6890:27^,6800:29,6890:29^")
with ThreadPoolExecutor(max_workers=3) as pool:
    fx = pool.submit(run, 9000, True, fx_keys, "strict-percussion")
    all_on = pool.submit(run, 4500, False, fx_keys, "all-overlays")
    muted = pool.submit(run, 5750, False, fx_keys, "bass-muted")
    on_state, _, _ = all_on.result()
    muted_state, _, _ = muted.result()
    fx_state, fx_stats, _ = fx.result()
assert on_state[0] and on_state[28]==1 and on_state[46]==1 and on_state[49]==15, list(on_state)
assert on_state[6]==101 and on_state[12]==74 and all(50<=level<100 for level in on_state[50:54]), list(on_state)
assert muted_state[0] and muted_state[8]&32 and muted_state[49]==15, list(muted_state)
assert fx_state[0] and fx_state[28]==1 and not fx_state[8]&32 and not fx_state[49], list(fx_state)
assert not any(fx_stats[2:5]) and fx_stats[6]>=2 and fx_stats[9]+240<128*544, fx_stats
fx_folder = out / "strict-percussion"
fx_audio = (fx_folder / "audio.raw").read_bytes()
fx_bench = json.loads((fx_folder / "bench.json").read_text())
fx_offset = fx_bench["emulatedMs"] * 44100 // 1000 - len(fx_audio) // 4
assert 0 <= fx_offset < 44100
with wave.open(str(out / "Compas-Bass-and-Percussion.wav"), "wb") as wav:
    wav.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
    wav.writeframes(bytes(fx_offset*4) + fx_audio)
assert late[0] and late[4] == 1 and late[44] == 2 and late[45] == 3, list(late)
assert after[0] and not after[4] and after[44] == 3 and after[45] == 6, list(after)
assert not boot[0] and boot[28] == 0
assert song_queue[0] and song_queue[28] == 0 and song_queue[29] == 2 and song_queue[13] == 0
assert verse_queue[0] and verse_queue[28] == 2 and verse_queue[45] == 2
assert verse[0] and verse[28] == 2 and verse[44] == 2 and verse[45] == 6
assert score[0] and score[28] == 8 and score[31]
assert score_part[0] and score_part[32] == 3 and final[0] and final[32] == 3
assert stats[6]==10 and stats[5]>=5, stats
assert 7800 < stats[0] < 8200 and 0 < stats[1] < stats[9] and stats[9]+240 < 128*544 and not any(stats[2:5]), stats
strict = out / "strict"
audio = (strict / "audio.raw").read_bytes()
bench = json.loads((strict / "bench.json").read_text())
offset = bench["emulatedMs"] * 44100 // 1000 - len(audio) // 4
assert 0 <= offset < 44100
peaks = []
for start, end in ((400, 1150), (2000, 4500), (7800, 10000), (13300, 13800), (21900, 22600)):
    lo = start * 44100 // 1000 - offset
    hi = end * 44100 // 1000 - offset
    assert 0 <= lo < hi <= len(audio)//4
    peaks.append(max(abs(l) for l, _ in struct.iter_unpack("<hh", audio[4*lo:4*hi])))
assert not peaks[0] and min(peaks[1:]) > 100, peaks
with wave.open(str(out / "Compas-Band-Demo.wav"), "wb") as wav:
    wav.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
    wav.writeframes(bytes(offset*4) + audio)
result = {
    "imageSHA256": hashlib.sha256(image.read_bytes()).hexdigest(),
    "callbackCount": stats[0], "peakCallbackUs": stats[1]/24, "peakIrqWindowUs": stats[9]/24,
    "irqUnmeasuredAllowanceUs": 10, "deadlineUs": 128*544/24,
    "deadlineMisses": stats[2], "lateHalves": stats[3], "clippedSamples": stats[4],
    "supervisorStackBytes": stack, "stereoAuditionPeaks": peaks,
    "bootLoadedFirstSlot": True, "latestPresetWon": True,
    "verseBCommittedAtPhrase": True, "writtenChorusCommittedAtBar": True,
    "bandRackPeak": stats[6], "chokeCuts": stats[5],
    "armedPartStartedWithPlay": True, "partCueWaitedForDeferredFill": True,
    "bassAndPercussionPeakIrqWindowUs": fx_stats[9]/24,
    "bassAndPercussionCallbacks": fx_stats[0],
    "allOverlaysToggledAndBassMutedDuringPlay": True,
    "selectSwitchedInstrumentDuringPlay": True, "overlayLevels": list(on_state[50:54]),
    "bassAndPercussionKeyScript": fx_keys,
    "keyScript": keys, "lateFillScript": late_fill,
}
(out / "conductor.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
