"""Rehearse the mixed set or Johnny Canales tribute on the offline FM-1."""
import argparse
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

root = Path(__file__).resolve().parents[1]
board = root / 'build/board'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('emulator', type=Path)
parser.add_argument('source', type=Path)
profile = parser.add_mutually_exclusive_group()
profile.add_argument('--tribute', action='store_true', help='check the eleven-slot Johnny Canales running order')
profile.add_argument('--adelita', action='store_true', help='check the three-slot La Adelita rehearsal')
profile.add_argument('--mora', action='store_true', help='check the five-slot La Mora piano rehearsal')
profile.add_argument('--merengue', action='store_true', help='check the seven-slot merengue rehearsal')
parser.add_argument('--output-parent', type=Path, default=board)
profile.add_argument('--siboney', action='store_true', help='check the eight Siboney rehearsals and the printed D.S. route')
profile.add_argument('--golondrina', action='store_true', help='check the Galveston piano, bolero and singer’s key')
profile.add_argument('--teodora', action='store_true', help='check compound meter and live meter changes')
profile.add_argument('--olas', action='store_true', help='check the Trifet waltz, polka and lower key')
profile.add_argument('--mora-finale', action='store_true', help='check the continuous sung passage and closing refrain of La Mora')
profile.add_argument('--jurame-song', action='store_true', help='check both stanzas and the Schirmer D.C. return')
profile.add_argument('--jurame', action='store_true', help='check the Schirmer refrain, guide and lower singing key')
profile.add_argument('--bamba', action='store_true', help='check the Veracruz strum, bass and footwork cues')
profile.add_argument('--merenhouse', action='store_true', help='check maco, kick and bass live cues')
profile.add_argument('--bolero-trova', action='store_true', help='check the Cuban bolero meter and guitar/bass dropouts')
args = parser.parse_args()
emulator = args.emulator.resolve()
source = args.source.resolve()
image = board / 'emu-store.xip'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
source_hash = digest(source)
assert digest(emulator) == '8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
songs = json.loads(source.read_text())['songs']
if args.tribute:
    studies = ['tejano', 'la-cucaracha-piano', 'coast', 'la-paloma-piano', 'bolero',
               'cielito-lindo-piano', 'son', 'el-manisero-kempton-piano', 'salsa',
               'merengue', 'la-golondrina']
    scores = {1: (8, 3), 3: (4, 2), 5: (7, 3), 7: (6, 2), 10: (2, 4)}
    interval, coda_press, coda_check, duration = 12000, 136000, 140000, 156000
    queue_slots = (0, 1)
    wav_name = 'Johnny-Canales-Rehearsal.wav'
    output_prefix = 'tribute-set-'
elif args.adelita:
    studies = ['la-adelita-kemp', 'conjunto', 'la-adelita-kemp']
    scores = {0: (9, 2), 2: (9, 2)}
    interval, coda_press, coda_check, duration = 12500, 104000, 106500, 138000
    queue_slots = (0, 1)
    wav_name = 'La-Adelita-Rehearsal.wav'
    output_prefix = 'adelita-set-'
elif args.mora:
    studies = ['la-mora-opening', 'la-mora-return', 'la-mora-interlude', 'la-mora-sung-bass', 'la-mora-refrain']
    scores = {0: (10, 2), 1: (15, 2), 2: (12, 2), 3: (13, 2), 4: (11, 2)}
    interval, coda_press, coda_check, duration = 9000, 43500, 44700, 53000
    queue_slots = (0, 1, 2, 3)
    wav_name = 'La-Mora-Rehearsal.wav'
    output_prefix = 'mora-set-'
elif args.merengue:
    studies = ['merengue-congas', 'merengue-congas', 'pambiche', 'derecho-primera',
               'derecho-segunda', 'merengue-maco', 'juana-quilina']
    scores = {6: (14, 2)}
    interval, coda_press, coda_check, duration = 10150, 66900, 68500, 73500
    queue_slots = (5,)
    wav_name = 'Merengue-Rehearsal.wav'
    output_prefix = 'merengue-set-'
elif args.siboney:
    studies = ['siboney-feist-chorus', 'siboney-feist-last',
               'siboney-feist-verse-last', 'siboney-feist-song']
    studies = [study for study in studies for _ in range(2)]
    scores = {slot: (score, 2) for slot, score in enumerate((17, 17, 18, 18, 26, 26, 27, 27))}
    interval, coda_check, duration = 9250, 277000, 286000
    queue_slots = (0, 1, 2, 3, 4, 5, 6)
    siboney_route = (
        (152600, 'First ending', 74, 76),
        (175000, 'Chorus repeat', 77, 107),
        (193700, 'Second ending', 108, 110),
        (196600, 'D.S. return', 111, 112),
        (208000, 'Second verse', 113, 140),
        (239000, 'Second chorus', 141, 172),
        (271000, 'Last ending', 173, 177),
    )
    wav_name = 'Siboney-Rehearsal.wav'
    output_prefix = 'siboney-set-'
