"""Export source transcriptions as Standard MIDI, without quantizing triplets."""
import argparse
import json
from pathlib import Path
import re
import struct

PITCH = r"[A-G][#b]?[0-8]"
NOTE = re.compile(rf"(?P<name>R|{PITCH}(?:\+{PITCH})*):(?P<duration>[1-9][0-9]*)(?P<tie>~?)$")
NATURALS = dict(zip("CDEFGAB", (0, 2, 4, 5, 7, 9, 11)))


def vlq(value):
    out = [value & 127]
    while value >= 128:
        value >>= 7
        out.append(128 | (value & 127))
    return bytes(reversed(out))


def bars(score):
    if "play_order" in score:
        return [
            (score["patterns"][name], score["partial_ticks"].get(name))
            for section in score["play_order"]
            for name in score["sections"][section]["bars"].split()
        ]
    if "systems" not in score:
        return [(bar["notes"],bar["ticks"]) if isinstance(bar, dict) else (bar,None)
                for bar in score["bars"]]
    return [
        (score["patterns"][name], {"pickup": 12, "ending": 36}.get(name))
        for system in score["systems"] for name in system["bars"].split()
    ]


def midi_pitch(name):
    return (int(name[-1]) + 1) * 12 + NATURALS[name[0]] + {"": 0, "#": 1, "b": -1}[name[1:-1]]


def notes(score):
    ppq = score["ppq"]
    beats, denominator = score["meter"]
    span = ppq * beats * 4 // denominator
    tick, tied = 0, []
    result = []
    for number, (bar, partial) in enumerate(bars(score), 1):
        start = tick
        for token in bar.split():
            match = NOTE.fullmatch(token)
            if not match:
                raise ValueError(f"Bar {number}: invalid token {token}")
            name, duration, tie = match.groups()
            duration = int(duration)
            pitches = [] if name == "R" else sorted(midi_pitch(n) for n in name.split("+"))
            if len(set(pitches)) != len(pitches):
                raise ValueError(f"Bar {number}: duplicate pitch in chord")
            if tied:
                if [note[1] for note in tied] != pitches:
                    raise ValueError(f"Bar {number}: tie changed pitches")
                attacks = tied
                for note in attacks:
                    note[2] += duration
            else:
                attacks = [[tick, pitch, duration] for pitch in pitches]
                result.extend(attacks)
            if tie and not pitches:
                raise ValueError(f"Bar {number}: rest cannot join a tie")
            tied = attacks if tie else []
            tick += duration
        expected = span if partial is None else partial
        if tick - start != expected:
            raise ValueError(f"Bar {number}: expected {expected} ticks, found {tick-start}")
    if tied:
        raise ValueError("Unclosed final tie")
    return result, tick


def text_event(kind, text):
    data = text.encode("ascii", errors="replace")
    return bytes((255, kind)) + vlq(len(data)) + data


def clock_events(score):
    tempo = round(60_000_000 / score["preview_bpm"])
    beats, denominator = score["meter"]
    if denominator not in (2, 4, 8, 16):
        raise ValueError("Unsupported time signature denominator")
    click = 36 if (beats, denominator) == (6, 8) else 24
    return (b"\0\xff\x51\x03" + tempo.to_bytes(3, "big") +
            b"\0\xff\x58\x04" + bytes((beats, denominator.bit_length()-1, click, 8)))


def append_notes(track, events, end, channel=0, velocity_marks=()):
    last_tick = -1
    for tick, velocity in velocity_marks:
        if (type(tick) is not int or not last_tick < tick < end or
                type(velocity) is not int or not 1 <= velocity <= 127):
            raise ValueError("MIDI velocity marks must use increasing ticks and velocities 1–127")
        last_tick = tick
    marks = iter(velocity_marks)
    next_mark = next(marks, None)
    messages = []
    velocity = 82
    for start, pitch, duration in events:
        while next_mark is not None and start >= next_mark[0]:
            velocity = next_mark[1]
            next_mark = next(marks, None)
        messages.extend(((start, 0x90, pitch, velocity), (start+duration, 0x80, pitch, 0)))
    elapsed = 0
    for tick, status, pitch, velocity in sorted(messages):
        track += vlq(tick-elapsed) + bytes((status|channel, pitch, velocity))
        elapsed = tick
    track += vlq(end-elapsed) + b"\xff\x2f\0"


