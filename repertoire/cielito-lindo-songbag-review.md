# Cielito Lindo Songbag edition audit

6 November 2026

The complete vocal line and both piano staves of the Morones/Wathall version
are available as separate MIDI files and as combined rehearsal MIDI. Both
hands also render through the native Compás piano engine. Full board integration
remains open, so this entry does not yet add an arrangement to the machine's
set list.

## Retained edition

Carl Sandburg, *The American Songbag*, Harcourt, Brace, New York, 1927,
printed pp. 298–299; PDF pages 326–327, counted from one. The scanned copyright
page is PDF page 8. The headnote attributes this version of the text and tune
to Luis Morones. It describes his residence in Mexico and the border states;
that is the book's account of this version, not a claim that Morones composed
the song. The score credits the arrangement to A. G. W.; the book's prefatory
notes identify Alfred G. Wathall among its harmonizers.

- [Retained scan](https://upload.wikimedia.org/wikipedia/commons/c/cb/Carl_Sandburg_-_The_American_Songbag.pdf#page=326)
- SHA-256: `7753977e675f6a2038ae6a6125faad9db5466285c8ec52d5ad4625dbf352b4a6`
- [Canto ledger](cielito-lindo-songbag-canto.json)
- [Piano left-hand ledger](cielito-lindo-songbag-left-hand.json)
- [Piano right-hand ledger](cielito-lindo-songbag-right-hand.json)
- [Separate Rodríguez and Mendoza Okeh recording record](https://frontera.library.ucla.edu/es/recordings/cielito-lindo-117)

The downloaded PDF matches the intake hash. Both score pages were rendered
and all five vocal systems compared against the ledger. The Okeh recording
is a listening reference only; its form, tempo and accompaniment have not
been substituted for the print.

## Musical map

Bar numbers count the opening one-beat pickup as bar 1. There are 31 full
3/4 bars between that pickup and the final two-beat bar: 33 written spans,
96 quarter-note beats, or 2,304 ticks at 24 PPQ.

| Printed page | System | Written spans | Check |
| --- | --- | --- | --- |
| 298 | 1 | 1–7 | G4 pickup; three C5/C5/A4 figures; A4 ties across the bar lines |
| 298 | 2 | 8–13 | D4 held across bars 8–9; B4 and D4 ties retained |
| 298 | 3 | 14–19 | Verse cadence C5 held through bar 17; printed chorus starts at bar 18 |
| 299 | 1 | 20–26 | A4 spans bars 20–21; later C5 continues into bar 25 |
| 299 | 2 | 27–33 | F5 is the top written pitch; final C5 spans bars 32–33 |

The MIDI contains 62 attacks with their written durations. Tied notes remain
single attacks, including A4 for six beats at the chorus and the final C5
for four beats. The conductor track marks VERSE at tick 0 and CHORUS at
tick 1,176. It retains the source URL and scan hash. The note track is separate
from the conductor track and ends at tick 2,304.

108 BPM and a uniform velocity of 82 are preview choices. The print provides
a verbal tempo indication, not that metronome number. This is one written
verse and chorus; additional stanzas and expressive timing are not generated.
No lyrics or recording audio are embedded. The separate canto MIDI contains
no piano notes.

## Piano left hand

The bass staff is encoded as two independent voices over the same 33 written
spans. The lower voice has 30 attacks; the upper has 43, including the eight
F-sharp3 grace notes in the verse. Each grace takes two 24-PPQ ticks from its
following G3, without delaying the lower bass note. That timing is an explicit
preview realization of unmeasured ornamentation.

The upper G3 is held across each same-pitch two-bar connection, then interrupted
by the next grace. Curves between different bass pitches are expressive slurs,
not sustained notes. The chorus moves through F octaves, E-flat octaves, D
octaves and G octaves before its tied C octave. The C2/C3 tie in bars 24–25 and
the G3 tie in bars 30–31 are retained. The final C octave releases after the
first beat of the shortened last bar while the canto C5 continues through
the second beat.

All five bass systems were checked in enlarged crops. The MIDI preserves
nominal pitch and duration; it does not perform the printed accents,
staccato, dynamic shaping or legato touch. The two bass voices and canto have
independent MIDI channels so grace notes and tied tones retain their own
releases. The source and clock must match before the exporter combines them.

A test-only board registry compiles the left-hand voices with the existing piano
engine. It checks the three-beat count-in, one-beat pickup, grace over held
bass, jump into a held canto, recovery of the tied bass octave, hold eligibility,
retained transposition after a jump, and two-beat final bar. These are focused
host checks, not a full-device performance or physical timing qualification.

## Complete piano rehearsal

The treble staff supplies the offbeat chords, short rests and chromatic
movement. The three rolled chords at bars 3, 5 and 7 are split into four voices:
the lowest note begins on beat 2, with the next three notes entering one
24-PPQ tick apart. Every note releases at the printed following beat. The
three-tick spread is an editorial performance choice; the score supplies the
arpeggio sign without a measured spread.

The G-sharp in bar 19 and C-sharp in bar 21 carry through their respective
bars. Bar 23 explicitly returns to F-natural. Bar 27 combines A-sharp3 in
the outer chords with a marked A-natural4 on beat 2. The tied chords across
bars 28–29 and 30–31 remain single attacks. At the ending, both hands release
on the last written beat's rest while the canto continues to the final bar
line. The ledger records the distinction between ties and changing-chord
slurs, plus the nominal treatment of expressive markings.

All five treble systems were compared with enlarged scan crops. The combined
rehearsal has independent channels for the two bass voices, canto, treble
chords and four rolled-chord lines. The board compiler reuses six backing
voice lanes plus its existing guide voice; it does not need a larger voice
pool. A separate native test verifies the staggered roll, recovery of the
tied chord on a bar jump, transposition and all six notes of the final piano
chord. The MIDI tests independently check accidentals, tie durations and
the shared final tick. A mismatched edition hash refuses the combined export.

The complete native auditions last 53.333 seconds at the editorial 108 BPM,
without count-in or gain boost. The with-guide peak is 8,162; the backing-only
peak is 6,691, with no 16-bit rail samples. These use the Compás synthesized
piano tone, not sampled acoustic piano, and are available for listening review.

## Board integration and listening review

The arrangement is registered as score 7, `cielito-lindo-piano`, with both
piano hands and an optional canto guide. `Compas-Cielito-Lindo.html` opens its
prepared rehearsal set directly; Set Builder can place it in any running
order. The existing sixteen-slot factory order remains unchanged.

Performance anchors are printed spans 1, 2, 18 and 32, labelled Pickup, Verse,
Chorus and Ending. These names describe useful entry points, not separately
labelled introductory or coda sections in the source. The three-beat count-in,
partial pickup/final bar, guide/hold/jumps, queued transposition and transitions
between score and groove are exercised in `tests/board-cielito.mjs`. All seven
registered scores also pass twelve-key playback and saved-show recall.

No automatic percussion has been added to this printed arrangement. Any
future optional beat must follow its three-beat meter and be identified as a
Compás arrangement. The full board timing, RAM, stack and mixed-set checks
are recorded in the [board review](../docs/CIELITO-BOARD-REVIEW-20261106.md).
Listening with a singer remains open, as does a separate comparison against
the Okeh recording; that recording is not the source of this piano edition.

## Edition rights record

The catalog records this as a U.S. public-domain edition: a U.S. 1927 print,
beyond the maximum 95-year term described in the Copyright Office's
[Circular 15A](https://www.copyright.gov/circs/circ15a.pdf). This records the
basis for the score encoding, not ownership of later arrangements or
recordings, and is not a worldwide distribution clearance.
