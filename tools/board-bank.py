"""Verify the included WAV inputs and generate the lossless release bank."""
import hashlib
import io
import json
import sys
import struct
import wave
from pathlib import Path

root = Path(__file__).resolve().parents[1]
studio = Path(sys.argv[1])
ledger = json.loads((root / "board/727-sources.json").read_text())
recordings = ledger["recordings"]
reference = json.loads((root / "board/727-levels.json").read_text())
levels = reference["voices"]
reference_maximum = reference["referenceMaxPeak24"]
assert reference_maximum == max(level["peak24"] for level in levels)
assert len(recordings) == len(levels) == 15
# Remove the common lift so the panel still controls the minimum accent lift.
accent_floor = min(level["accented"]["peak24"] / level["peak24"] for level in levels)
accent_gains = [
    round(level["accented"]["peak24"] / level["peak24"] / accent_floor * 4096)
    for level in levels
]
assert min(accent_gains) == 4096 and max(accent_gains) * 127 + 2048 < 256 * 4096
folder = root / "build/board"
folder.mkdir(parents=True, exist_ok=True)
samples = bytearray()
packed = bytearray()
entries = []
for entry, level, accent_gain in zip(recordings, levels, accent_gains):
    assert entry["sampleId"] == level["sampleId"]
    raw = (studio / "public/bigbeat/audio/collection" / (entry["sampleId"] + ".wav")).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == entry["processedSha256"], entry["sound"]
    with wave.open(io.BytesIO(raw), "rb") as src:
        assert (src.getnchannels(), src.getsampwidth(), src.getframerate()) == (1, 2, 44100)
        n = src.getnframes()
        pcm = src.readframes(n)
        assert len(pcm) == n * 2
    # Apply common-gain ratios to the retained Studio timbres.
    ratio = level["peak24"] / reference_maximum
    gain = level["gainQ15"]
    assert gain == round(ratio * 32768)
    values=[v[0] for v in struct.iter_unpack("<h",pcm)]
    pcm=struct.pack("<%dh" % n, *(round(value*gain/32768) for value in values))
    peak=max(abs(value[0]) for value in struct.iter_unpack("<h",pcm))
    assert abs(peak - ratio * 8191.75) <= 1.5
    packed_offset = len(packed)
    previous = 0
    repeated = 0
    for value, in struct.iter_unpack("<h", pcm):
        delta = value - previous
        if not -16384 <= value <= 16383:
            raise ValueError(f'{entry["sound"]}: sample outside packed bank range')
        if not delta:
            repeated += 1
            if repeated == 255:
                packed.extend((0, repeated))
                repeated = 0
            continue
        if repeated:
            packed.extend((0, repeated) if repeated >= 3 else (64,) * repeated)
            repeated = 0
        if -63 <= delta <= 63:
            packed.append(delta + 64)
        else:
            packed.extend((0x8000 | (value + 16384)).to_bytes(2, "big"))
        previous = value
    if repeated:
        packed.extend((0, repeated) if repeated >= 3 else (64,) * repeated)
    entries.append({"sound": entry["sound"], "offset": len(samples) // 2, "frames": n,
                    "packedOffset": packed_offset, "packedBytes": len(packed) - packed_offset,
                    "peak16": peak, "referencePeak24": level["peak24"],
                    "accentReferencePeak24": level["accented"]["peak24"], "accentGainQ12": accent_gain,
                    "pcmSha256": hashlib.sha256(pcm).hexdigest()})
    samples.extend(pcm)
(folder / "727.pcm").write_bytes(samples)
(folder / "727.pack").write_bytes(packed)
for filename, offset in (("727.inc", "packedOffset"), ("727-reference.inc", "offset")):
    (folder / filename).write_text(
        "static const Clip clips[] = {\n" +
        "".join("    {%d, %d},\n" % (e[offset], e["frames"]) for e in entries) + "};\n" +
        "static const uint16_t accent_gain_q12[] = {\n    " +
        ", ".join(map(str, accent_gains)) + "\n};\n")
(folder / "727.json").write_text(json.dumps({"bytes":len(samples), "packedBytes":len(packed), "clips":entries,
    "referenceArchiveSha256":reference["archiveSha256"], "pcmSha256":hashlib.sha256(samples).hexdigest()}, indent=2) + "\n")
