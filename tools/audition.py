"""Render the five clean studio studies and make a compact no-voiceover reel."""
from pathlib import Path
import array
import json
import subprocess
import sys
import wave

root = Path(__file__).resolve().parents[1]
dest = Path(sys.argv[1]).resolve()
dest.mkdir(parents=True, exist_ok=True)
rate, excerpt = 44100, 12
measurements = []
names = ["01-cumbia-coast", "02-cumbia-mx", "03-salsa-son", "04-conjunto-polka", "05-tejano-cumbia"]
with wave.open(str(dest / "Compas-audition.wav"), "wb") as reel:
    reel.setparams((1, 2, rate, 0, "NONE", "not compressed"))
    for i, name in enumerate(names):
        file = dest / (name + ".wav")
        result = subprocess.check_output([str(root / "build/compas"), str(i), str(file)])
        measurements.append(json.loads(result))
        with wave.open(str(file), "rb") as sample:
            clip = array.array("h", sample.readframes(excerpt * rate))
        if sys.byteorder == "big":
            clip.byteswap()
        fade = rate // 20
        for f in range(fade):
            clip[-1 - f] = int(clip[-1 - f] * f / fade)
        if sys.byteorder == "big":
            clip.byteswap()
        if i:
            reel.writeframes(b"\0" * (2 * rate))
        reel.writeframes(clip.tobytes())
(dest / "host-timing.json").write_text(json.dumps(measurements, indent=2) + "\n")
