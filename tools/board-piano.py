"""Compile the board repertoire into bounded, score-specific flash tables."""
import argparse
import json
from itertools import groupby
from pathlib import Path

from repertoire_midi import aligned_right_hand, bars, check_source_clock, notes, score_parts

root = Path(__file__).resolve().parents[1]
repertoire = root / "repertoire"
MAX_VOICES = 9  # Wathall's La Cucaracha cadence holds nine written piano notes.


def allocate_lanes(events):
    lanes = []
    for start, pitch, duration in sorted(events):
        for lane in lanes:
            previous_start, _, previous_duration = lane[-1]
            if previous_start + previous_duration <= start:
                lane.append((start, pitch, duration))
                break
        else:
            if len(lanes) == MAX_VOICES:
                raise ValueError(f"More than {MAX_VOICES} backing voices at tick {start}")
            lanes.append([(start, pitch, duration)])
    return lanes


def packed_note(start, duration, index):
    """PianoNote in board/piano.c: tick, then duration | pitch index << 10."""
    if not (0 <= start < 65536 and 0 < duration < 1024 and 0 < index < 64):
        raise ValueError(f"note does not fit the packed score format: {start} {duration} {index}")
    return f"{{{start},{duration | index << 10}}}"


def phase_increment(pitch):
    return round(2**32 * 440 * 2**((pitch - 69) / 12) / 44100)


