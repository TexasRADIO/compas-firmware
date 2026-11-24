# Compás · solo live set and 727 reference

6 November 2026. Private offline candidate.

## 727 voice allocation and balance

Roland's TR-727 owner's manual, printed p. 7, explicitly lists five pairs that
cannot sound together. The July 1985 service notes show the shared waveform
channels and separate quijada and star chime ROMs. The Compás rack and sequence
share ten voice groups. An incoming hit replaces a sounding member of its group
at the trigger frame. Two hits at one frame resolve in trigger order. Quijada and
star chime can overlap. Each has its own voice, even though the TR-727 pattern
display shares one row for them. Same-step data entry and the analog transient
of a cut on original hardware remain separate from this audio rule.
[Roland owner's manual](https://cdn.roland.com/assets/media/pdf/TR-727_OM.pdf)
· [Roland service notes, Table 1 and Table 2](https://manuals.plus/m/1a52d801fe4e02c94383a8101abc71bf4d7c8433ad564e47b55d054820ec2cab.pdf)

| Shared group | Pair |
| --- | --- |
| Bongo | High / low |
| High conga | Muted / open |
| Agogo | High / low |
| Scrape and shake | Cabasa / maracas |
| Whistle | Short / long |

### Common-gain reference

Alessandro Saccoia's 2013 TR-727 archive has 32 mono, 24-bit, 44.1 kHz
WAVs. He reports keeping the input gain fixed across recordings and
leaving their levels unnormalized. All 32 files were verified against
the archived ZIP's CRCs and none reaches a digital rail. The archive
contains normal and accented hits for each sound and an extra short
whistle take in each state. The unaccented high timbale is the loudest
normal hit, peaking at −11.77 dBFS in the capture. The two normal short
whistle takes differ by 0.13 dB. The reference measures the recorder's
hardware and settings. His output routing, fader positions and accent
setting are undocumented.
[Saccoia's account](https://rekkerd.org/alessandro-saccoia-releases-free-tr-727-sample-pack/)
· [Archived WAVs](https://web.archive.org/web/20160404214027id_/http://www.keplero.com/material/tr727/RolandTR727Samples.zip)

Compás retains the Williams/Studio waveforms, bounded trim and end fade.
The normal-hit ratios are applied at build time, with a common −12 dB
scale for the loudest normal voice. Accented pads and accented 727 steps
also apply the measured voice-to-voice differences at trigger time.
The normal and accented hits share the same PCM bank.

In the reference, the accent lift ranges from 7.20 dB for quijada to
11.45 dB for low timbale. Compás subtracts the common 7.20 dB before
applying each voice's correction. HIT and ACC determine the common
lift, so the default ACC setting can bring out the timbales without
raising every percussion hit by the same amount. LEVEL scales both.
The table is fixed-point and adds 30 bytes of lookup data in flash.
The saved rhythm and show formats are unchanged. Preset accompaniment
velocities, percussion overlays and explicit pad velocity commands
retain their existing gain paths.

The original Williams WAVs match the Studio custody hashes. Twelve of
the 15 files each touch a digital rail once. The earlier balance was
reproducible against that digital archive and left the balance between
those 12 hits unresolved. Its measurements remain in a separate ledger.
The source tree carries the gain and custody ledgers. The source WAVs
stay in private custody.
[Williams's capture description](https://cdm.link/free-tr-727-drum-samples-from-the-latin-roland/)
· [Earlier levels](../board/727-levels-williams.json)
· [Current levels and file hashes](../board/727-levels.json)

Reproduce the measurements from the retained ZIP:

```sh
python3 tools/727-common-gain.py /path/to/RolandTR727Samples.zip
```

### First accented target pad check

The strict FM-1 emulator played all 15 pads unaccented and accented.
HIT was 92, ACC was 124, and both rack level and MASTER were 100%.
The complete captures match the generated PCM and per-hit gain,
including tails and silence, after the emulator tap's common output
shift. Values are relative to high timbale within each profile.

| Voice | Normal reference | Normal Compás | Accent reference | Accent Compás |
| --- | ---: | ---: | ---: | ---: |
| High bongo | -4.34 dB | -4.35 dB | -4.48 dB | -4.49 dB |
| Low bongo | -4.97 dB | -4.98 dB | -5.01 dB | -4.97 dB |
| Muted high conga | -4.74 dB | -4.75 dB | -5.31 dB | -5.30 dB |
| Open high conga | -4.24 dB | -4.25 dB | -5.19 dB | -5.17 dB |
| Low conga | -3.17 dB | -3.17 dB | -3.58 dB | -3.56 dB |
| High timbale | 0.00 dB | 0.00 dB | 0.00 dB | 0.00 dB |
| Low timbale | -1.88 dB | -1.88 dB | -0.84 dB | -0.83 dB |
| High agogo | -4.51 dB | -4.51 dB | -5.42 dB | -5.38 dB |
| Low agogo | -4.87 dB | -4.89 dB | -5.84 dB | -5.80 dB |
| Cabasa | -7.29 dB | -7.32 dB | -8.19 dB | -8.17 dB |
| Maracas | -5.65 dB | -5.65 dB | -6.62 dB | -6.62 dB |
| Short whistle | -10.19 dB | -10.18 dB | -11.02 dB | -10.99 dB |
| Long whistle | -9.80 dB | -9.81 dB | -10.76 dB | -10.72 dB |
| Quijada | -6.33 dB | -6.33 dB | -9.54 dB | -9.51 dB |
| Star chime | -8.86 dB | -8.87 dB | -10.90 dB | -10.88 dB |

The largest errors were 0.027 dB normal and 0.046 dB accented.
At the default knob settings, Compás accent lifts ran from
2.60 dB for quijada to 6.83 dB for low timbale.
At ACC 127 and LEVEL 100, the sum of the ten group peak bounds is
16,546 PCM16 counts for the rack alone. The host stress test triggered
accents at this setting alongside the band with zero rail samples.

Both trigger orders of all five shared pairs cut the first hit's tail.
Quijada and star chime summed during overlap. The target counted ten
cuts and two simultaneous rack voices at peak. The 35.148-second
run recorded 12,002 callbacks, a 230.750 µs peak full callback and
a 231.083 µs peak IRQ window.

Storage XIP SHA-256
`2e61563da8d10529a12f8efb720d05c70511dc7db55da2f47edf10c74f78fb06`

The same image passed the 156-second Johnny Canales set and 184-second
rack and storage regression. Full load recorded 63,301 callbacks,
a 888.542 µs peak callback and an 888.833 µs peak IRQ
window plus 10 µs against a 2,901.333 µs deadline. All three runs had
zero missed deadlines, late DMA halves or rail samples. Linked static
RAM was 22,516 bytes. Supervisor stack was 268 bytes at full load and
304 bytes in the tribute. Linked bounds were 356 bytes for audio and
1,688 bytes for foreground C. Chromium passed the tribute transitions,
727 step editor and rhythm and show import round trips.

| Full-load display window | Compute | Transfer | Complete | Pixels |
| --- | ---: | ---: | ---: | ---: |
| R1 Beat | 1.708 ms | 3.107 ms | 4.181 ms | 1,715 |
| R2 First visible change | 2.410 ms | 17.961 ms | 32.179 ms | 13,440 |
| R3 Full redraw | 12.511 ms | 77.266 ms | 105.824 ms | 57,600 |

The columns are independent emulator maxima. R2 starts at debounced
input acceptance. R3 includes 12 MHz transfer and display servicing.

The private WAV is ordered by the 15 normal hits, the five pairs in both
directions, the quijada/chime overlap and the 15 accented hits. The receipt includes each onset
in WAV frames. Reproduce it from the linked storage build and generated
bank:

```sh
python3 tools/board-727-pad-rehearsal.py /path/to/fm1-live /path/to/full-load.json
```

The subsequent offline candidate reproduced the same 30 isolated hit
captures and ten choke results. The latest evidence binds the retest to the
same storage image used for the full tribute and 196-second load.
[Current 727 and live-set evidence](evidence/current-offline-review-20261106.json)

## Roland's Step Write example

On a 4/4 groove, press REC, then SEQ, and turn PRESETS to the last entry,
**ROLAND STEP WRITE**. The 727 editor shows **727 MANUAL**. Use FX to select
**DRUMS ONLY** for the example on its own, or **LAYER** for accompaniment.
Tempo and Feel follow the current groove. Set Feel to zero to hear the
straight sixteenths.

The lesson uses high agogo, high bongo, open high conga and low conga.
The score, display diagram and key-by-key instructions agree on the
onsets. The accent row is empty. Compás repeats the printed bar in both
bars of its editor and uses the corresponding 727 voice for each hit.
The preset is the manual's teaching example. It has no documented
factory pattern number, named genre or tempo. The local source ledger
records the edition and scan hash. The firmware contains event data
only. Rights to the manual and its illustrations remain with their owners.
[Roland manual, printed pp. 12–13](https://cdn.roland.com/assets/media/pdf/TR-727_OM.pdf#page=14) ·
[Event ledger](../repertoire/tr727-step-write.json)

The real-bank host check compared every exported hit and accent bit with
Figure 2 and exercised saved-slot recall for all nineteen presets. The
browser WASM test selected the lesson through the panel, checked the same
grid, played it in Drums Only and checked for silence after STOP.

The 184-second strict board run cycled all nineteen patterns and wrote
and recalled slots in virtual storage. It ran 63,301 callbacks with zero
deadline misses, late halves or rail samples. The peak full callback was
889.21 µs. The peak IRQ window was 889.54 µs plus 10 µs entry/exit allowance
against a 2,901.33 µs deadline. The storage link uses 22,516 B static RAM,
with linked bounds of 356 B audio stack and 1,688 B foreground stack.
The observed supervisor stack watermark was 268 B.

| Display gate | Compute | Transfer | Complete |
| --- | ---: | ---: | ---: |
| R1 beat | 1.74 ms | 2.65 ms | 4.92 ms |
| R2 first visible change | 2.41 ms | 17.96 ms | 29.99 ms |
| R3 full redraw | 12.86 ms | 81.01 ms | 106.53 ms |

R1 sent at most 1,715 pixels. XIP SHA-256 was
`6c07de82bcdbc5bc184739dd093d3e9393e3be8188b4968c63f504883fec5682`.
Reproduce with the rebuilt storage link and
`tools/board-show-rehearsal.py /path/to/fm1-live --store --all-rhythms`.
Set `COMP_REHEARSAL_TMP` to a directory with room for the captures.

## The solo show

Jorge Medina Martínez describes the Mexican keyboardist/singer/MC format and
the R-8's use for cumbia, salsa, bolero, son montuno and merengue. His account
attributes the sub-hour full-set workflow to working musicians. Compás uses it
as a design target. No musician setup-time trial has yet measured that claim
for Compás. Roland's R-8 manuals describe separate pattern and song workflows,
expressive articulation and Feel. Compás adapts them as separate prep and
performance surfaces, playable accents and bounded, repeatable Feel.
[Roland México account](https://articles.roland.com/the-legacy-of-the-r-8-human-rhythm-composer-in-mexico/)
· [Roland R-8 Mk II manual](https://cdn.roland.com/assets/media/pdf/R-8MKII_OM.pdf)

| Job | Control |
| --- | --- |
| Prepare | Open the standalone Set Builder. Choose the groove, title, opening section, tempo, Feel, kit, root, clave where supported, and mix. Duplicate and reorder up to 16 songs. Export the portable JSON. |
| Load for offline rehearsal | Open Play a Prepared Set and choose the exported JSON. The emulator starts on its first song with the imported running order. |
| Run the show | At power-on, Band has the first song ready. PRESETS browses songs while stopped or playing. GLO opens Songs for direct slot keys, and SELECT chooses the save position there. The screen shows the current title and the queued song or part. |
| Handle an extended dance or talk break | A phrase count of 0 holds until cued. A count from 1 to 64 advances to the next occupied slot. Band keys cue Intro, Verse A, Verse B, Chorus, Break and Ending for original grooves. Written scores offer their own form markers. ARP cues a fill on grooves and toggles hold on scores. |
| Refine at rehearsal | Edit the pattern, kit and mix on the panel. In GLO, SAVE captures the current setup into the selected slot while stopped. RAM captures are session-only. The set JSON is the repeatable offline file. |

Transitions can change tempo, root, kit and meter at the downbeat while
transport continues. The next song begins with a fresh phrase. A newer song or part request replaces the pending cue. If a late fill
is still waiting for its phrase, the cue follows that fill. STOP clears
the cue and wins over the command mailbox. The 727 rack can continue sounding across
song boundaries while the previous sequenced lanes clear.

The expanded library adds three original backing studies. Bolero leaves a
singer and requinto room, with sparse bongó and maracas. Son montuno uses
son clave and a bongó-to-campana Lift. Merengue uses a two-beat pulse with
distinct tambora head, rim and güira gestures. The Hybrid 727 does not
rename or replace the merengue tambora and güira. These are accompaniment
studies, informed by traditions rather than sourced song transcriptions.
[UNESCO bolero](https://ich.unesco.org/en/RL/bolero-identity-emotion-and-poetry-turned-into-song-01990)
· [Smithsonian on Arsenio Rodríguez and son montuno](https://www.si.edu/es/object/arsenio-rodriguez-and-transnational-flows-latin-popular-music-david-f-garcia%3Asiris_sil_822719)
· [Dominican merengue field recordings and notes](https://folkways-media.si.edu/docs/folkways/artwork/SFW40547.pdf)

The set editor prepares cue order and starting sound settings. Board step
edits and captured slots do not transfer back into that JSON yet. The board
uses the existing per-pattern Feel and two-level hit accents. R-8-style
recorded rolls, flams and fully editable Feel patches are outside this build.

## Priorities from the first hardware audition

A solo player should be able to start with a playable rhythm, make it fit
their band or instrument, and use it in a set. The first hardware audition
showed that the screen needs to distinguish those jobs. Home uses **Song
Parts** for cues, **Layers** for accompaniment switches and **Drums** for
hand-played hits. A written score should identify its accompaniment before
the player presses PLAY. A score with added percussion should credit the
score and the added arrangement separately.

The 727 now has an independent fifteen-voice step grid, entered with SEQ,
and a Drums Only audition mode. Its edits are session-only. The next work is
a saved rhythm bank: PRESETS on that future bank should audition loops without
changing the selected song, and SEQ should edit the chosen loop. Retain phrase cues on Home and use a single bar line
for a live change of rhythm. A player should be able to save their version,
put it into the set, and recover that same version after a power cycle.

Build the bank in local families. Start with coastal, Mexican electronic and
Tejano cumbia, salsa in both son clave directions, bongó and bell forms of son
montuno, singer-first bolero, and merengue and pambiche. Preserve the source
meter, phrasing, articulation and instrument roles. Add variants when they
change what the player can do, such as an open bell passage or a sparser
verse. If a role depends on a güira, tambora or clave, use a suitable voice
and document any 727 substitution. Keep three kinds of entries visible in
the library: documented machine patterns, transcribed performances or
scores, and original Compás arrangements. Each entry needs an exact source,
rights status, tempo and meter, and a note on adaptations.

For the next rehearsal, give a singer-keyboardist the machine with no
control walkthrough. Ask them to find a familiar rhythm, start it, enter a
fill, change section while singing, and switch to a second song. Ask them
to make and save one rhythm edit. Time a complete ten-song set preparation
and check recall after power cycling. Have players from the relevant styles
listen to the pattern in context and record what they would change. These
are proposed acceptance checks. The emulator measures playback and timing,
and the rehearsal measures whether a musician can use the instrument.

## Offline board measurements

The portrait-bearing image was exercised under strict fm1-emu v0.4.0
with 128-frame I2S halves at 44.1 kHz. TIMER4 measures the ALNK0 C callback
from entry through interrupt acknowledgement and the DMA ownership check.
The assembly wrapper and trailing statistics updates sit outside that
timer bracket.

| Run | Callbacks | Worst callback | Half-buffer budget | Peak PCM voices | Supervisor stack watermark |
| --- | ---: | ---: | ---: | ---: | ---: |
| Set navigation and performance | 1,118 | 291.17 µs | 2,901.33 µs | 2 | 272 B |
| Overlapping rack hits, sequence and live cues | 1,117 | 332.38 µs | 2,901.33 µs | 10 | 272 B |

Both runs recorded zero late DMA halves, deadline misses and audio rails.
The linked normal image has 569,364 bytes of XIP text and assets and
12,784 bytes of static RAM, including both DMA halves and the 64-byte crash
record. The linked call-frame calculation bounds the normal audio path
at 316 bytes including the software IRQ entry. The native 35,000-callback
stress run also reached ten simultaneous voices and recorded zero rails.
Fault injection verified that late halves and deadline misses shut off
output. These measurements describe the modeled board and host. Hardware
timing, codec output and full-concert soak remain unmeasured.