def smf(format_number, ppq, tracks):
    header = b"MThd" + struct.pack(">IHHH", 6, format_number, len(tracks), ppq)
    return header + b"".join(b"MTrk" + struct.pack(">I", len(track)) + track for track in tracks)


def midi(score):
    if "parts" in score:
        return score_midi(score)
    track = bytearray(clock_events(score) + b"\0" + text_event(3, score["title"]))
    track += b"\0\xc0\0"  # GM acoustic grand, a neutral transcription preview
    events, end = notes(score)
    append_notes(track, events, end)
    return smf(0, score["ppq"], [track])


def score_conductor(title, score, spans, end):
    conductor = bytearray(clock_events(score) + b"\0" + text_event(3, title))
    conductor += b"\0" + text_event(1, "Source: " + score["source"])
    conductor += b"\0" + text_event(1, "Source scan SHA-256: " + score["source_sha256"])
    if "attribution" in score:
        credit = score["attribution"]
        for value in (credit["author"], credit["source_page"], credit["license"],
                      credit["license_url"], credit["changes"]):
            conductor += b"\0" + text_event(1, value)
    ppq = score["ppq"]
    beats, denominator = score["meter"]
    full_bar = ppq * beats * 4 // denominator
    starts, tick = [], 0
    for span in spans:
        starts.append(tick)
        tick += full_bar if span is None else span
    elapsed, previous_bar = 0, 0
    for section in score["form"]:
        bar = section["bar"]
        if not previous_bar < bar <= len(starts):
            raise ValueError("Score markers must be ordered written bar positions")
        tick = starts[bar-1]
        conductor += vlq(tick-elapsed) + text_event(6, section["name"])
        elapsed, previous_bar = tick, bar
    conductor += vlq(end-elapsed) + b"\xff\x2f\0"
    return conductor


def score_parts(score):
    spans = None
    voices = []
    beats, denominator = score["meter"]
    full_bar = score["ppq"] * beats * 4 // denominator
    for part in score["parts"]:
        voice = dict(part, ppq=score["ppq"], meter=score["meter"])
        if "sections" in part:
            voice.update(play_order=score["play_order"], partial_ticks=part.get("partial_ticks", {}))
        events, _ = notes(voice)
        part_spans = [full_bar if span is None else span for _, span in bars(voice)]
        if not part_spans or (spans is not None and part_spans != spans):
            raise ValueError("Score parts must share every bar line and the final tick")
        spans = part_spans
        voices.append(events)
    if spans is None:
        raise ValueError("A score needs at least one part")
    return voices, spans


def score_midi(score):
    # Each written voice has its own channel so unisons can end independently.
    channels = [channel for channel in range(16) if channel != 9]
    parts = score["parts"]
    if not 1 <= len(parts) <= len(channels):
        raise ValueError("A score needs 1–15 pitched MIDI parts")
    voices, spans = score_parts(score)
    end = sum(spans)
    tracks = []
    for part, channel, events in zip(parts, channels, voices):
        program = part["program"]
        if type(program) is not int or not 0 <= program <= 127:
            raise ValueError("Invalid MIDI program")
        track = bytearray(b"\0" + text_event(3, part["title"]) + bytes((0, 0xc0 | channel, program)))
        append_notes(track, events, end, channel, part.get("velocity_marks", ()))
        tracks.append(track)
    conductor = score_conductor(score["title"], score, spans, end)
    return smf(1, score["ppq"], [conductor, *tracks])


def check_source_clock(backing, part):
    clock = ("ppq", "meter", "preview_bpm")
    if any(backing[key] != part[key] for key in clock):
        raise ValueError("Rehearsal tracks must share PPQ, meter and preview tempo")
    if any(backing[key] != part[key] for key in ("source", "source_sha256")):
        raise ValueError("Rehearsal tracks must come from the same retained score")


def aligned_notes(backing, part):
    check_source_clock(backing, part)
    if [partial for _, partial in bars(backing)] != [partial for _, partial in bars(part)]:
        raise ValueError("Rehearsal tracks must share every bar line and the final tick")
    return notes(part)[0]


