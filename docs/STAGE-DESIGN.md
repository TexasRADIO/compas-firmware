# Compás stage display

The stage keeps the current song, next song, cue and phrase clock visible.
Yellow identifies the current clave direction; pink identifies a waiting cue.
Speaker cones count beats. Written-score controls include tempo, guide, hold
and semitone shift. The set list displays all sixteen slots.

The opening portrait lasts 650 ms and can be dismissed by any input. A song
change brings back the small television cameo for 900 ms. Transport and cue
indicators remain visible.

The fixed COMPÁS lettering uses Taylor Rushing’s Picnic; RHYTHM SECTION uses
Rodeo. The bitmaps and portrait are cleared for distribution with Compás as
stated in [LICENSING.md](../LICENSING.md). Source font files are not included.
The firmware and browser share the fixed one-bit artwork and renderer.

To regenerate lettering with the appropriate licensed source font:

```sh
swift tools/wordmark.swift /path/to/Picnic.otf 'COMPÁS' lcd
swift tools/wordmark.swift /path/to/Rodeo.otf 'RHYTHM SECTION' subtitle-lcd
```
