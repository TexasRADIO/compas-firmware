# Compás stage display

The current Sonidero stage separates the song sign, next song, cue, phrase
clock and page controls. On the Salsa/Son Percussion page, the current clave
direction sits above the faders in yellow, with a pending direction in pink.
The OCT−/OCT+ assignments remain visible below the faders. Speaker cones count beats and turn pink toward a
waiting cue. On written scores, Band's four footer columns are Tempo, Guide, Hold and SHIFT.
SHIFT displays a signed semitone offset; a waiting change shows the current
value and pink NEXT value on separate lines, landing at the next written bar.
The set list fits all sixteen slots on one screen, keeping NOW and NEXT
visible when they are far apart. A pending cue paints its row first, then
clears the superseded cue and updates the SAVE cursor. See the
[Sonidero control map and redraw design](CONTROL-DESIGN.md).

Johnny opens the show for 650 ms once the splash is drawn. An input dismisses
it and executes normally. The live song hand-off brings him back in a pink
television at the end of the title sign. The transport and cue stay visible.
The browser board bench uses the same renderer. Both the factory and
prepared-rehearsal pages leave the opening after 650 ms, including when
audio is still disabled. Enabling audio during the opening brings the
panel forward immediately. The small TV cameo clears after 900 ms on the
display clock. A timed idle redraw clears it before audio is enabled.
Browsing the standalone rhythm bank does not trigger a song cameo.
The older rhythm bench uses its own brief cameo when the ensemble changes.

A new song, part, Energy, rhythm, key or clave cue interrupts a decorative
sequence. Promoting a queued part to the next bar also interrupts it.
When Lift's animation ends, all marquee bulbs return to their normal
colours. The musical Energy remains Lift while a key change waits.
[Host, browser and strict FM-1 verification](evidence/stage-cue-priority-20261106.json)

The splash uses Taylor Rushing's Picnic for COMPÁS and Rodeo for RHYTHM SECTION.
Both use the retained Big Beat brand assets. The Picnic outline has a drawn
acute accent. The board and browser render fixed one-bit lettering through
the same stage renderer. Rodeo takes 360 bytes of bitmap data and uses the
existing LCD stripe. The editable fonts stay in the private source collection.

- Picnic source SHA256 `d05d16a4bd9054e119234880972f943c459fd5a2af1f49db709055996fc9c1f2`
- Rodeo source SHA256 `9be86aa3174b86530b7f4c16845f5384f41e1e25e092072812d572029c97596e`

To regenerate either bitmap, use the hash-matched font with the existing Swift
generator. The output goes to stdout.

```sh
swift tools/wordmark.swift /path/to/Picnic.otf 'COMPÁS' lcd
swift tools/wordmark.swift /path/to/Rodeo.otf 'RHYTHM SECTION' subtitle-lcd
```

The 240 × 240 host splash and browser WASM splash match at every pixel. The
strict FM-1 emulator capture at 650 ms shows both marks and the portrait.

### Lettering provenance and release scope

The two source files match the owner-supplied Big Beat brand collection.
Their OpenType name records identify Picnic Filled Regular and Rodeo Display
Regular, both version 1.001. Both source files regenerated the checked-in
one-bit arrays byte for byte with the commands above. The combined payload
is 659 bytes. The Johnny Canales offline review ZIP contains the two fixed
marks and no OTF, TTF or WOFF files.

The retained brand provenance records the owner's direction to use these
assets. The inspected records contain no purchase agreement or written
grant specifying public Compás firmware and browser distribution. The
OpenType files have no licence description or licence URL. Their embedding
flag is zero, which describes a technical setting and supplies no release
permission. The Rodeo Display shop listing identifies the face and offers
distinct Desktop, Webfont and App licences. The storefront and its general
terms establish what it offers today. The agreement that accompanied each
exact file would establish the applicable terms. No source-font embedding
is needed for the current splash.

For a public release decision, record permission for the two fixed splash
images in the downloadable firmware, browser build and source archive.
A reusable glyph atlas or embedded font file would require its own review.
The retained approval for a BB-Bob glyph subset is specific to that
project.

[Rushing’s Rodeo Display listing](https://creativemarket.com/TWRUSHING/292301136-RODEO-DISPLAY)
· [Creative Market’s font terms](https://creativemarket.com/licenses/terms/fonts)

The original identity references include RCA-era Puerto Rican Menudo, Selena's late-1980s
South Texas stage photos, and Flans's 1985 Mexican pop debut. The earlier screen palette drew on record sleeves and TV color bars.
The current song titles use the Sonidero sign face. Artist photographs and marks are reference material.
[Menudo album sleeve, People's Graphic Design Archive](https://peoplesgdarchive.org/item/7844/menudo-album-cover)
· [RCA's 1983 trade ad for the U.S., Puerto Rico and Mexico release](https://www.worldradiohistory.com/Archive-All-Music/Billboard/80s/1983/BB-1983-11-19.pdf)
· [Smithsonian Selena collection](https://www.si.edu/spotlight/selena-quintanilla-perez)
· [Flans's 1985 debut](https://music.apple.com/us/album/flans/1443501998)
