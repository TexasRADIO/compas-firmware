# Meter and tempo in the private Compás development build

The engine and role editor support 6/8 and 12/8 without stretching a 4/4
pattern. The three neutral compound studies remain test fixtures, excluded
from the production rhythm bank. This private development branch adds one
[source-labelled Costa Chica 6/8 accompaniment](CHILENA-COMPOUND-STUDY-20261121.md).
Its notation and regional player review remain pending; no sourced 12/8
accompaniment has been added yet.

| Meter | Steps per two-bar phrase | Display BPM counts | MIDI quarter BPM |
| --- | ---: | --- | ---: |
| 2/4 | 16 | quarter notes | display BPM |
| 4/4 | 32 | quarter notes | display BPM |
| 6/8 | 24 | dotted quarters | display BPM × 1.5 |
| 12/8 | 48 | dotted quarters | display BPM × 1.5 |

Thus 6/8 at 60 has two dotted-quarter beats per bar. Its two-bar phrase lasts
four seconds; MIDI clock runs at quarter-note 90 BPM. At 180 the phrase lasts
4/3 seconds and MIDI clock runs at 270. Echo's note divisions use quarter-note
tempo too. The screen and Sideman report the player's pulse tempo.

The metronome counts the same pulse as the display, one bar before playback.
The hemiola fixture changes the visible grouping from 3+3 eighths to 2+2+2
eighths in its second bar. Its underlying sixteenth clock stays continuous.
It does not establish a stylistic interpretation of any named tradition.

The role editor uses two pages for 24 steps and three for 48. The v2 project
format retains six 48-bit edit masks; old 32-step projects remain importable.
The historic 727 editor retains its 4/4 grid and refuses compound role grids
explicitly. It does not silently truncate steps 33–48.

The four upper-key percussion layers have separate authored compound cells:
per twelve sixteenth steps, conga answers at 4/10, maraca on even steps,
timbal at 10 and bell at 0/6. The cell repeats twice in a 6/8 phrase and four
times in 12/8. These are optional Compás support, not extracted traditional
patterns. The straight-meter family masks are unchanged. Compound masks
never shift a 32-bit word by 32 or more.

`tools/board-compound-emu.py` checks native phrase boundaries to one sample,
count-in clicks, rendered pulse groups, step 48 and the stopped project round
trip. Its `--usb-clock` pass measures the actual outgoing MIDI packets for
all three fixtures at 60 and 180. These tests use an isolated image built
with `COMP_EMU_COMPOUND=1`; that image must never be packaged or installed.