elif args.golondrina:
    studies = ['la-golondrina-piano-bolero', 'la-golondrina-piano',
               'la-golondrina', 'la-golondrina-piano-bolero']
    scores = {slot: (score_id, 4) for slot, score_id in enumerate((19, 5, 2, 19))}
    interval, coda_press, coda_check, duration = 23000, 91000, 94500, 107000
    queue_slots = (0, 1, 2)
    wav_name = 'Golondrina-Rehearsal.wav'
    output_prefix = 'golondrina-set-'
elif args.teodora:
    studies = ['ma-teodora-1929', 'cielito-lindo-piano',
               'el-manisero-kempton-piano', 'ma-teodora-1929']
    scores = {0: (20, 2), 1: (7, 3), 2: (6, 2), 3: (20, 2)}
    interval, coda_press, coda_check, duration = 10500, 41000, 42650, 46000
    queue_slots = (0,)
    wav_name = 'Ma-Teodora-Rehearsal.wav'
    output_prefix = 'teodora-set-'
elif args.olas:
    studies = ['sobre-las-olas-trifet-strain', 'conjunto', 'sobre-las-olas-trifet-strain']
    scores = {0: (22, 3), 2: (22, 3)}
    interval, coda_press, coda_check, duration = 9500, 27800, 29300, 35000
    queue_slots = (0,)
    wav_name = 'Sobre-las-Olas-Rehearsal.wav'
    output_prefix = 'olas-set-'
elif args.mora_finale:
    studies = ['la-mora-singer-finale'] * 3
    scores = {slot: (25, 2) for slot in range(3)}
    interval, coda_check, duration = 5100, 64400, 72000
    queue_slots = (0, 1)
    wav_name = 'La-Mora-Singer-to-Finale.wav'
    output_prefix = 'mora-finale-set-'
elif args.jurame_song:
    studies = ['jurame-schirmer-song'] * 3
    scores = {slot: (24, 2) for slot in range(3)}
    interval, coda_check, duration = 5100, 160000, 166000
    queue_slots = (0, 1)
    wav_name = 'Jurame-Complete-Rehearsal.wav'
    output_prefix = 'jurame-song-set-'
elif args.jurame:
    studies = ['jurame-schirmer-refrain'] * 3
    scores = {slot: (23, 2) for slot in range(3)}
    interval, coda_check, duration = 5100, 48000, 56000
    queue_slots = (0, 1)
    wav_name = 'Jurame-Rehearsal.wav'
    output_prefix = 'jurame-set-'
elif args.merenhouse or args.bamba:
    study_id, mutes = (28, (33, 1, 0)) if args.bamba else (27, (34, 32, 0))
    studies = ['jarocho-bamba' if args.bamba else 'merenhouse'] * 3
    scores = {}
    interval, coda_press, coda_check, duration = 9000, 29000, 27500, 33000
    queue_slots = (0, 1)
    wav_name = 'Bamba-Rehearsal.wav' if args.bamba else 'Merenhouse-Rehearsal.wav'
    output_prefix = 'bamba-set-' if args.bamba else 'merenhouse-set-'
elif args.bolero_trova:
    studies = ['bolero-trova', 'bolero-trova', 'bolero', 'bolero-trova']
    scores = {}
    interval, coda_press, coda_check, duration = 9000, 35000, 37150, 44000
    queue_slots = (0, 1, 2)
    wav_name = 'Cuban-Bolero-Rehearsal.wav'
    output_prefix = 'bolero-trova-set-'
else:
    studies = ['coast', 'bolero', 'son', 'cielito-lindo-piano', 'salsa', 'merengue',
               'tejano', 'el-manisero-kempton-piano']
    scores = {3: (7, 3), 7: (6, 2)}
    interval, coda_press, coda_check, duration = 10000, 83000, 87000, 111000
    queue_slots = (2, 3)
    wav_name = 'Mixed-Rehearsal.wav'
    output_prefix = 'mixed-set-'
