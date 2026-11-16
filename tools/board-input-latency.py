"""Measure scripted FM-1 input to the first changed pixel of the emulated LCD."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EMU_SHA256 = "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
# fm1-live 0.4.0 renders the 240-square LCD at 2× inside the 1280×820 panel.
PANEL_SIZE = (1280, 820)
LCD_ORIGIN = (370, 42)
BOOT_AND_PLAY = "900:pot:760,1050:e6:+1,1450:12,1540:12^"
STARTS = (2300, 2413, 2507)
CHECKPOINTS = (10, 20, 30, 36, 39, 45, 90)
TARGETS = {
    "cueText": ("cue", (4, 93, 137, 104)),
    "chorusKey": ("cue", (121, 158, 146, 174)),
    "congaKey": ("layer", (49, 143, 72, 157)),
    "tempoValue": ("tempo", (5, 215, 55, 237)),
}


def panel_regions(path):
    header, size, maximum, rgb = path.read_bytes().split(b"\n", 3)
    width, height = PANEL_SIZE
    if header != b"P6" or size != f"{width} {height}".encode() or maximum != b"255":
        raise ValueError("Unexpected fm1-live panel format")
    if len(rgb) != width * height * 3:
        raise ValueError("Incomplete fm1-live panel capture")
    origin_x, origin_y = LCD_ORIGIN
    return {
        name: b"".join(rgb[(y * width + x) * 3:(y * width + x) * 3 + 3]
                       for y in range(origin_y + 2*y0, origin_y + 2*y1, 2)
                       for x in range(origin_x + 2*x0, origin_x + 2*x1, 2))
        for name, (_, (x0, y0, x1, y1)) in TARGETS.items()
    }


def measure(args, folder, stats_offset, event, kind, delay):
    prefix = folder / f"{event}-{kind}-{delay}"
    frame, clock, ram = (prefix.with_suffix(ext) for ext in (".ppm", ".json", ".ram"))
    additions = {
        "control": "",
        "cue": f",{event}:26,{event+75}:26^",
        "layer": f",{event}:22,{event+75}:22^",
        "tempo": f",{event}:e2:+1",
    }
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_STRICT="1", FM1_BENCH_KEYS=BOOT_AND_PLAY + additions[kind],
               FM1_BENCH_PANEL=str(frame), FM1_BENCH_JSON=str(clock), FM1_DUMPRAM=str(ram))
    completed = subprocess.run(
        [str(args.emulator), "--bin", str(args.image), "--entry", "0x02000120",
         "--midi", "none", "--bench", str(event+delay)],
        env=env, cwd=folder, capture_output=True, timeout=120,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr.decode(errors="replace"))
    stats = struct.unpack_from("<10I", ram.read_bytes(), stats_offset)
    if not stats[0] or any(stats[2:5]) or stats[9]+240 >= 128*544:
        raise ValueError(f"Audio fault in {prefix.name}: {stats}")
    return json.loads(clock.read_text())["emulatedMs"]-event, panel_regions(frame)


def result_for(samples, event, name, action):
    reference = CHECKPOINTS[-1]
    _, still = samples[event, "control", reference]
    _, settled = samples[event, action, reference]
    target = settled[name]
    changed = [i for i in range(0, len(target), 3) if target[i:i+3] != still[name][i:i+3]]
    if not changed:
        raise ValueError(f"{name} did not settle to a changed state after {event} ms")
    observations = []
    for delay in CHECKPOINTS:
        _, control = samples[event, "control", delay]
        elapsed, sample = samples[event, action, delay]
        if control[name] != still[name]:
            raise ValueError(f"Unrelated pixel motion in {name} at {event}+{delay} ms")
        matches = sum(sample[name][i:i+3] == target[i:i+3] for i in changed)
        observations.append({"requestedMs": delay, "observedMs": elapsed, "changedPixels": matches})
    first = min(row["observedMs"] for row in observations if row["changedPixels"])
    complete = min(row["observedMs"] for row in observations if row["changedPixels"] == len(changed))
    return dict(firstVisibleByMs=first, changedRegionCompleteByMs=complete,
                changedPixels=len(changed), samples=observations)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emulator", type=lambda name: Path(name).resolve())
    parser.add_argument("--image", type=lambda name: Path(name).resolve(),
                        default=ROOT/"build/board/emu-store.xip")
    parser.add_argument("--sections", type=Path, default=ROOT/"build/board/compas-store.sections")
    parser.add_argument("--observe", action="store_true", help="report failures in an earlier image")
    args = parser.parse_args()
    if hashlib.sha256(args.emulator.read_bytes()).hexdigest() != EMU_SHA256:
        parser.error("emulator does not match the pinned native fm1-live 0.4.0")
    symbol = next(line.split()[0] for line in args.sections.read_text().splitlines()
                  if line.split()[-1:] == ["board_stats"])
    stats_offset = int(symbol, 16) - 0x01c00000
    plan = [(event, kind, delay) for event in STARTS for kind in ("control", "cue", "layer", "tempo")
            for delay in CHECKPOINTS]
    with tempfile.TemporaryDirectory(prefix="compas-input-latency-") as temp:
        with ThreadPoolExecutor(max_workers=6) as pool:
            samples = dict(zip(plan, pool.map(lambda job: measure(args, Path(temp), stats_offset, *job),
                                               plan)))
    actions = {str(event): {name: result_for(samples, event, name, action)
                            for name, (action, _) in TARGETS.items()} for event in STARTS}
    passes = all(result["firstVisibleByMs"] <= 40
                 for row in actions.values() for result in row.values())
    receipt = {
        "imageSHA256": hashlib.sha256(args.image.read_bytes()).hexdigest(),
        "emulatorSHA256": EMU_SHA256,
        "clock": "Scripted matrix/encoder input to the captured emulated LCD",
        "baseline": "No-input control at the same requested capture time",
        "mode": "Cumbia Eléctrica in playback, Band page, built-in set",
        "deadlineMs": 40,
        "firstVisiblePass": passes,
        "strictRuns": len(plan),
        "probes": actions,
    }
    print(json.dumps(receipt, indent=2))
    if not passes and not args.observe:
        raise SystemExit("An affected LCD region missed the 40 ms first-visible budget")


if __name__ == "__main__":
    main()
