"""Measure Alessandro Saccoia's common-gain TR-727 reference for Compás."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_SHA256 = "ccf3f2aa395c055635547792413052595c484c39097a4602abaa7c0c8e9c4b1d"
WAV_STEMS = (
    "Bongo Hi", "Bongo Lo", "Hi Conga Mute", "Hi Conga Open", "Low Conga",
    "Hi Timbale", "Low Timbale", "Agogo Hi", "Agogo Low", "Cabasa", "Maracas",
    "Whistle Short One", "Whistle Long", "Quijada", "Star Chime",
)


def measure(archive, stem):
    name = stem + ".wav"
    raw = archive.read("RolandTR727Samples/" + name)
    with wave.open(io.BytesIO(raw), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 3, 44100)
        frames = wav.getnframes()
        pcm = wav.readframes(frames)
    assert len(pcm) == frames * 3
    samples = [int.from_bytes(pcm[i:i + 3], "little", signed=True) for i in range(0, len(pcm), 3)]
    assert samples and not any(sample in (-8388608, 8388607) for sample in samples), name
    return dict(referenceFile=name, sha256=hashlib.sha256(raw).hexdigest(), frames=frames,
                peak24=max(map(abs, samples)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    raw_archive = args.archive.read_bytes()
    assert hashlib.sha256(raw_archive).hexdigest() == ARCHIVE_SHA256
    recordings = json.loads((ROOT / "board/727-sources.json").read_text())["recordings"]
    assert len(recordings) == len(WAV_STEMS) == 15
    with zipfile.ZipFile(io.BytesIO(raw_archive)) as archive:
        voices = [
            dict(sound=entry["sound"], sampleId=entry["sampleId"], **measure(archive, stem),
                 accented=measure(archive, stem + " Acc"))
            for entry, stem in zip(recordings, WAV_STEMS)
        ]
        short_repeat = [measure(archive, "Whistle Short Two" + suffix) for suffix in ("", " Acc")]

    maximum = max(voice["peak24"] for voice in voices)
    for voice in voices:
        voice["gainQ15"] = round(voice["peak24"] * 32768 / maximum)
    print(json.dumps(dict(
        source="Alessandro Saccoia, Roland TR-727, 2013",
        url="https://web.archive.org/web/20160404214027id_/http://www.keplero.com/material/tr727/RolandTR727Samples.zip",
        archiveSha256=ARCHIVE_SHA256,
        baseline="Normal hits relative to unaccented high timbale. Common -12 dB playback scale for the loudest hit.",
        qualification="Recorder reports common input gain and no normalization. The normal and accented files were measured without clipping. Hardware fader and accent settings are undocumented.",
        referenceMaxPeak24=maximum,
        alternateShortWhistle=short_repeat,
        voices=voices,
    ), indent=2))


if __name__ == "__main__":
    main()