assert [song['study'] for song in songs] == studies
out = Path(tempfile.mkdtemp(prefix=output_prefix, dir=args.output_parent.resolve()))
print(out, flush=True)
seed = Path(subprocess.check_output(
    [sys.executable, str(root / 'tools/play-prepared-set.py'), str(source),
     '--emulator', str(emulator), '--image', str(image), '--image-sha', digest(image),
     '--state-dir', str(out / 'prepared'), '--prepare-only'], text=True).strip())

symbols = {}
for line in (board / 'compas-store.sections').read_text().splitlines():
    fields = line.split()
    if fields and fields[-1] in {'view', 'board_stats', 'compas_supervisor_watermark', 'compas_display_stats'}:
        symbols[fields[-1]] = int(fields[0], 16) - 0x01c00000
cues = [interval * slot for slot in range(len(studies))]
ending_key = 12 if args.merenhouse or args.bamba else 11
keys = ','.join(['950:pot:760', '1200:12', '1290:12^'] +
                [f'{time}:e6:+1' for time in cues[1:]] +
                ([] if args.jurame or args.jurame_song or args.mora_finale or args.siboney else [f'{coda_press}:{ending_key}', f'{coda_press + 90}:{ending_key}^']))

def run(time, strict=False):
    folder = out / ('strict' if strict else str(time))
    folder.mkdir()
    flash = folder / 'virtual.bin'
    flash.write_bytes(seed.read_bytes())
    env = {key: value for key, value in os.environ.items() if not key.startswith('FM1_')}
    env.update(FM1_BENCH_KEYS=keys, FM1_DUMPRAM=str(folder / 'ram.bin'),
               FM1_BENCH_PANEL=str(folder / 'panel.ppm'))
    if strict:
        env.update(FM1_STRICT='1', FM1_BENCH_WAV=str(folder / 'audio.raw'),
                   FM1_BENCH_JSON=str(folder / 'bench.json'))
    command = [str(emulator), '--bin', str(image), '--entry', '0x02000120',
               '--midi', 'none', '--flash', str(flash), '--bench', str(time)]
    with (folder / 'bench.log').open('w') as log:
        subprocess.run(command, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=600)
    return (folder / 'ram.bin').read_bytes()

def view(ram):
    return ram[symbols['view']:symbols['view']+76]

soundcheck_delay = 18000 if args.golondrina else 4000 if args.merengue else 4500 if args.jurame or args.jurame_song or args.mora_finale else 7400
times = [time + soundcheck_delay for time in cues]
queue_checks = {cues[slot + 1] + 150: slot for slot in queue_slots}
checkpoints = times + list(queue_checks) + [coda_check]
if args.jurame_song:
    checkpoints.append(118000)
if args.mora_finale:
    checkpoints.append(42500)
if args.siboney:
    checkpoints.extend(time for time, _, _, _ in siboney_route)
with ThreadPoolExecutor(max_workers=4) as pool:
    full = pool.submit(run, duration, True)
    cases = dict(zip(checkpoints, pool.map(run, checkpoints)))
    ram = full.result()
rows = []
for slot, time in enumerate(times):
    state = view(cases[time])
    assert state[0] and state[28] == slot, (time, list(state))
    assert state[6] == songs[slot]['bpm']
    score_id, beats = scores.get(slot, (0, None))
    assert state[31] == score_id
    if score_id:
        assert state[37] == beats, list(state)
        assert state[36] == int(songs[slot].get('guide', False))
    if args.golondrina:
        assert bool(state[73]) == (slot != 1)
        assert state[75] & 15 == (songs[slot].get('transpose', 0) % 12)
        assert state[32] in (2, 3), (time, 'Still in count-in or intro')
    if (args.teodora or args.olas or args.jurame or args.jurame_song or args.mora_finale) and score_id:
        assert not state[73]
        assert state[75] & 15 == songs[slot].get('transpose', 0) % 12
    if args.bolero_trova:
        assert state[1] == (5 if slot == 2 else 26)
        assert state[8] == (0, 8, 0, 32)[slot]
        assert state[11] == (slot == 2) and not state[49]
    rows.append(dict(slot=slot+1, study=studies[slot], bpm=state[6],
                     score=state[31], scoreBeats=state[37] if state[31] else None,
                     guide=bool(state[36]) if state[31] else None,
                     scorePercussion=bool(state[73]) if state[31] else None))
    if args.merenhouse or args.bamba:
        assert state[1] == study_id and state[8] == mutes[slot] and not (state[11] or state[49])
    if args.bolero_trova or args.merenhouse or args.bamba:
        rows[-1].update(muteMask=state[8], hybridKit=bool(state[11]), overlays=state[49])
