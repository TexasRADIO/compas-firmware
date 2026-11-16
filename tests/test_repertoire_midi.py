import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from repertoire_midi import midi, notes, rehearsal_midi, score_parts


def read_vlq(data, start):
    value = 0
    while True:
        byte = data[start]
        start += 1
        value = (value << 7) | (byte & 127)
        if byte < 128:
            return value, start


def tracks(smf):
    assert smf[:4] == b"MThd"
    size, kind, count, ppq = struct.unpack_from(">IHHH", smf, 4)
    offset, result = 8 + size, []
    for _ in range(count):
        assert smf[offset:offset+4] == b"MTrk"
        size = int.from_bytes(smf[offset+4:offset+8], "big")
        offset += 8
        data = smf[offset:offset+size]
        offset += size
        position, tick, events = 0, 0, []
        while position < len(data):
            delta, position = read_vlq(data, position)
            tick += delta
            status = data[position]
            position += 1
            if status == 255:
                tag = data[position]
                length, position = read_vlq(data, position+1)
                payload = data[position:position+length]
                position += length
                events.append((tick, status, tag, payload))
            else:
                length = 1 if status >> 4 == 12 else 2
                payload = tuple(data[position:position+length])
                position += length
                events.append((tick, status, *payload))
        result.append(events)
    assert offset == len(smf)
    return kind, ppq, result


def note_messages(events):
    return [(tick, status & 240, pitch, velocity) for tick, status, pitch, velocity in
            (event for event in events if event[1] >> 4 in (8, 9))]


