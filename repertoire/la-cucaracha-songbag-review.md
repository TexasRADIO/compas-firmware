# La Cucaracha in the 1927 American Songbag

Carl Sandburg, *The American Songbag*, Harcourt, Brace, New York, 1927.
The music is on printed p. 290, PDF page 318. The page credits the
arrangement to A. G. W., Alfred George Wathall. Sandburg's notes on p. 289
give several transmission leads. The transcription follows the printed
version. [Score scan](https://upload.wikimedia.org/wikipedia/commons/c/cb/Carl_Sandburg_-_The_American_Songbag.pdf#page=318)
· [Morgan Library catalog](https://www.themorgan.org/music-manuscripts-and-printed-music/310781)

The retained scan SHA-256 is
`7753977e675f6a2038ae6a6125faad9db5466285c8ec52d5ad4625dbf352b4a6`.
The 1927 New York print is in the U.S. public domain. The MIDI encodes
the written notes and keeps the canto and piano staves on separate tracks.
[U.S. Copyright Office, Circular 15A](https://www.copyright.gov/circs/circ15a.pdf)

| System | Written spans | Score details |
| --- | --- | --- |
| 1 | 1–4 | Verse starts on the first downbeat. Rolled piano chords sustain through beat three. |
| 2 | 5–8 | Verse continues over the dominant. Span 8 ends on a fermata after a quarter and eighth. |
| 3 | 9–12 | Span 9 contains the three eighth-note chorus pickup. The chromatic C-sharps at the end of span 12 lead into D. |
| 4 | 13–17 | The chorus resolves to C. At the last bar's second beat the low half notes and both upper chords overlap. |

The three ledgers share 17 spans and 1,152 ticks at 24 PPQ. Spans 8 and 9
are each 36 ticks. Rolls enter low to high one tick apart and release
together. Fermatas and staccato play at their nominal durations. Compás
starts at an editorial 112 BPM and adds a three-beat count-in from STOP.
Start and Verse cue the opening, Chorus cues span 9, and Coda cues the
last bar. The default rehearsal slot enables the optional canto guide.
The second slot leaves the vocal part to the singer. The arrangement
has no automatic percussion.

[Canto](la-cucaracha-songbag-canto.json)
· [Piano bass staff](la-cucaracha-songbag-left-hand.json)
· [Piano treble staff](la-cucaracha-songbag-right-hand.json)
· [Rehearsal set](cucaracha-rehearsal-set.json)

The strict offline run reached the last bar with the guide and eight
manual 727 pads active. Over 10,848 callbacks, the peak callback was
1,053.17 µs and the measured IRQ window peaked at 1,053.50 µs. The latter
plus a 10 µs allowance fits the 2,901.33 µs deadline. No deadline misses,
late halves or clipped samples were recorded. The storage build used
22,516 bytes of static RAM and had linked stack bounds of 356 bytes for
audio and 1,688 bytes for foreground C. The emulator's supervisor stack
watermark was 256 bytes. The linked image SHA-256 was
`8da1592dca3221183e575bc5f40820428a5726107783e7044181a1be22d3e402`.
Browser WASM also exercised the opening, chorus pickup, return to verse
and coda. Listening review remains open.

The regular build passed the separate 138-second factory rehearsal.
It reached all ten rehearsal slots over 47,453 callbacks. The measured
IRQ peak was 627.00 µs with zero deadline misses, late halves or clipped
samples. The linked image SHA-256 was
`cc8ab6f8d544fb6f0808f902ef05518ba1fa7344bde101eec771a64833b497d9`.
