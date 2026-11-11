"""Build the optional Hanan bank from the independently verified local extraction."""
from hashlib import sha256
from math import tanh
from pathlib import Path
import sys
import wave

source = Path(sys.argv[1])


def pinned(file, expected):
    path = source / file
    data = path.read_bytes()
    if sha256(data).hexdigest() != expected:
        raise SystemExit(f"Hanan asset checksum mismatch: {path}")
    return data


samples = (
    ("bombo-kick", "f96f3e11263e4f564022088f579edb89b9f2db0599cde65a37d2aae9b890edab"),
    ("huiro-guiro", "8103e4172f68906b6fff17bfb85b179bb2098000a09124da59b37d730bd45399"),
    ("campana-cowbell", "3d2e149c8b38f495fec0d3223fabbd3a91f83b3e676fa76c21d481455a4b7eb9"),
    ("conga", "4612fa0afaf61ed1ec0bd4dc390e40b0d47322d01945eaec23cdeaede8e1dbc6"),
)
lines = []
for voice, (name, digest) in enumerate(samples):
    file = f"sounds/{name}.wav"
    pinned(file, digest)
    with wave.open(str(source / file), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 1, 9804)
        pcm = wav.readframes(wav.getnframes())
    lines.append(f"static const uint8_t hanan_pcm_{voice}[]={{")
    lines.extend(",".join(map(str, pcm[start:start + 32])) + "," for start in range(0, len(pcm), 32))
    lines.append("};")

packed = pinned("presets/factory-packed.bin", "198dc69a93ea4866c81d7c97821acd1911f80e60374ab8272af89beadbb278e0")
assert len(packed) == 906 and packed[10:] == packed[10:234] * 4
bank = int.from_bytes(packed[10:234], "little")
lines.append("static const uint16_t hanan_steps[4][32]={")
for slot in range(4):
    words = []
    for step in range(slot * 32, (slot + 1) * 32):
        # Firmware lane order is conga, bell, güiro, kick. Pad order is reversed.
        hits = sum(((bank >> (lane * 128 + step)) & 1) << (3 - lane) for lane in range(4))
        conga = (bank >> (512 + step * 5)) & 31
        bell = (bank >> (1152 + step * 5)) & 31
        words.append(hits | (conga << 4) | (bell << 9))
    lines.append("    {" + ",".join(map(str, words)) + "},")
lines.append("};")
# Q10 pitch keeps original increments exact at unity. The drive curve spans
# 30 dB, with an asymmetric, zero-centred transfer on the kit bus.
def table(name, c_type, values):
    lines.append(f"static const {c_type} {name}[]={{")
    lines.extend(",".join(map(str, values[i:i + 16])) + "," for i in range(0, len(values), 16))
    lines.append("};")


table("kumbia_pitch_q10", "uint16_t", [round(1024 * 2**(n/12)) for n in range(-12,13)])
table("kumbia_gain_q8", "uint16_t", [round(256 * 10**(0.015*d)) for d in range(101)])
table("kumbia_clip", "int16_t", [round(128*(tanh(n/128+0.15)-tanh(0.15))) for n in range(-256,257)])
Path("build/board/hanan-data.inc").write_text("\n".join(lines) + "\n")
