# North-coast Peru: two private live accompaniments

This separate development branch extends the qualified 34-study RC3 bank to
36. It does not replace the RC3 install candidate, change the sixteen-slot
factory set, install a unit or publish a new bank. Earlier public study IDs
and musical data remain unchanged. The private compound fixtures follow the
36 ordinary studies and remain excluded from normal builds.

## Musical basis and limits

The source is PER-01, Burgos Segura and Ñopo Olazábal's 2023 ANTEC research,
[university PDF](https://repositorio.unm.edu.pe/server/api/core/bitstreams/1d131676-94ec-4669-8820-acfc1948a4f5/content),
SHA-256 `d76287573e894edacb4c9aa48f5f27f93481e0bb0ed53aaf9073c1a973bc04cd`.
Figure 14, printed p. 161 / PDF p. 20, supplies the inspected ensemble
examples. Its left panel refers to *Motor y motivo* / Grupo 5, its right to
*Dios mío, haz que me enamore* / Armonía 10. The latter fragments the shell
while upper percussion continues. Source half-note pulses are 98 and 140.

The machine retains adopted percussion onset relationships. It composes bass
pitches, mix levels, dynamics, fills and live form. Two 2/2 source bars become
two 2/4 machine bars at the same audible pulse; no swing is imposed. A source
quarter becomes a machine eighth. Full bass lines, piano voicings and lead
melodies are not transcribed or claimed. This is one-reader source work,
pending independent notation and regional player comparison.

The [ledger](../repertoire/peru-north-development-ledger.json) separates every
adopted percussion role, articulation loss and authorial choice. These are
north-coast case studies, separate from chicha, tecnocumbia and later digital
cumbia. They do not define a national rhythm.

## Playing

Build the ordinary real-bank browser player and open
[`Compas-Peru-North-Review.html`](../build/Compas-Peru-North-Review.html).
The same two entries are available as **North-coast Peru** in Rehearsal sets,
or through [the review set](../repertoire/peru-north-development-set.json).
The initial mixes are authored starting points. Each song holds until the
player cues a new part or song; no automatic concert form is imposed.

| Part key | Audible machine arrangement |
| --- | --- |
| Intro | Hat and bass in the first bar; percussion opens in the second, then Verso |
| Verso | Adopted percussion body with an authored bass reduction |
| Coro | Base first bar, Lift accents in the second |
| Puente | Lift accents across both bars |
| Break | Hat and bass leave space for a human call or lead |
| Final | One reduced phrase, then the existing stop/next-song behaviour |

These are the existing Cumbia-family screen labels for the six conductor
slots, not a claim to reproduce either recording's section names or order.
ALGORITHM still selects Base/Lift/Break. ARP arms the separately authored
last-beat fill; a part key chooses the landing. None of these gestures claims
to reproduce the original song form. The melody and the performer’s phrasing
remain live. The score's source piano/brass/guitar has no hidden synth lane.

## Palette compromises

With the review set's 727 kit on, bongó strokes share one high-bongó sample,
so their high/low/palm distinctions are reduced. The conga maps open strokes
to the board's accented open-conga event; the stronger level is an authored
consequence, not a source velocity reading. Palm and slap collapse.

Kick, Hat, Shell and Bass use Compás procedural voices. The source jam block
is omitted. There is no claim of an acoustic hat emulation or of a 727 on
either source recording. Turn the kit off to compare the entirely procedural
palette; the onset relationships remain the same.

The displayed English **HAT** and Spanish **HI-HAT** retain the existing
translation. The new **SHELL / CASCO** label identifies the timbal-shell role
in both page languages. All displayed text passes the size/font gate.

## Verification and review

```sh
node --test tests/peru-north-development.test.mjs
node tools/peru-north-review.mjs
COMP_STUDIO=/path/to/Studio sh tools/board-host.sh
```

The focused rehearsal covers 48 part/language/palette renders, six Energy
auditions, newest-cue/phrase landing and portable-show byte round-trips.
It also renders two complete scripted conductor performances, including a
fill into Final. The [player worksheet](PERU-NORTH-PLAYER-REVIEW.md) records
the remaining musical questions.
English and Spanish audio must be identical. Intro must enter Verso and
Final must stop. The ordinary source tests also pin the previous 34 studies
and check the compressed compiled masks for the new upper and shell cells.

Host audio renders are functional evidence; they do not establish FM-1 CPU
timing or a musician's verdict. Native flash, stack/RAM and timed playback
receipts must accompany any later candidate promotion. The 34-study RC3
receipts do not automatically qualify this 36-study branch.

`tools/peru-baseline-audio.mjs` compares all 34 earlier studies against the
pinned qualified browser engine: three Energy states, both palettes, complete
two-bar phrases and exact stereo PCM bytes. Its receipt distinguishes channel
samples from stereo frames.

Listen for the continuing upper role against the sparse cumbión shell, whether
the source pulse is natural to count, whether the authored bass leaves useful
space, and whether the 727 articulation reductions mislead. Record objections
before choosing release names, palette defaults or further embellishments.
