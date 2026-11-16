"""Check Compás startup ROM requests in the pinned strict emulator."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

BOARD = Path(__file__).resolve().parents[1] / "build/board"
EMU_SHA256 = "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
ROM_MAILBOX = 0x01C7FD80 - 0x01C00000
ROM_REQUEST = b"usb_update_mode\0"
# The two OCT buttons are 0 and 1. Button 2 and note 14 exercise exclusions.
CASES = (
    ("normal-boot", "", 1600, False, True),
    ("held-at-power-on", "0:0,0:1", 350, True, False),
    ("before-hold-threshold", "0:0,0:1", 180, False, False),
    ("one-button", "0:0", 1600, False, True),
    ("released-early", "0:0,0:1,100:1^", 1600, False, True),
    ("extra-button", "0:0,0:1,0:2", 1600, False, True),
    ("held-note", "0:0,0:1,0:14", 1600, False, True),
    ("pressed-after-startup", "800:0,800:1", 1600, False, True),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def probe(emulator, image, case, folder):
    name, keys, duration, expected_marker, expected_audio = case
    ram = folder / "ram.bin"
    bench = folder / "bench.json"
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_STRICT="1", FM1_BENCH_KEYS=keys, FM1_DUMPRAM=str(ram),
               FM1_BENCH_JSON=str(bench))
    result = subprocess.run(
        [str(emulator), "--bin", str(image), "--entry", "0x02000120",
         "--midi", "none", "--bench", str(duration)],
        cwd=folder, env=env, capture_output=True, text=True, timeout=120,
    )
    if result.returncode:
        raise RuntimeError(f"{image.name} {name}: {result.stderr or result.stdout}")
    data = ram.read_bytes()
    if len(data) != 0x100000:
        raise RuntimeError(f"{image.name} {name}: incomplete emulator RAM snapshot")
    marker = data[ROM_MAILBOX:ROM_MAILBOX + len(ROM_REQUEST)] == ROM_REQUEST
    clock = json.loads(bench.read_text())
    audio_frames = clock["audioFrames"]
    if (marker, audio_frames > 0) != (expected_marker, expected_audio):
        raise RuntimeError(
            f"{image.name} {name}: ROM marker {marker}, audio frames {audio_frames}"
        )
    return {"case": name, "input": keys, "benchMs": duration,
            "observedMs": clock["emulatedMs"], "romRequestMarker": marker,
            "audioFrames": audio_frames}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emulator", type=Path)
    args = parser.parse_args()
    emulator = args.emulator.resolve()
    if not emulator.is_file():
        parser.error("Pass the pinned fm1-live executable.")
    if digest(emulator) != EMU_SHA256:
        parser.exit(1, "Emulator differs from the pinned fm1-live build.\n")
    variants = {}
    with tempfile.TemporaryDirectory(prefix="compas-startup-recovery-") as temp:
        for variant in ("offline", "store"):
            subprocess.run(
                [sys.executable, str(Path(__file__).with_name("board-emu.py")),
                 str(BOARD), variant, "prepare"], check=True, capture_output=True,
            )
            # All eight probes read the same private snapshot of this image.
            image = Path(temp) / f"emu-{variant}.xip"
            image.write_bytes((BOARD / image.name).read_bytes())
            probes = []
            for case in CASES:
                folder = Path(temp) / variant / case[0]
                folder.mkdir(parents=True)
                probes.append(probe(emulator, image, case, folder))
            variants[variant] = {"imageSHA256": digest(image), "cases": probes}
    report = {"scope": "emulated startup mailbox and audio activity",
              "emulatorSHA256": EMU_SHA256, "variants": variants,
              "physicalRomEnumerationVerified": False, "physicalUnitTouched": False}
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