for time, slot in queue_checks.items():
    state = view(cases[time])
    assert state[0] and state[28] == slot and state[29] == slot+1, (time, list(state))
ending = view(cases[coda_check])
if args.merenhouse or args.bamba:
    assert ending[0] and ending[28] == 2 and not ending[8]
elif args.bolero_trova:
    assert ending[0] and ending[44] == 5, (coda_check, 'Ending did not land', list(ending))
elif args.mora_finale:
    continuation = view(cases[42500])
    assert continuation[0] and continuation[28] == 2 and continuation[31] == 25, list(continuation)
    assert 23 <= continuation[33] < 35, list(continuation)
    assert ending[0] and ending[32] == 4 and ending[33] >= 41, (coda_check, list(ending))
elif args.siboney:
    route_checks = []
    for time, label, first, last in siboney_route + ((coda_check, 'Final cadence', 178, 181),):
        state = view(cases[time])
        bar = state[33] + 1
        assert state[0] and state[28] == 7 and state[31] == 27 and not state[36], (label, list(state))
        assert first <= bar <= last, (label, bar)
        route_checks.append(dict(timeMs=time, section=label, performanceBar=bar))
    assert ending[32] == 4, list(ending)
elif args.jurame_song:
    second_stanza = view(cases[118000])
    assert second_stanza[0] and second_stanza[28] == 2 and second_stanza[32] == 2, list(second_stanza)
    assert 63 <= second_stanza[33] < 67, list(second_stanza)
    assert ending[0] and ending[32] == 4 and ending[33] >= 88, (coda_check, list(ending))
elif args.jurame:
    assert ending[0] and ending[32] == 4 and ending[33] >= 21, (coda_check, list(ending))
else:
    assert ending[32] == 4, (coda_check, 'Coda did not land', list(ending))
final = view(ram)
assert not final[0] and final[28] == len(studies) - 1
stats = struct.unpack_from('<10I', ram, symbols['board_stats'])
assert not any(stats[2:5]) and 0 < stats[1] < stats[9] and stats[9]+240 < 128*544
display = struct.unpack_from('<12I', ram, symbols['compas_display_stats'])
assert display[0] <= 96000 and display[2] <= 288000 and display[3] <= 6000
assert display[6] <= 960000 and display[8] <= 960000 and display[10] <= 2880000
gates = {f'R{i+1}': dict(zip(('computeMs','transferMs','completeMs','pixels'),
         [n/24000 for n in display[i*4:i*4+3]] + [display[i*4+3]])) for i in range(3)}

strict = out / 'strict'
raw = (strict / 'audio.raw').read_bytes()
pcm = array.array('h', raw)
left = pcm[::2]
assert left == pcm[1::2]
bench = json.loads((strict / 'bench.json').read_text())
offset = bench['emulatedMs'] * 44100 // 1000 - len(left)
assert 0 <= offset < 44100

def peak(start, end):
    lo = start * 44100 // 1000 - offset
    hi = end * 44100 // 1000 - offset
    assert 0 <= lo < hi <= len(left)
    return max(map(abs, left[lo:hi]))

peaks = [peak(time, time+1400) for time in times]
assert min(peaks) > 100 and peak(duration - 2000, duration - 500) == 0
with wave.open(str(out / wav_name), 'wb') as wav:
    wav.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
    wav.writeframes(bytes(offset*4) + raw)

assert digest(source) == source_hash
report = dict(sourceSHA256=source_hash, seedSHA256=digest(seed), imageSHA256=digest(image),
              durationMs=duration, slots=rows, liveQueueVerified=True, liveCrossMeterQueueVerified=not (args.mora or args.siboney or args.golondrina or args.jurame or args.jurame_song or args.mora_finale),
              codaStoppedAndTailSilent=True, slotPeaks=peaks, callbacks=stats[0],
              peakCallbackUs=stats[1]/24, peakIrqUs=stats[9]/24, irqAllowanceUs=10,
              deadlineUs=128*544/24, deadlineMisses=stats[2], lateHalves=stats[3],
              rails=stats[4], gates=gates, R2ClockStartsAt='debounced foreground acceptance',
              supervisorStackBytes=struct.unpack_from('<I', ram, symbols['compas_supervisor_watermark'])[0],
              keyScript=keys)
if args.merenhouse or args.bamba:
    report['liveCrossMeterQueueVerified'] = False
    report['stopAndTailSilent'] = report.pop('codaStoppedAndTailSilent')
if args.siboney:
    report['routeCheckpoints'] = route_checks
(out / 'mixed-rehearsal.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