def aligned_right_hand(backing, right_hand):
    if "parts" not in right_hand and "parts" not in backing:
        return [("Piano right hand", 0, aligned_notes(backing, right_hand))]
    check_source_clock(backing, right_hand)
    if "play_order" in right_hand and backing.get("play_order") != right_hand["play_order"]:
        raise ValueError("Rehearsal tracks must share the written play order")
    full_bar = backing["ppq"] * backing["meter"][0] * 4 // backing["meter"][1]
    expected = (score_parts(backing)[1] if "parts" in backing else
                [full_bar if partial is None else partial for _, partial in bars(backing)])
    if "parts" in right_hand:
        voices, spans = score_parts(right_hand)
        parts = [(part["title"], 0, events) for part, events in zip(right_hand["parts"], voices)]
    else:
        spans = [full_bar if partial is None else partial for _, partial in bars(right_hand)]
        parts = [("Piano right hand", 0, notes(right_hand)[0])]
    if spans != expected:
        raise ValueError("Rehearsal tracks must share every bar line and the final tick")
    return parts


def rehearsal_midi(title, backing, guide=None, right_hand=None, *, extra=None):
    if "parts" in backing:
        voices, spans = score_parts(backing)
        end = sum(spans)
        parts = [(part["title"], part["program"], voice)
                 for part, voice in zip(backing["parts"], voices)]
    else:
        back_notes, end = notes(backing)
        spans = [partial for _, partial in bars(backing)]
        parts = [("Piano left hand", 0, back_notes)]
    full_bar = backing["ppq"] * backing["meter"][0] * 4 // backing["meter"][1]
    expected_spans = [full_bar if span is None else span for span in spans]
    if guide is not None:
        check_source_clock(backing, guide)
        if "parts" in guide:
            guide_voices, guide_spans = score_parts(guide)
        else:
            guide_voices = [notes(guide)[0]]
            guide_spans = [full_bar if span is None else span for _, span in bars(guide)]
        if guide_spans != expected_spans:
            raise ValueError("Rehearsal tracks must share every bar line and the final tick")
        if len(guide_voices) != 1:
            raise ValueError("Rehearsal canto must have one written voice")
        parts.append(("Canto guide", 53, guide_voices[0]))
    conductor = score_conductor(title, backing, spans, end)
    if right_hand is not None:
        parts.extend(aligned_right_hand(backing, right_hand))
    if extra is not None:
        check_source_clock(backing, extra)
        if extra.get("play_order") != backing.get("play_order"):
            raise ValueError("Rehearsal tracks must share the written play order")
        extra_voices, extra_spans = score_parts(extra)
        if extra_spans != expected_spans:
            raise ValueError("Rehearsal tracks must share every bar line and the final tick")
        parts.extend((part["title"], part["program"], voice)
                     for part, voice in zip(extra["parts"], extra_voices))
    channels = [channel for channel in range(16) if channel != 9]
    if len(parts) > len(channels):
        raise ValueError("A rehearsal needs at most 15 pitched MIDI parts")
    tracks = [conductor]
    for channel, (name, program, events) in zip(channels, parts):
        if type(program) is not int or not 0 <= program <= 127:
            raise ValueError("Invalid MIDI program")
        track = bytearray(b"\0" + text_event(3, name) + bytes((0, 0xc0 | channel, program)))
        append_notes(track, events, end, channel)
        tracks.append(track)
    return smf(1, backing["ppq"], tracks)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--right-hand", type=Path, help="add a matching upper staff to the source")
    parser.add_argument("--title", help="title for a combined piano rehearsal")
    args = parser.parse_args()
    score = json.loads(args.source.read_text())
    if args.right_hand:
        right_hand = json.loads(args.right_hand.read_text())
        args.destination.write_bytes(rehearsal_midi(args.title or score["title"], score, right_hand=right_hand))
        print("Combined piano rehearsal MIDI")
        raise SystemExit(0)
    args.destination.write_bytes(midi(score))
    if "parts" in score:
        print(f"{len(score['parts'])} independently timed MIDI parts")
    else:
        events, ticks = notes(score)
        print(f"{len(bars(score))} measures, {len(events)} note attacks, {ticks} ticks")