class RehearsalMidi(unittest.TestCase):
    def test_valentina_preserves_the_complete_normalized_source_midi(self):
        score = json.loads((ROOT / "repertoire/la-valentina-apke-guitar.json").read_text())
        voices, spans = score_parts(score)
        self.assertEqual(spans, [72] * 48)
        events = sorted(event for voice in voices for event in voice)
        self.assertEqual(len(events), 587)
        # Calculated independently from the 480 PPQ PDF attachment after
        # dropping its one-tick release gaps and reducing it to 24 PPQ.
        self.assertEqual(hashlib.sha256(json.dumps(events, separators=(",", ":")).encode()).hexdigest(),
                         "88599243e46db14a73207339ed0ee895df8b24268f0a4bf2bf0b6311c38f5f00")
        self.assertEqual([e for e in events if e[0] < 1152],
                         [[s-1152, p, d] for s, p, d in events if 1152 <= s < 2304])
        self.assertEqual(events[-4:], [[3420, pitch, 36] for pitch in (40, 64, 68, 76)])
        kind, ppq, (conductor, *guitar) = tracks(midi(score))
        self.assertEqual((kind, ppq, len(guitar)), (1, 24, 4))
        self.assertIn((0, 255, 1, b"CC BY-SA 4.0"), conductor)
        self.assertIn((0, 255, 88, b"\x06\x03\x24\x08"), conductor)
        self.assertTrue(all(track[-1] == (3456, 255, 47, b"") for track in guitar))

    def test_jurame_keeps_the_refrain_tie_and_low_triplet(self):
        score = json.loads((ROOT / "repertoire/jurame-schirmer-refrain.json").read_text())
        (bass, canto), spans = score_parts(score)
        self.assertEqual(spans, [48] * 23)
        self.assertEqual(bass[2:5], [[42, 40, 2], [44, 41, 2], [46, 42, 2]])
        self.assertEqual(canto[:4], [[0, 72, 12], [12, 71, 12], [24, 74, 36], [60, 71, 6]])
        self.assertEqual([event for event in bass if 282 <= event[0] < 288],
                         [[282, 38, 2], [284, 40, 2], [286, 41, 2]])
        self.assertEqual([event for event in canto if event[0] in (558, 816, 840)],
                         [[558, 76, 6], [558, 79, 6],
                          [816, 76, 24], [816, 79, 24],
                          [840, 76, 36], [840, 79, 36]])
        self.assertEqual(canto[-1], [1008, 72, 60])
        kind, ppq, (conductor, low, guide) = tracks(midi(score))
        self.assertEqual((kind, ppq), (1, 24))
        self.assertIn((240, 255, 6, b"ANSWER"), conductor)
        self.assertIn((816, 255, 6, b"CADENCE"), conductor)
        self.assertIn((558, 144, 76, 82), note_messages(guide))
        self.assertIn((558, 144, 79, 82), note_messages(guide))
        self.assertTrue(all(track[-1] == (1104, 255, 47, b"") for track in (conductor, low, guide)))

    def test_jurame_dc_repeats_the_piano_and_omits_the_three_bracketed_notes(self):
        score = json.loads((ROOT / "repertoire/jurame-schirmer-song.json").read_text())
        (bass, intro, canto), spans = score_parts(score)
        self.assertEqual(spans, [48] * 90)
        for part in (bass, intro):
            self.assertEqual([event for event in part if event[0] < 2160],
                             [[t-2160, p, d] for t,p,d in part if t >= 2160])
        self.assertEqual(intro[0], [0, 84, 6])
        self.assertEqual(canto[0], [276, 67, 6])
        self.assertEqual([event for event in canto if 930 <= event[0] < 948],
                         [[930, 72, 6], [936, 72, 6], [942, 70, 6]])
        self.assertFalse(any(3090 <= t < 3108 for t, _, _ in canto))
        self.assertIn([3108, 70, 6], canto)
        self.assertEqual(canto[-1], [4224, 72, 60])
        kind, ppq, parts = tracks(midi(score))
        self.assertEqual((kind, ppq, len(parts)), (1, 24, 4))
        self.assertIn((2160, 255, 6, b"D.C. RETURN"), parts[0])
        self.assertTrue(all(track[-1] == (4320, 255, 47, b"") for track in parts))

    def test_trifet_waltz_keeps_both_piano_staves_and_four_ties(self):
        score = json.loads((ROOT / "repertoire/sobre-las-olas-trifet-theme.json").read_text())
        (bass, melody), spans = score_parts(score)
        self.assertEqual(spans, [72] * 16)
        self.assertEqual((len(bass), len(melody)), (112, 28))
        self.assertEqual(melody[:5],
                         [[0, 59, 72], [72, 58, 24], [96, 59, 24], [120, 62, 24], [144, 67, 96]])
        self.assertEqual([(t, pitch, d) for t, pitch, d in melody if d == 96],
                         [(144, 67, 96), (432, 66, 96), (720, 66, 96), (1008, 59, 96)])
        self.assertEqual([pitch for t, pitch, _ in bass if t == 456], [48, 50, 54])
        self.assertEqual([pitch for t, pitch, _ in bass if t == 1032], [47, 50, 55])
        kind, ppq, (conductor, left, right) = tracks(midi(score))
        self.assertEqual((kind, ppq), (1, 24))
        self.assertIn((0, 255, 88, bytes((3, 2, 24, 8))), conductor)
        self.assertIn((576, 255, 6, b"ANSWERING PHRASE"), conductor)
        self.assertIn((1008, 255, 6, b"CLOSING TWO BARS"), conductor)
        self.assertEqual(note_messages(right)[-1][:3], (1104, 128, 59))
        self.assertTrue(all(track[-1] == (1152, 255, 47, b"") for track in (conductor, left, right)))

    def test_trifet_strain_preserves_the_octave_rise_and_both_endings(self):
        score = json.loads((ROOT / "repertoire/sobre-las-olas-trifet-strain.json").read_text())
        (bass, melody), spans = score_parts(score)
        self.assertEqual(spans, [72] * 64)
        self.assertIn([1584, 64, 96], melody)
        self.assertEqual([p for t,p,_ in melody if t == 1728], [64,76])
        self.assertIn([1872, 86, 96], melody)
        self.assertEqual([p for t,p,_ in bass if t == 2280], [])
        self.assertEqual([p for t,p,_ in bass if t == 4536], [31,43])
        kind, ppq, (conductor, left, right) = tracks(midi(score))
        self.assertEqual((kind, ppq), (1, 24))
        for tick,label in [(2160,b"FIRST ENDING"),(2304,b"REPEAT FROM WALTZ ONE"),
                           (4464,b"SECOND ENDING AND FINE")]:
            self.assertIn((tick,255,6,label),conductor)
        for part in (left, right):
            hits = [(tick, vel) for tick, status, _, vel in note_messages(part) if status == 144]
            for tick,velocity in [(0,46),(1728,112),(2304,46),(4032,112)]:
                self.assertIn((tick,velocity),hits)
            self.assertEqual(part[-1],(4608,255,47,b""))

    def test_ma_teodora_preserves_compound_meter_voices_and_echoes(self):
        score = json.loads((ROOT / "repertoire/ma-teodora-cuba-musical-1929.json").read_text())
        (voice,), spans = score_parts(score)
        self.assertEqual(spans, [36] + [72]*20)
        self.assertEqual(voice[:2], [[0, 77, 24], [24, 76, 12]])
        self.assertEqual([event for event in voice if 108 <= event[0] < 180],
                         [[108, 70, 24], [132, 64, 24],
                          [156, 64, 24], [156, 67, 24], [156, 72, 24]])
        self.assertIn([576, 81, 24], voice)
        kind, ppq, (conductor, performance) = tracks(midi(score))
        self.assertEqual((kind, ppq), (1, 24))
        self.assertIn((0, 255, 88, bytes((6, 3, 36, 8))), conductor)
        self.assertIn((828, 255, 6, b"ECHOES"), conductor)
        hits = {tick: set() for tick, status, _, _ in note_messages(performance) if status == 144}
        for tick, status, pitch, velocity in note_messages(performance):
            if status == 144:
                hits[tick].add((pitch, velocity))
        for tick, velocity in [(888, 62), (1032, 46), (1176, 32), (1320, 32)]:
            self.assertEqual(hits[tick], {(64, velocity), (67, velocity), (72, velocity)})
        self.assertEqual(hits[1404], {(65, 32), (69, 32)})
        self.assertEqual(max(tick for tick, _, _, _ in note_messages(performance)), 1428)
        self.assertEqual(performance[-1], (1476, 255, 47, b""))
        score["parts"][0]["velocity_marks"] = [[888, 62], [888, 46]]
        with self.assertRaisesRegex(ValueError, "increasing ticks"):
            midi(score)

    def test_havana_manisero_keeps_the_partial_bar_and_held_chords(self):
        score = json.loads((ROOT / "repertoire/el-manisero-bohemia-opening.json").read_text())
        (bass, treble, held), spans = score_parts(score)
        self.assertEqual(spans, [36, 48, 48, 48])
        self.assertEqual([event for event in bass if event[1] == 43][:4],
                         [[0, 43, 6], [6, 43, 12], [18, 43, 6], [24, 43, 12]])
        self.assertEqual(treble[8:12], [[36, 84, 6], [42, 79, 6], [48, 76, 6], [54, 72, 6]])
        self.assertEqual(held[:3], [[36, 72, 18], [36, 76, 18], [36, 79, 18]])
        self.assertNotIn([54, 72, 6], held)
        self.assertEqual([event for event in bass if event[0] == 60], [[60, 41, 12], [60, 53, 12]])
        self.assertEqual(bass[-8:], [[132, 36, 12], [132, 48, 12], [144, 40, 6], [150, 52, 6],
                                    [156, 41, 6], [162, 53, 6], [168, 42, 6], [174, 54, 6]])
        self.assertEqual(treble[-12:], [[132, 60, 12], [132, 64, 12], [132, 72, 12],
                                      [144, 64, 12], [144, 67, 12], [144, 76, 12],
                                      [156, 62, 12], [156, 65, 12], [156, 74, 12],
                                      [168, 60, 12], [168, 67, 12], [168, 71, 12]])
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 4))
        self.assertEqual([(tick, label) for tick, status, tag, label in result[0]
                          if status == 255 and tag == 6],
                         [(0, b"LEAD-IN"), (36, b"OPENING REPEAT SIGN"), (84, b"SECOND BAR"),
                          (132, b"RISING BASS")])
        self.assertTrue(all(track[-1] == (180, 255, 47, b"") for track in result))

    def test_havana_manisero_draft_unfolds_both_endings(self):
        base = ROOT / "repertoire"
        excerpt = json.loads((base / "el-manisero-bohemia-opening.json").read_text())
        draft = json.loads((base / "el-manisero-bohemia-first-repeat-review.json").read_text())
        prior, _ = score_parts(excerpt)
        voices, spans = score_parts(draft)
        self.assertEqual(spans, [36] + [48] * 16)
        self.assertEqual(sum(spans), 804)
        for tick, pitches in (
            (192, (55, 59, 65)), (204, (55, 59, 65)),
            (252, (59, 62, 65)), (300, (55, 60, 64)), (768, (55, 59, 65)),
        ):
            self.assertEqual([event for event in voices[0] if event[0] == tick],
                             [[tick, pitch, 12] for pitch in pitches])
        # A7 appears twice, at the end of each pass through the common bars.
        for start in (324, 708):
            self.assertIn([start + 6, 45, 6], voices[0])  # A2 between the bass lines.
            self.assertEqual([event for event in voices[1]
                              if start <= event[0] < start + 48 and event[1] >= 81],
                             [[start, 83, 12], [start + 12, 81, 12], [start + 36, 83, 12]])
        for original, current in zip(prior, voices):
            self.assertEqual(original, [event for event in current if event[0] < 180])
            first = [event for event in current if 36 <= event[0] < 372]
            second = [[tick-384, pitch, length] for tick, pitch, length in current if 420 <= tick < 756]
            self.assertEqual(first, second)
        self.assertNotEqual(
            [pitch for tick, pitch, _ in voices[1] if 372 <= tick < 420],
            [pitch for tick, pitch, _ in voices[1] if 756 <= tick < 804])
        kind, ppq, result = tracks(midi(draft))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 4))
        self.assertEqual([(tick, label) for tick, status, tag, label in result[0]
                          if status == 255 and tag == 6],
                         [(0, b"LEAD-IN"), (36, b"A FIRST PASS"), (372, b"FIRST ENDING"),
                          (420, b"A SECOND PASS"), (756, b"SECOND ENDING")])
        self.assertTrue(all(track[-1] == (804, 255, 47, b"") for track in result))

    def test_siboney_last_ending_preserves_pickup_cadence_and_guide(self):
        score = json.loads((ROOT / "repertoire/siboney-feist-last.json").read_text())
        (bass, treble, counterline, guide, bass_counterline), spans = score_parts(score)
        self.assertEqual(spans, [24] + [48] * 40)
        self.assertEqual(guide[:3], [[0,64,12], [12,67,12], [24,76,72]])
        self.assertEqual(guide[-1], [1752,60,168])
        self.assertEqual([note for note in bass if note[0] == 1896],
                         [[1896,pitch,24] for pitch in (36,43,48,51,55,60)])
        self.assertIn([1734,55,6], bass_counterline)
        self.assertIn([1722,59,12], treble)
        self.assertIn([828,64,24], counterline)
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1,24,6))
        self.assertEqual([(t,label) for t,status,tag,label in result[0] if status==255 and tag==6],
                         [(0,b"CHORUS PICKUP"), (24,b"FIRST PHRASE"), (408,b"SECOND PHRASE"),
                          (792,b"THIRD PHRASE"), (1512,b"FINAL ENDING"), (1752,b"FINAL CADENCE")])
        self.assertTrue(all(track[-1] == (1944,255,47,b"") for track in result))

    def test_siboney_verse_keeps_the_lead_repeat_and_chorus_boundary(self):
        repertoire = ROOT / "repertoire"
        score = json.loads((repertoire / "siboney-feist-verse-last.json").read_text())
        (bass, treble, counterline, bass_counterline, guide), spans = score_parts(score)
        self.assertEqual(spans, [24] + [48] * 39 + [24, 24] + [48] * 40)
        self.assertEqual([(t + 96, p, d) for t, p, d in bass if 408 <= t < 504],
                         [(t, p, d) for t, p, d in bass if 504 <= t < 600])
        self.assertIn([312, 36, 60], bass)
        self.assertIn([330, 43, 6], bass_counterline)
        self.assertIn([1284, 55, 36], bass_counterline)
        self.assertEqual(guide[:5], [[480, 60, 12], [492, 63, 12], [576, 60, 12],
                                    [588, 63, 12], [600, 67, 72]])
        self.assertEqual([note for note in guide if 1750 < note[0] < 1945],
                         [[1752, 60, 156], [1920, 64, 12], [1932, 67, 12], [1944, 76, 72]])
        chorus = json.loads((repertoire / "siboney-feist-last.json").read_text())
        self.assertEqual([part["bars"][41:] for part in score["parts"]],
                         [chorus["parts"][i]["bars"] for i in (0, 1, 2, 4, 3)])
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 6))
        self.assertEqual([(t, label) for t, status, tag, label in result[0] if status == 255 and tag == 6],
                         [(0, b"INTRO"), (408, b"SEGNO LEAD"), (600, b"VERSE"), (1920, b"REFRAIN"),
                          (3432, b"LAST ENDING"), (3672, b"CODA")])
        self.assertTrue(all(track[-1] == (3864, 255, 47, b"") for track in result))

    def test_siboney_song_takes_each_ending_and_returns_to_the_segno(self):
        score = json.loads((ROOT / "repertoire/siboney-feist-song.json").read_text())
        (bass, treble, counterline, bass_counterline, guide), spans = score_parts(score)
        self.assertEqual(len(spans), 181)
        self.assertEqual([bar for bar, size in enumerate(spans, 1) if size == 24],
                         [1, 41, 42, 140, 141])
        self.assertEqual(sum(spans), 8568)
        # First leads directly to the chorus repeat with a harmonized pickup.
        self.assertIn([3480, 52, 18], bass)
        self.assertIn([3552, 64, 12], guide)
        self.assertIn([3564, 67, 12], guide)
        self.assertIn([3564, 64, 12], treble)
        self.assertIn([3576, 76, 72], guide)
        # Second alters the cadence, leaves the lead to the piano and jumps
        # to the earlier segno. The two-bar lead plays once on this pass.
        self.assertIn([5112, 51, 18], bass)
        self.assertEqual([note for note in guide if 5112 <= note[0] < 5208], [[5112, 60, 60]])
        self.assertIn([5196, 63, 12], treble)
        self.assertIn([5208, 36, 18], bass)
        self.assertEqual([note for note in guide if 5208 <= note[0] <= 5304],
                         [[5280, 60, 12], [5292, 63, 12], [5304, 67, 72]])
        self.assertIn([8136 + 48, 51, 18], bass)
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 6))
        self.assertEqual([(t, label) for t, status, tag, label in result[0] if status == 255 and tag == 6],
                         [(0, b"INTRO"), (408, b"SEGNO LEAD"), (600, b"VERSE ONE"), (1920, b"REFRAIN ONE"),
                          (3432, b"FIRST ENDING"), (3576, b"CHORUS REPEAT"), (5064, b"SECOND ENDING"),
                          (5208, b"D.S. RETURN"), (5304, b"VERSE TWO"), (6624, b"REFRAIN TWO"),
                          (8136, b"LAST ENDING"), (8376, b"FINAL CADENCE")])
        self.assertTrue(all(track[-1] == (8568, 255, 47, b"") for track in result))

    def test_la_mora_opening_unfolds_both_endings(self):
        score = json.loads((ROOT / "repertoire/la-mora-grenet-opening-bass.json").read_text())
        voices, spans = score_parts(score)
        self.assertEqual(spans, [48] * 17)
        lower = voices[0]
        self.assertEqual(lower[:3], [[36,54,12], [36,61,12], [36,62,12]])
        self.assertEqual([(t+384,p,d) for t,p,d in lower if 48<=t<384],
                         [(t,p,d) for t,p,d in lower if 432<=t<768])
        self.assertEqual([(t,p) for t,p,_ in lower if t>=768],
                         [(768,36),(768,48),(780,36),(780,48),(804,36),(804,48)])
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1,24,2))
        self.assertEqual([(t,label) for t,status,tag,label in result[0] if status==255 and tag==6],
                         [(0,b"INTRO"),(48,b"FIRST STRAIN"),(384,b"FIRST ENDING"),
                          (432,b"STRAIN REPEAT"),(768,b"SECOND ENDING")])
        self.assertTrue(all(track[-1]==(816,255,47,b"") for track in result))

    def test_la_mora_two_staves_retain_rolls_grace_and_tie(self):
        base = ROOT / "repertoire"
        bass, treble = [json.loads((base / f"la-mora-grenet-opening-{staff}.json").read_text())
                        for staff in ("bass", "treble")]
        right, spans = score_parts(treble)
        self.assertEqual(spans, [48] * 17)
        notes = {tuple(note) for track in right for note in track}
        self.assertTrue({(264,70,18), (265,76,17), (266,81,16),
                         (300,69,18), (300,72,18), (300,77,18),
                         (359,76,1), (360,65,6), (361,68,5), (362,70,4),
                         (363,74,3)} <= notes)
        self.assertNotIn((312,77,6), notes)
        kind, ppq, result = tracks(rehearsal_midi("La Mora", bass, right_hand=treble))
        self.assertEqual((kind, ppq, len(result)), (1,24,6))
        self.assertEqual(note_messages(result[1]), note_messages(tracks(midi(bass))[2][1]))
        self.assertEqual([note_messages(track) for track in result[2:]],
                         [note_messages(track) for track in tracks(midi(treble))[2][1:]])
        self.assertTrue(all(track[-1] == (816,255,47,b"") for track in result))
        treble["source_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "same retained score"):
            rehearsal_midi("La Mora", bass, right_hand=treble)

    def test_la_mora_closing_refrain_retains_ties_and_final_voicing(self):
        score = json.loads((ROOT / "repertoire/la-mora-grenet-refrain.json").read_text())
        (bass, treble), spans = score_parts(score)
        self.assertEqual(spans, [48] * 8)
        self.assertEqual([note for note in treble if note[1] == 60 and note[2] > 12],
                         [[36, 60, 18], [132, 60, 24], [228, 60, 18]])
        self.assertEqual([note for note in bass if note[0] == 372],
                         [[372, 54, 12], [372, 63, 12]])
        self.assertEqual(treble[-4:], [[360, 79, 6], [366, 81, 6], [372, 84, 6], [378, 81, 6]])
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 3))
        self.assertEqual([(t, label) for t, status, tag, label in result[0] if status == 255 and tag == 6],
                         [(0, b"REFRAIN"), (192, b"REFRAIN RETURN"), (288, b"CLOSING FIGURE")])
        self.assertTrue(all(track[-1] == (384, 255, 47, b"") for track in result))

    def test_la_mora_interlude_preserves_the_page_four_variants(self):
        score = json.loads((ROOT / "repertoire/la-mora-grenet-interlude.json").read_text())
        (bass, treble), spans = score_parts(score)
        self.assertEqual(spans, [48] * 8)
        self.assertEqual([note for note in treble if note[0] in (84, 90)],
                         [[84, 64, 6], [90, 64, 6]])
        self.assertEqual([note for note in treble if note[1] == 60 and note[0] in (132, 144)],
                         [[132, 60, 12], [144, 60, 12]])
        self.assertEqual([note for note in bass if note[0] == 276],
                         [[276, 36, 12], [276, 40, 12]])
        self.assertEqual(treble[-2:], [[372, 72, 12], [372, 76, 12]])
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 3))
        self.assertTrue(all(track[-1] == (384, 255, 47, b"") for track in result))

    def test_la_mora_sung_backing_unfolds_repeat_and_two_endings(self):
        score = json.loads((ROOT / "repertoire/la-mora-grenet-sung-bass.json").read_text())
        (bass,), spans = score_parts(score)
        self.assertEqual(spans, [48] * 22)
        self.assertEqual([(t + 528, pitch, duration) for t, pitch, duration in bass if t < 480],
                         [(t, pitch, duration) for t, pitch, duration in bass if 528 <= t < 1008])
        self.assertEqual([note for note in bass if 192 <= note[0] < 216],
                         [[192, 36, 8], [192, 48, 8], [200, 52, 8], [208, 55, 8]])
        self.assertEqual([note for note in bass if 480 <= note[0] < 528],
                         [[480, 41, 12], [480, 53, 12], [516, 36, 12], [516, 48, 12]])
        self.assertEqual(bass[-5:], [[1020, 53, 6], [1026, 57, 6], [1032, 60, 12],
                                    [1044, 36, 12], [1044, 48, 12]])
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 2))
        self.assertEqual([(t, label) for t, status, tag, label in result[0] if status == 255 and tag == 6],
                         [(0, b"FIRST PASS"), (480, b"FIRST ENDING"), (528, b"REPEAT"), (1008, b"SECOND ENDING")])
        self.assertTrue(all(track[-1] == (1056, 255, 47, b"") for track in result))

    def test_la_mora_sung_guide_preserves_ties_and_reads_the_two_endings(self):
        base = ROOT / "repertoire"
        bass, guide = [json.loads((base / f"la-mora-grenet-sung-{part}.json").read_text())
                       for part in ("bass", "guide")]
        melody, end = notes(guide)
        self.assertEqual(end, 1056)
        self.assertIn([36, 81, 48], melody)
        self.assertIn([114, 76, 54], melody)
        self.assertIn([336, 82, 84], melody)
        self.assertEqual([e for e in melody if 456 <= e[0] < 528],
                         [[456, 77, 36], [492, 72, 12], [504, 74, 12], [516, 76, 12]])
        self.assertEqual(melody[-5:],
                         [[1008, 77, 24], [1032, 77, 4], [1036, 81, 8], [1044, 84, 4], [1048, 81, 8]])
        kind, ppq, result = tracks(rehearsal_midi("La Mora", bass, guide))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 3))
        self.assertTrue(all(track[-1] == (end, 255, 47, b"") for track in result))

    def test_la_mora_finale_carries_the_second_ending_into_the_continuation(self):
        base = ROOT / "repertoire"
        source = json.loads((base / "la-mora-grenet-singer-finale.json").read_text())
        (bass, piano, guide, inner), spans = score_parts(source)
        self.assertEqual(spans, [48] * 43)
        self.assertEqual(inner, [
            [1404, 52, 6], [1404, 60, 6],
            [1410, 52, 18], [1410, 55, 18],
            [1428, 52, 12], [1428, 60, 12],
        ])
        self.assertEqual([note for note in bass if note[0] == 23*48 - 12],
                         [[1092, 36, 48], [1092, 48, 48]])
        self.assertFalse(any(note[0] == 23*48 for note in bass))
        self.assertEqual([note for note in piano if 34*48 <= note[0] < 35*48],
                         [[1632,60,6], [1638,62,6], [1644,64,6], [1656,65,6],
                          [1662,64,6], [1668,62,6], [1674,60,6]])
        self.assertLessEqual(guide[-1][0] + guide[-1][2], 41*48)
        kind, ppq, result = tracks(midi(source))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 5))
        self.assertIn((1680, 255, 6, b"REFRAIN"), result[0])
        self.assertTrue(all(track[-1] == (2064, 255, 47, b"") for track in result))

    def test_cielito_full_piano_keeps_chromatic_voicing_and_rolled_attacks(self):
        repo = ROOT / "repertoire"
        backing, guide, right = [json.loads((repo / f"cielito-lindo-songbag-{part}.json").read_text())
                                 for part in ("left-hand", "canto", "right-hand")]
        voices, spans = score_parts(right)
        self.assertEqual(spans, [24] + [72] * 31 + [48])
        self.assertEqual([voice[0] for voice in voices[1:]],
                         [[120,59,24], [121,65,23], [122,67,22], [123,71,21]])
        self.assertEqual([p for tick,p,d in voices[0] if tick==1296], [64,68,72])
        self.assertEqual([p for tick,p,d in voices[0] if tick==1848], [69])
        for pitch in (67,71,74):
            self.assertIn([1944,pitch,48], voices[0])
        kind, ppq, result = tracks(rehearsal_midi("Cielito Lindo",backing,guide,right))
        self.assertEqual((kind, ppq, len(result)), (1,24,9))
        self.assertEqual([note_messages(track) for track in result[4:]],
                         [note_messages(track) for track in tracks(midi(right))[2][1:]])
        self.assertEqual(note_messages(result[3]), note_messages(tracks(midi(guide))[2][1]))
        self.assertTrue(all(track[-1] == (2304,255,47,b"") for track in result))
        # Piano releases a beat before the guide at the printed final rest.
        self.assertEqual([e[2] for e in note_messages(result[4]) if e[0]==2280 and e[1]==128],
                         [72,76,79,84])
        right["source_sha256"] = "0"*64
        with self.assertRaisesRegex(ValueError,"same retained score"):
            rehearsal_midi("Cielito Lindo",backing,guide,right)

    def test_cielito_bass_graces_and_ties_remain_independent_of_guide(self):
        repo = ROOT / "repertoire"
        backing, guide = [json.loads((repo / f"cielito-lindo-songbag-{part}.json").read_text())
                          for part in ("left-hand", "canto")]
        voices, spans = score_parts(backing)
        self.assertEqual(spans, [24] + [72] * 31 + [48])
        self.assertEqual(voices[0][:2], [[24,48,72], [96,50,72]])
        self.assertEqual(voices[1][:2], [[24,54,2], [26,55,142]])
        self.assertIn([1608,36,120], voices[0])
        self.assertIn([1608,48,120], voices[1])
        self.assertIn([2088,55,48], voices[1])
        kind, ppq, result = tracks(rehearsal_midi("Cielito Lindo", backing, guide))
        self.assertEqual((kind, ppq, len(result)), (1,24,4))
        self.assertEqual([note_messages(track) for track in result[1:3]],
                         [note_messages(track) for track in tracks(midi(backing))[2][1:]])
        self.assertEqual(note_messages(result[3]), note_messages(tracks(midi(guide))[2][1]))
        self.assertTrue(all(track[-1] == (2304,255,47,b"") for track in result))
        self.assertEqual([event[2] for event in note_messages(result[1])
                          if event[0]==2256 and event[1]==144], [36])
        guide["parts"][0]["bars"][-1]["ticks"] = 72
        guide["parts"][0]["bars"][-1]["notes"] = "C5:72"
        with self.assertRaisesRegex(ValueError, "share every bar line"):
            rehearsal_midi("Cielito Lindo", backing, guide)

    def test_cielito_songbag_preserves_pickup_chorus_and_cadence(self):
        score = json.loads((ROOT / "repertoire/cielito-lindo-songbag-canto.json").read_text())
        kind, ppq, (conductor, canto) = tracks(midi(score))
        self.assertEqual((kind, ppq), (1, 24))
        self.assertIn((0, 255, 88, bytes((3, 2, 24, 8))), conductor)
        self.assertEqual([e for e in conductor if e[1:3] == (255, 6)],
                         [(0, 255, 6, b"VERSE"), (1176, 255, 6, b"CHORUS")])
        self.assertIn((0, 255, 1, ("Source scan SHA-256: " +
                          score["source_sha256"]).encode()), conductor)
        messages = note_messages(canto)
        self.assertEqual(sum(e[1] == 144 for e in messages), 62)
        self.assertEqual(messages[:4], [(0,144,67,82), (24,128,67,0),
                                        (24,144,72,82), (48,128,72,0)])
        # The printed six-beat chorus A4 must not retrigger at the bar line.
        self.assertEqual([e for e in messages if 1320 <= e[0] < 1464 and e[1] == 144],
                         [(1320,144,69,82)])
        self.assertIn((1464,128,69,0), messages)
        self.assertEqual(messages[-2:], [(2208,144,72,82), (2304,128,72,0)])
        self.assertEqual(canto[-1], (2304,255,47,b""))
        self.assertEqual(conductor[-1], canto[-1])

    def ledgers(self, song):
        return [json.loads((ROOT / "repertoire" / f"{song}-{part}.json").read_text())
                for part in ("left-hand", "canto")]

    def test_rehearsal_tracks_match_separate_midi_and_board_form(self):
        for song, markers in (
                ("la-paloma", [0, 636, 1596, 3036]),
                ("la-golondrina", [0, 384, 1920, 3456])):
            with self.subTest(song=song):
                backing, guide = self.ledgers(song)
                kind, ppq, result = tracks(rehearsal_midi(song, backing, guide))
                self.assertEqual((kind, ppq, len(result)), (1, 24, 3))
                conductor, piano, canto = result
                self.assertEqual(conductor[:2], tracks(midi(backing))[2][0][:2])
                self.assertEqual([(tick, data) for tick, status, tag, data in conductor
                                  if status == 255 and tag == 6],
                                 list(zip(markers, (b"INTRO", b"VERSE", b"REFRAIN", b"CODA"))))
                for index, (score, events) in enumerate(((backing, piano), (guide, canto))):
                    self.assertEqual(note_messages(events), note_messages(tracks(midi(score))[2][0]))
                    self.assertTrue(all(event[1] & 15 == index for event in events if event[1] != 255))
                    self.assertEqual(events[-1], (notes(backing)[1], 255, 47, b""))
                self.assertEqual(conductor[-1], piano[-1])
                self.assertIn((0, 255, 1, ("Source scan SHA-256: " +
                              backing["source_sha256"]).encode()), conductor)

    def test_kempton_excerpt_preserves_held_notes_over_moving_voices(self):
        score = json.loads((ROOT / "repertoire/el-manisero-kempton-calls.json").read_text())
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 5))
        conductor, call, upper, lower, bass = result
        self.assertIn((0, 255, 88, bytes((2, 1, 24, 8))), conductor)
        self.assertEqual([e for e in conductor if e[1:3] == (255, 6)],
                         [(0, 255, 6, b"SEGNO"), (384, 255, 6, b"VENDOR CALLS")])
        self.assertEqual(note_messages(call), [
            (408, 144, 71, 82), (432, 128, 71, 0),
            (432, 144, 74, 82), (576, 128, 74, 0),
            (600, 144, 71, 82), (624, 128, 71, 0),
            (624, 144, 74, 82), (768, 128, 74, 0),
        ])
        self.assertEqual(note_messages(upper)[-8:], note_messages(call))
        self.assertEqual([e for e in note_messages(lower) if 432 <= e[0] < 576 and e[1] == 144],
                         [(432, 144, 62, 82), (456, 144, 64, 82), (504, 144, 66, 82),
                          (516, 144, 64, 82), (540, 144, 64, 82), (552, 144, 62, 82)])
        self.assertEqual([e[2] for e in note_messages(bass) if e[0] == 504 and e[1] == 144],
                         [50, 54, 60])
        for channel, part in enumerate(result[1:]):
            self.assertTrue(all(e[1] & 15 == channel for e in part if e[1] != 255))
        self.assertTrue(all(part[-1] == (768, 255, 47, b"") for part in result))
        score["parts"][2]["bars"].pop()
        with self.assertRaisesRegex(ValueError, "share every bar line"):
            midi(score)

    def test_kempton_complete_lower_staff_follows_ds_and_coda(self):
        score = json.loads((ROOT / "repertoire/el-manisero-kempton-bass.json").read_text())
        fragment = json.loads((ROOT / "repertoire/el-manisero-kempton-calls.json").read_text())
        full, end = notes(score)
        excerpt, _ = notes({"ppq":24, "meter":[2,2], "bars":fragment["parts"][-1]["bars"]})
        self.assertEqual(end, 89 * 96)
        self.assertEqual([bar["bar"] for bar in score["form"]], [1,5,9,81])
        self.assertEqual([[tick-384,pitch,length] for tick,pitch,length in full
                          if 384 <= tick < 1152], excerpt)
        self.assertEqual([[tick+38*96,pitch,length] for tick,pitch,length in full
                          if 384 <= tick < 42*96],
                         [[tick,pitch,length] for tick,pitch,length in full
                          if 42*96 <= tick < 80*96])
        self.assertIn([80*96,31,12], full)       # Coda starts on G1.
        self.assertIn([84*96,55,2], full)        # Unmeasured G3 grace realization.
        self.assertIn([84*96+2,67,10], full)
        self.assertEqual(full[-2:], [[88*96,31,12], [88*96,43,12]])
        kind, ppq, result = tracks(midi(score))
        self.assertEqual((kind, ppq, len(result)), (0, 24, 1))
        self.assertIn((0,255,88,bytes((2,1,24,8))), result[0])
        self.assertEqual(result[0][-1], (89*96,255,47,b""))

    def test_kempton_guide_matches_written_call_and_retains_coda_tie(self):
        repo = ROOT / "repertoire"
        backing, guide, excerpt = [json.loads((repo / f"el-manisero-kempton-{part}.json").read_text())
                                   for part in ("bass", "guide", "calls")]
        full, end = notes(guide)
        excerpt_notes, _ = notes({"ppq":24, "meter":[2,2], "bars":excerpt["parts"][0]["bars"]})
        self.assertEqual([[tick-384,pitch,length] for tick,pitch,length in full
                          if 768 <= tick < 1152], excerpt_notes)
        self.assertEqual((end, full[-1]), (8544, [8112, 74, 312]))
        kind, ppq, (conductor, bass, vocal) = tracks(rehearsal_midi("El Manisero", backing, guide))
        self.assertEqual((kind, ppq), (1,24))
        self.assertEqual(note_messages(vocal)[-2:], [(8112,144,74,82),(8424,128,74,0)])
        self.assertEqual(vocal[-1], bass[-1])
        self.assertEqual(vocal[-1], conductor[-1])

    def test_kempton_printed_piano_matches_excerpt_and_keeps_coda_voices(self):
        repo = ROOT / "repertoire"
        backing, guide, right, excerpt = [
            json.loads((repo / f"el-manisero-kempton-{part}.json").read_text())
            for part in ("bass", "guide", "right-hand", "calls")
        ]
        voices, spans = score_parts(right)
        excerpt_voices, _ = score_parts(excerpt)
        self.assertEqual(spans, [96] * 89)
        self.assertEqual(
            sorted((tick - 384, pitch, length) for voice in voices for tick, pitch, length in voice
                   if 384 <= tick < 1152),
            sorted(tuple(note) for voice in excerpt_voices[1:3] for note in voice),
        )
        self.assertIn([81 * 96 - 48, 62, 144], voices[0])
        self.assertIn([81 * 96, 57, 96], voices[1])
        kind, ppq, result = tracks(rehearsal_midi("El Manisero", backing, guide, right))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 5))
        self.assertTrue(all(track[-1] == (8544, 255, 47, b"") for track in result))
        self.assertEqual(
            [note_messages(track) for track in result[3:]],
            [note_messages(track) for track in tracks(midi(right))[2][1:]],
        )

        right["play_order"] = right["play_order"][::-1]
        with self.assertRaisesRegex(ValueError, "share the written play order"):
            rehearsal_midi("El Manisero", backing, guide, right)

    def test_kempton_whistle_keeps_its_own_tied_coda_track(self):
        repo = ROOT / "repertoire"
        backing, guide, right, whistle = [
            json.loads((repo / f"el-manisero-kempton-{part}.json").read_text())
            for part in ("bass", "guide", "right-hand", "whistle")
        ]
        (voice,), spans = score_parts(whistle)
        self.assertEqual(spans, [96] * 89)
        self.assertEqual(voice, [[7776, 74, 96], [7968, 74, 96], [8160, 74, 120], [8424, 74, 24]])
        kind, ppq, result = tracks(rehearsal_midi("El Manisero", backing, guide, right, extra=whistle))
        self.assertEqual((kind, ppq, len(result)), (1, 24, 6))
        self.assertEqual(result[:-1], tracks(rehearsal_midi("El Manisero", backing, guide, right))[2])
        stem = tracks(midi(whistle))[2][1]
        self.assertEqual(note_messages(result[-1]), note_messages(stem))
        self.assertIn((0, 0xc4, 78), result[-1])
        self.assertTrue(all(track[-1] == (8544, 255, 47, b"") for track in result))
        whistle["source_sha256"] = "unverified"
        with self.assertRaisesRegex(ValueError, "same retained score"):
            rehearsal_midi("El Manisero", backing, guide, right, extra=whistle)

    def test_adelita_quartet_keeps_written_octaves_and_barline_ties(self):
        score = json.loads((ROOT / "repertoire/la-adelita-kemp-opening.json").read_text())
        kind, ppq, (conductor, alto1, alto2, tenor, bass) = tracks(midi(score))
        self.assertEqual((kind, ppq), (1, 24))
        self.assertIn((0, 255, 1, b"CC BY 4.0"), conductor)
        self.assertIn((0, 255, 1, b"https://creativecommons.org/licenses/by/4.0/"), conductor)
        self.assertEqual(note_messages(alto1)[:2], [(12, 144, 83, 82), (24, 128, 83, 0)])
        self.assertIn((396, 128, 76, 0), note_messages(alto1))
        self.assertIn((396, 128, 72, 0), note_messages(alto2))
        self.assertIn((144, 144, 72, 82), note_messages(tenor))
        self.assertEqual(note_messages(bass)[0], (48, 144, 43, 82))
        self.assertTrue(all(track[-1] == (432, 255, 47, b"")
                            for track in (conductor, alto1, alto2, tenor, bass)))

    def test_adelita_complete_score_preserves_all_four_voices(self):
        base = ROOT / "repertoire"
        score = json.loads((base / "la-adelita-kemp-quartet.json").read_text())
        opening = json.loads((base / "la-adelita-kemp-opening.json").read_text())
        for part, excerpt in zip(score["parts"], opening["parts"]):
            self.assertEqual(part["bars"][:9], excerpt["bars"])
        voices, spans = score_parts(score)
        self.assertEqual(spans, [48] * 73)
        self.assertEqual(sum(len(voice) for voice in voices), 759)
        self.assertIn([27 * 48 + 24, 79, 36], voices[0])  # Into the next system.
        self.assertIn([67 * 48 + 24, 72, 48], voices[2])  # Tenor held across m. 68.
        self.assertIn([70 * 48 + 36, 67, 24], voices[2])  # Tenor held into m. 72.
        self.assertIn([71 * 48, 76, 72], voices[0])
        self.assertIn([71 * 48, 72, 72], voices[1])
        kind, ppq, (conductor, *parts) = tracks(midi(score))
        self.assertEqual((kind, ppq, len(parts)), (1, 24, 4))
        self.assertEqual([(t, label) for t, status, tag, label in conductor
                          if status == 255 and tag == 6],
                         [(0, b"PAGE 1"), (1008, b"PAGE 2"),
                          (2016, b"PAGE 3"), (3024, b"PAGE 4")])
        self.assertTrue(all(note_messages(part)[-1][0] == 3480 for part in parts))
        self.assertTrue(all(part[-1] == (3504, 255, 47, b"") for part in [conductor, *parts]))

    def test_juana_fragment_keeps_tie_and_carries_share_alike_attribution(self):
        score = json.loads((ROOT / "repertoire/juana-quilina-fragment.json").read_text())
        kind, ppq, (conductor, melody) = tracks(midi(score))
        self.assertEqual((kind, ppq), (1, 24))
        self.assertIn((0, 255, 88, bytes((2, 2, 24, 8))), conductor)
        credit = score["attribution"]
        for field in ("author", "source_page", "license", "license_url", "changes"):
            self.assertIn((0, 255, 1, credit[field].encode("ascii")), conductor)
        messages = note_messages(melody)
        self.assertEqual(sum(status == 144 for _, status, _, _ in messages), 37)
        self.assertIn((306, 144, 74, 82), messages)
        self.assertIn((312, 144, 79, 82), messages)
        self.assertIn((360, 128, 79, 0), messages)
        self.assertEqual(melody[-1], (432, 255, 47, b""))

    def test_different_retained_source_is_refused(self):
        backing, guide = self.ledgers("la-paloma")
        guide["source_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "same retained score"):
            rehearsal_midi("La Paloma", backing, guide)

    def test_paloma_two_hands_align_and_keep_ties_and_final_chord(self):
        backing, guide = self.ledgers("la-paloma")
        right_hand = json.loads((ROOT / "repertoire/la-paloma-right-hand.json").read_text())
        events, end = notes(right_hand)
        self.assertEqual(end, 3840)
        self.assertEqual(events[:4], [[0,67,12], [0,79,12], [12,67,56], [12,79,56]])
        self.assertEqual(events[-3:], [[3828,64,12], [3828,67,12], [3828,72,12]])
        kind, ppq, (conductor, bass, canto, right) = tracks(
            rehearsal_midi("La Paloma", backing, guide, right_hand))
        self.assertEqual((kind, ppq), (1, 24))
        self.assertEqual(note_messages(right), note_messages(tracks(midi(right_hand))[2][0]))
        self.assertTrue(all(event[1] & 15 == 2 for event in right if event[1] != 255))
        self.assertTrue(all(track[-1] == (3840,255,47,b"") for track in (conductor,bass,canto,right)))

    def test_golondrina_second_refrain_ending_and_coda_have_written_piano(self):
        backing, guide = self.ledgers("la-golondrina")
        treble = json.loads((ROOT / "repertoire/la-golondrina-right-hand.json").read_text())
        events, end = notes(treble)
        self.assertEqual(end, 3810)
        self.assertEqual([e for e in events if 3432 <= e[0] < 3456],
                         [[3432,74,6],[3438,72,6],[3444,71,6],[3450,72,6]])
        self.assertEqual(events[-4:], [[3792,pitch,12] for pitch in (65,69,72,77)])
        self.assertEqual([e for e in notes(backing)[0] if e[0]==3768],
                         [[3768,53,24],[3768,65,24]])
        _, _, (conductor, bass, canto, right) = tracks(rehearsal_midi("La Golondrina",backing,guide,treble))
        self.assertEqual(note_messages(right), note_messages(tracks(midi(treble))[2][0]))
        self.assertTrue(all(track[-1] == (3810,255,47,b"") for track in (conductor,bass,canto,right)))


if __name__ == "__main__":
    unittest.main()
