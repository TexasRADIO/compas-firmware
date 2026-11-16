"""Compare the strict FM-1 pad output to the retained digital 727 bank."""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'build/board'
PAIRS = ((0, 1), (2, 3), (7, 8), (9, 10), (11, 12))
RATE = 44100
STRENGTH = 92
ACCENT_STRENGTH = 124


def sha(data):
    return hashlib.sha256(data).hexdigest()


def signed_samples(data):
    samples = array.array('h', data)
    if sys.byteorder != 'little':
        samples.byteswap()
    return samples


def scaled_sample(sample, strength):
    # Use integer division toward zero, as in the target voice gain.
    magnitude = abs(sample) * strength // 512
    return -magnitude if sample < 0 else magnitude


def captured(samples):
    # The pinned emulator's WAV tap maps the codec words at half this
    # digital scale. Apply the shift after summing any active voices.
    return array.array('h', (sample >> 1 for sample in samples))


def locate(signal, clip, start, end, overlap=None):
    """Find a voiced prefix inside the capture's key-trigger window."""
    anchor = next(i for i, sample in enumerate(captured(clip)) if sample)
    if overlap is not None:
        first, first_clip = overlap
        for onset in range(start, end):
            index = onset + anchor
            overlap_start = index - first
            if not 0 <= overlap_start <= len(first_clip)-64:
                continue
            if all(signal[index+i] == ((sample + first_clip[overlap_start+i]) >> 1)
                   for i, sample in enumerate(clip[anchor:anchor+64])):
                return onset
        raise AssertionError('Independent second voice prefix missing')
    prefix = captured(clip[anchor:anchor+64]).tobytes()
    window = signal[start:end+anchor+64].tobytes()
    found = window.find(prefix)
    assert found >= 0 and found % 2 == 0, (start, end, anchor)
    onset = start + found // 2 - anchor
    assert start <= onset < end
    return onset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('emulator', type=Path)
    parser.add_argument('full_load_receipt', type=Path)
    args = parser.parse_args()
    emulator = args.emulator.resolve()
    assert sha(emulator.read_bytes()) == '8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
    image = BOARD / 'emu-store.xip'
    image_hash = sha(image.read_bytes())
    assert image_hash == json.loads(args.full_load_receipt.read_text())['imageSHA256']
    manifest = json.loads((BOARD / '727.json').read_text())
    source = (BOARD / '727.pcm').read_bytes()
    assert sha(source) == manifest['pcmSha256']
    clips, accented_clips, names = [], [], []
    for voice in manifest['clips']:
        offset, frames = voice['offset'], voice['frames']
        data = source[offset*2:(offset+frames)*2]
        assert sha(data) == voice['pcmSha256']
        samples = signed_samples(data)
        clips.append(array.array('h', (scaled_sample(sample, STRENGTH) for sample in samples)))
        gain = (ACCENT_STRENGTH * voice['accentGainQ12'] + 2048) >> 12
        accented_clips.append(array.array('h', (scaled_sample(sample, gain) for sample in samples)))
        names.append(voice['sound'])
    assert len(clips) == 15

    actions = [(900, 'pot:1023'), (1000, '13'), (1090, '13^')]
    events = []
    cursor = 1800

    def add(first, second=None, choke=False, accented=False):
        nonlocal cursor
        hits = [(cursor, first)]
        if second is not None:
            hits.append((cursor+45, second))
        for time, voice in hits:
            actions.extend([(time, str(14+voice)), (time+90, str(14+voice)+'^')])
        finish = max(time + (1000*len(clips[voice])+RATE-1)//RATE for time, voice in hits) + 400
        events.append((hits, finish, choke, accented))
        cursor = finish

    for sound in range(len(clips)):
        add(sound)
    for a, b in PAIRS:
        add(a, b, True)
        add(b, a, True)
    add(13, 14)  # Quijada and star chime may overlap.
    actions.extend(((cursor, '29'), (cursor+90, '29^')))  # ACC pad.
    cursor += 350
    for sound in range(len(clips)):
        add(sound, accented=True)
    duration = cursor+500
    keys = ','.join(f'{time}:{event}' for time, event in sorted(actions))

    out = Path(tempfile.mkdtemp(prefix='727-pad-rehearsal-', dir=BOARD))
    print(out, flush=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('FM1_')}
    env.update(FM1_STRICT='1', FM1_BENCH_KEYS=keys, FM1_DUMPRAM=str(out/'ram.bin'),
               FM1_BENCH_WAV=str(out/'audio.raw'), FM1_BENCH_JSON=str(out/'bench.json'))
    with (out/'bench.log').open('w') as log:
        subprocess.run([str(emulator), '--bin', str(image), '--entry', '0x02000120',
                        '--midi', 'none', '--bench', str(duration)], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)

    raw = (out/'audio.raw').read_bytes()
    pcm = signed_samples(raw)
    signal = pcm[::2]
    assert signal == pcm[1::2]
    offset = json.loads((out/'bench.json').read_text())['emulatedMs'] * RATE // 1000 - len(signal)
    assert 0 <= offset < RATE

    def frame(time):
        return time * RATE // 1000 - offset

    results = []
    previous_end = 0
    for hits, finish, choke, accented in events:
        time, voice = hits[0]
        first_clip = (accented_clips if accented else clips)[voice]
        first = locate(signal, first_clip, frame(time-60), frame(time+120))
        expected = first_clip
        row = dict(triggerMs=time, voice=names[voice], accented=accented, wavStartFrame=offset+first)
        if len(hits) == 2:
            second_time, second_voice = hits[1]
            overlap = None if choke else (first, first_clip)
            second = locate(signal, clips[second_voice], frame(second_time-35),
                            frame(second_time+120), overlap)
            gap = second - first
            assert 64 < gap < len(first_clip)
            assert any(first_clip[gap:gap+64]), 'First voice has no tail to test'
            if choke:
                expected = first_clip[:gap] + clips[second_voice]
            else:
                length = max(len(first_clip), gap+len(clips[second_voice]))
                expected = first_clip + array.array('h', [0])*(length-len(first_clip))
                for i, sample in enumerate(clips[second_voice], gap):
                    expected[i] += sample
            row.update(secondVoice=names[second_voice], secondTriggerMs=second_time,
                       onsetSpacingFrames=gap, choke=choke)
        end = first + len(expected)
        assert end < frame(finish-100), row
        assert signal[first:end] == captured(expected), row
        assert not any(signal[previous_end:first]), row
        previous_end = end
        row['capturePeak'] = max(map(abs, signal[first:end]))
        results.append(row)
    normal, accented = results[:15], results[-15:]
    for profile, reference, tolerance in ((normal, 'referencePeak24', 0.04),
                                          (accented, 'accentReferencePeak24', 0.06)):
        maximum = max(row['capturePeak'] for row in profile)
        reference_max = max(voice[reference] for voice in manifest['clips'])
        for row, voice in zip(profile, manifest['clips']):
            row['relativePeakDb'] = 20*math.log10(row['capturePeak']/maximum)
            row['referencePeakDb'] = 20*math.log10(voice[reference]/reference_max)
            assert abs(row['relativePeakDb']-row['referencePeakDb']) < tolerance, row
    for hit, accent in zip(normal, accented):
        accent['liftDb'] = 20*math.log10(accent['capturePeak']/hit['capturePeak'])
        assert accent['liftDb'] > 0, accent
    assert not any(signal[previous_end:])
    metrics = json.loads(subprocess.check_output(
        [sys.executable, str(ROOT/'tools/board-emu.py'), str(BOARD), 'store',
         '--native-ram', str(out/'ram.bin')], text=True))
    assert metrics['chokeCuts'] == 10
    assert metrics['rackPeak'] == 2 and metrics['sequencedPCMPeak'] == 0
    assert not any(metrics[key] for key in ('deadlines', 'lateHalves', 'railSamples'))
    assert metrics['maxIrqWindowUs'] + metrics['irqUnmeasuredAllowanceUs'] < metrics['deadlineUs']

    with wave.open(str(out/'727-Pad-Reference.wav'), 'wb') as wav:
        wav.setparams((2, 2, RATE, 0, 'NONE', 'not compressed'))
        wav.writeframes(bytes(offset*4)+raw)
    report = dict(scope='strict target pad output against the retained digital reference bank',
                  imageSHA256=image_hash, bankSHA256=sha(source), durationMs=duration,
                  referenceArchiveSHA256=manifest["referenceArchiveSha256"],
                  rackStrength=STRENGTH, accentStrength=ACCENT_STRENGTH, masterPercent=100, rackLevelPercent=100,
                  isolatedVoices=15, isolatedAccentVoices=15, chokeOrders=10, independentOverlap=True,
                  exactPCM=True, keyScript=keys, events=results, timing=metrics)
    (out/'727-pad-reference.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
