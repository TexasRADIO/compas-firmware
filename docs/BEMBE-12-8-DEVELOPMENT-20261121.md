# A playable 12/8 bell/block study

Private development, 21 November 2026. This is a separate branch from Will's
frozen 37-study Monkey Test candidate. No lab assets or physical unit change.

Csabi Pusztai's [MEINL artist lesson](https://meinlpercussion.com/en/blog/csabi_pusztai)
offers three one-page scores. Version 1, bar 1, gives the seven-stroke cowbell
and a wood-block stroke on each dotted quarter. Version 2, bar 1, doubles the
block on the first two eighths of each dotted quarter. Both are visually read
and retained with hashes in the local research folder. The downloadable third
score has only whole-bar block rests, despite the web description; its missing
left-hand pattern is not reconstructed. This is one reader's notation reduction.

The [ledger](../repertoire/bembe-source-ledger.json) records exact score URLs,
hashes, source locations, adoption and exclusions. The UVic MUS 320 guide
independently corroborates the bell's cyclic intervals; its separate Ghanaian
examples are not turned into a Cuban ensemble or historical-origin claim.

| Role | Zero-based eighth-note positions within one 12/8 bar |
| --- | --- |
| Bell, versions 1 and 2 | 0, 2, 4, 5, 7, 9, 11 |
| Block, version 1 | 0, 3, 6, 9 |
| Block, version 2 | 0, 1, 3, 4, 6, 7, 9, 10 |

One machine phrase repeats the chosen source bar twice: 48 sixteenths,
eight dotted-quarter pulses. **96 BPM means dotted quarters**, not quarter
notes. The two-bar phrase lasts five seconds; outgoing quarter-note MIDI clock
would be 144 BPM. No tempo is printed in the adopted source bars; 96 is chosen
for rehearsal. Feel starts at zero to keep the written relationships clear.

Base uses version 1. Lift adds the second eighth of each block group, producing
version 2's first-bar block cell. The bell stays fixed. Compás's generic form
exposes bell alone in Intro's first bar, then opens the block; Verse B opens the
doubles in bar two; Chorus uses them throughout. Break and Ending expose bell
alone. ARP introduces an authored last-beat block reply into the next part.
This is a live rehearsal arrangement, not either lesson's complete order.

The machine's procedural bell and stick give distinguishable attack cues.
They are not the MEINL instruments' recordings. Initial levels are Bell 76 and
Block 58; all other lanes are silent. No electric bass, conga ensemble or
religious function is inferred from a two-hand independence exercise. Manual
drums and the existing compound support remain optional machine features.

Build with `COMP_STUDIO` and `COMP_HANAN` pointing to the retained real inputs,
then run `python3 tools/board-browser.py` and open **Compas-Bembe-Review.html**.
The 48-step role editor has three complete 16-cell pages. The historic 727
32-step editor is not silently stretched over this accompaniment.

`node tools/bembe-review.mjs` renders six parts in both languages at 60, 96 and
180 dotted-quarter BPM, visits all 48 steps and performs three complete live
cue/fill/ending/save journeys. Its WAVs are private listening material and its
receipt is browser-board evidence, not a chip timing result or musician review.
Both PI32 language builds pass the new study's combined-load and display checks,
and the complete 38-study target sweep passes. Exact scopes are recorded below.

This entry is explicitly a named artist's Bembé teaching reference. It does
not claim to provide a complete regional Bembé ensemble, sacred repertoire,
Mixteca yaa sii, or one interchangeable rhythm for all 12/8 music.

## Measured development status

Native C source is `4ff3841`; later changes extend the retained-bank comparison
tool and add documentation/evidence. Both real-bank language apps are 562,484
bytes, leaving 14,764 bytes: +324 bytes relative to Will's 37-study build.
Static RAM stays 24,988 bytes excluding the effects pool, 187,228 including it.
Linked conservative stack bounds stay 436 bytes audio plus IRQ and 2,400 bytes
foreground C. The pattern pool uses 219 production plus 14 fixture row IDs,
of 255 possible nonzero IDs; silence is row zero.

The new study at dotted-quarter 180 BPM, Feel 100, maximum effects, Kumbia,
ten manual 727 voices, four compound layers and USB clock/Sideman passes the
slow/home sample-clock load corners and FRENO stop in both languages.
Peak IRQ is 1,610.792 µs EN / 1,651.333 µs ES against a 2,901.333 µs deadline;
there are no misses, late halves, clipping or effect shedding. Clock output
tracks 270 quarter-note BPM / 108 MIDI clocks per second at this compound tempo.
These are strict-emulator measurements, not physical FM-1 acceptance.

R1/R2/R3 pass on Band, Mixer, Effects and Kumbia in both languages with effects
and USB clock active. The raw receipts separate pixel compute, LCD-service and
complete time. LCD-service includes interrupt/scheduling overhead; it is not
pure wire time. Every completed full screen transfers exactly 57,600 pixels.

`make test` passes, including 80 Node tests and native memory checks; the 56
EN/56 ES golden frames and 42 stage screens remain unchanged. All 37 retained
studies compare sample-for-sample across three Energy states and both palettes:
222 runs / 37,623,552 stereo frames. The compound comparison covers the actual
24-step Chilena phrase rather than assuming every meter is 32 steps.

The real Studio bank host harness passes timing, instrumented-stack and
ASan/UBSan modes, each 266,000 callbacks / 34,048,000 stereo frames, all 38
study bits set. The host stack probe observes 6,368 bytes; that is distinct
from the linked PI32 stack bounds above. The native English count-in's four
numerals and beat cones agree in eight mid-beat pixel checks at 60/180 BPM.

The [machine-readable receipts](evidence/bembe-20261121/) retain exact image,
WASM, input and source-ledger hashes. Full-bank target coverage completed in a
separate test-only image: 829,892 ms /285,908 callbacks, every one of 38 studies
and all six parts, peak IRQ 1,723.333 µs, zero missed deadlines, late halves,
clipping, refused commands or effects shedding. Minimum playing observations
per study is 5,615; the new 12/8 study has 6,075 and all six part bits set.

Offline **1.3-rc.3-patterns.2** EN/ES FWSCs are now built from the retained
production XIPs. Both retain the exact BB1 r85 loader/head and pass the package
guards, including deliberately unsafe-package refusal. Actual packages boot,
PLAY/STOP and answer USB identity on both virtual P25Q80 and UC25HQ80 chips.
The unchanged update envelope clears saved shows; export before a later update.
These packages stay local. Will's fixed 37-study protected review, public site,
tags and physical device remain unchanged. This is software qualification for
review, not a claim of regional endorsement or physical acceptance.
