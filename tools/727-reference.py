"""Audit Kent Williams's direct recordings against Studio custody hashes."""
from pathlib import Path
import hashlib
import io
import json
import sys
import wave
import zipfile

studio, archive = map(Path,sys.argv[1:])
root = Path(__file__).resolve().parents[1]
rows = json.loads((root/"board/727-sources.json").read_text())["recordings"]
custody = json.loads((studio/"public/bigbeat/audio/collection/provenance.json").read_text())["assets"]
out = []
with zipfile.ZipFile(archive) as z:
    for entry in rows:
        original=custody[entry["sampleId"]]
        filename=Path(original["source"]).name
        wav=z.read("727 Samples/Orig/"+filename)
        digest=hashlib.sha256(wav).hexdigest()
        if digest!=original["sha256"]:
            raise ValueError("Reference differs from the retained original: "+filename)
        with wave.open(io.BytesIO(wav),"rb") as source:
            assert (source.getnchannels(),source.getsampwidth(),source.getframerate())==(1,3,44100)
            raw=source.readframes(source.getnframes())
        data=[int.from_bytes(raw[i:i+3],"little",signed=True) for i in range(0,len(raw),3)]
        peak=max(map(abs,data))
        out.append({"sound":entry["sound"],"sampleId":entry["sampleId"],"referenceFile":filename,
            "sha256":digest,"frames":len(data),"peak24":peak,
            "railSamples":sum(x in (-8388608,8388607) for x in data),
            "gainQ15":round(peak*32768/8388608)})
print(json.dumps({
    "source":"Kent Williams, TR-727 direct to RME Hammerfall DSP/Multiface, 2008",
    "url":"https://www.cornwarning.com/xfer/727Samples.zip",
    "archiveSha256":hashlib.sha256(archive.read_bytes()).hexdigest(),
    "baseline":"Common -12 dB playback scale. Relative digital peaks from the direct archive.",
    "qualification":"Hash-verified recordings. Capture gain, panel settings and unclipped hardware level are unverified.",
    "voices":out},indent=2))