def compile_score(entry, index, compact=False):
    score = json.loads((repertoire / entry["backing"]).read_text())
    beats, denominator = score["meter"]
    multipart = "parts" in score
    independent = multipart or "right_hand" in entry
    quarter_meter = denominator == 4 and beats in (2, 3, 4)
    cut_time = (beats, denominator) == (2, 2)
    compound = (beats, denominator) == (6, 8)
    if score["ppq"] != 24 or not (quarter_meter or cut_time or compound):
        raise ValueError("The board piano expects 24 PPQ and a supported score meter")

    guide_events = []
    if "guide_part" in entry and not multipart:
        raise ValueError("A guide_part index requires a multi-part score")
    if multipart:
        tracks, bar_spans = score_parts(score)
        if "guide_part" in entry:
            part_index = entry["guide_part"]
            if type(part_index) is not int or not 0 <= part_index < len(tracks):
                raise ValueError("Invalid guide part index")
            guide_events = tracks.pop(part_index)
            if not guide_events:
                raise ValueError("The selected guide part has no notes")
        events = [note for track in tracks for note in track]
        if "right_hand" in entry:
            right_hand = json.loads((repertoire / entry["right_hand"]).read_text())
            events += [event for _, _, voice in aligned_right_hand(score, right_hand) for event in voice]
        if not events:
            raise ValueError("A score needs backing notes")
        lanes = allocate_lanes(events)
        end = sum(bar_spans)
    else:
        events, end = notes(score)
        bar_spans = [96 * beats // denominator if partial is None else partial for _, partial in bars(score)]
        if independent:
            right_hand = json.loads((repertoire / entry["right_hand"]).read_text())
            events += [event for _, _, voice in aligned_right_hand(score, right_hand) for event in voice]
            lanes = allocate_lanes(events)
    starts = []
    tick = 0
    for span in bar_spans:
        starts.append(tick)
        tick += span
    if tick != end or end > 32767 or len(starts) > 255:
        raise ValueError("Score length outside the player limits")

    if "guide" in entry:
        if "guide_part" in entry:
            raise ValueError("Choose either an external guide or a guide_part index")
        guide = json.loads((repertoire / entry["guide"]).read_text())
        check_source_clock(score, guide)
        if "parts" in guide:
            guide_tracks, guide_spans = score_parts(guide)
            if len(guide_tracks) != 1:
                raise ValueError("The board canto needs one written voice")
            guide_events = guide_tracks[0]
        else:
            guide_events, _ = notes(guide)
            guide_spans = [96 * beats // denominator if partial is None else partial
                           for _, partial in bars(guide)]
        if guide_spans != bar_spans:
            raise ValueError("Canto and backing bar lines differ")

    if entry.get("guide_top"):
        # The FM-1 guide has one oscillator. Keep a printed divisi intact
        # in the MIDI ledger and play the higher voice on the board.
        top = []
        for start, group in groupby(guide_events, key=lambda event: event[0]):
            chord = list(group)
            if any(event[2] != chord[0][2] for event in chord):
                raise ValueError(f"Unequal canto chord durations at tick {start}")
            top.append(max(chord, key=lambda event: event[1]))
        guide_events = top

    all_notes = events + guide_events
    low, high = min(e[1] for e in all_notes), max(e[1] for e in all_notes)
    guide_rows = []
    previous_end = 0
    for start, pitch, duration in guide_events:
        if start < previous_end:
            raise ValueError(f"Unsupported canto overlap at tick {start}")
        guide_rows.append(f"    {packed_note(start, duration, pitch-low+1)},")
        previous_end = start + duration

    rows = []
    previous_end = 0
    voices = 0
    if independent:
        voices = len(lanes)
    else:
        for start, group in groupby(events, key=lambda event: event[0]):
            chord = list(group)
            duration = chord[0][2]
            if (start < previous_end or len(chord) > 4 or duration > 255 or
                    any(e[2] != duration for e in chord)):
                raise ValueError(f"Unsupported overlap at tick {start}")
            voices = max(voices,len(chord))
            indexes = [e[1] - low + 1 for e in chord]
            rows.append(f"    {{{start},{duration},{{{','.join(map(str,indexes))}}}}},")
            previous_end = start + duration

    sections = entry.get("form", score["form"])
    section_bars = [x["bar"] - 1 for x in sections]
    if [x["name"] for x in sections] != ["INTRO", "VERSE", "REFRAIN", "CODA"]:
        raise ValueError("Unexpected score form")
    # A score with no written introduction can cue its opening verse from either key.
    if (section_bars[0] != 0 or not 0 <= section_bars[1] < section_bars[2] < section_bars[3]
            or section_bars[-1] >= len(starts)):
        raise ValueError("Invalid section positions")
    repeat_bar = 0
    if "repeat_bar" in entry:
        repeat = entry["repeat_bar"]
        if type(repeat) is not int or not section_bars[2] < repeat - 1 < section_bars[3] - section_bars[2]:
            raise ValueError("The repeated form must reach its refrain before the final cadence")
        repeat_bar = repeat - 1
    # Six lower and five higher notes leave source event IDs unchanged.
    # Offset six is bit-identical to the original printed-pitch table.
    data = (
        f"/* Generated from {entry['backing']}. */\n"
        f"static const uint16_t bars_{index}[] = {{" + ",".join(map(str, starts)) + "};\n"
    )
    increments = f"increments_{index}"
    if compact:
        if not 7 <= low <= high <= 127:
            raise ValueError("Score is outside the compact MIDI pitch table")
        # Events use one-based pitch indices. The player never reads index zero.
        increments = f"piano_increments+{low-7}"
    else:
        phases = [phase_increment(p) for p in range(low-6, high+6)]
        data += f"static const uint32_t {increments}[] = {{0," + ",".join(map(str, phases)) + "};\n"
    hits = "0"
    lane_table = "0"
    if independent:
        for lane_id, lane in enumerate(lanes):
            data += f"static const PianoNote voice_{index}_{lane_id}[] = {{\n"
            data += "".join(f"    {packed_note(start, duration, pitch-low+1)},\n" for start, pitch, duration in lane)
            data += "};\n"
        lane_table = f"lanes_{index}"
        data += f"static const NoteLane {lane_table}[] = {{\n"
        data += "".join(f"    {{voice_{index}_{lane_id},{len(lane)}}},\n" for lane_id, lane in enumerate(lanes))
        data += "};\n"
    else:
        hits = f"hits_{index}"
        data += f"static const Chord {hits}[] = {{\n" + "\n".join(rows) + "\n};\n"
    canto = "0"
    if guide_rows:
        canto = f"canto_{index}"
        data += f"static const PianoNote {canto}[] = {{\n" + "\n".join(guide_rows) + "\n};\n"
    form = ",".join(map(str,section_bars))
    # A gain change is applied to the sum of the written staves.
    parts = score.get("parts", [])
    part_marks = [part.get("velocity_marks", []) for part in parts]
    shared_marks = next((marks for marks in part_marks if marks), [])
    marks, marks_table = [], "0"
    if shared_marks:
        if ("right_hand" in entry or guide_events or
                any(marks != shared_marks for marks in part_marks)):
            raise ValueError("Board dynamics must be shared by all written staves")
        last_tick = -1
        for tick, velocity in shared_marks:
            if (type(tick) is not int or not last_tick < tick < end or
                    type(velocity) is not int or not 1 <= velocity <= 127):
                raise ValueError("Invalid board dynamic mark")
            marks.append(f"{{{tick},{round(velocity * 128 / 82)}}}")
            last_tick = tick
        if len(marks) > 255:
            raise ValueError("Too many board dynamic marks")
        marks_table = f"marks_{index}"
        data += f"static const PianoMark {marks_table}[] = {{" + ",".join(marks) + "};\n"

    pulses = 2 if compound else beats
    pulse_ticks = 36 if compound else 48 if cut_time else 24
    tempo_ticks = 36 if compound else 24
    fields = (f"bars_{index},{increments},{hits},{canto},"
              f"{end},{len(rows)},{len(guide_rows)},{len(starts)},{pulses},{voices},"
              f"{{{form}}},{repeat_bar},{lane_table},{marks_table},{len(marks)},{pulse_ticks},{tempo_ticks}")
    return data, fields, (beats, denominator)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, default=repertoire / "board-scores.json")
    parser.add_argument("--output", type=Path, default=root / "build/board/piano.inc")
    parser.add_argument("--compact", action="store_true")
    args = parser.parse_args()
    registry = json.loads(args.registry.read_text())
    independent = any("right_hand" in entry or "parts" in json.loads((repertoire / entry["backing"]).read_text())
                      for entry in registry)
    tables, definitions, notation = [], [], {}
    for index, entry in enumerate(registry):
        # Playback variants can share note tables while retaining distinct IDs.
        key = json.dumps({name: value for name, value in entry.items()
                          if name not in ("id", "title", "catalog", "bpm", "percussion")},
                         sort_keys=True)
        if key not in notation:
            data, fields, meter = compile_score(entry, index, args.compact)
            tables.append(data)
            notation[key] = fields, meter
        fields, meter = notation[key]
        percussion = entry.get("percussion")
        if percussion is not None and (percussion != "bolero" or meter != (4, 4)):
            raise ValueError("The score percussion preset expects bolero in 4/4")
        definitions.append(f"    {{{fields},{int(percussion == 'bolero')}}},")
    header = f"#define PIANO_VOICES {MAX_VOICES}\n" if independent else ""
    if args.compact:
        header += "static const uint32_t piano_increments[] = {" + ",".join(
            str(phase_increment(p)) for p in range(133)) + "};\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header + "\n".join(tables) + "static const PianoScore scores[] = {\n" +
                           "\n".join(definitions) + "\n};\n")


if __name__ == "__main__":
    main()
