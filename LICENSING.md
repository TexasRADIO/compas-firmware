# Licensing

## Compás

Compás is free software: GNU General Public License, version 3 only
(`GPL-3.0-only`). The full text is in `LICENSE`. There is no warranty.

Compás is a Felucca-family firmware. It builds on the pinned Felucca board
support (register definitions, IRQ entry, LCD driver and link script), which is
itself GPL-3.0-only; `board/BSP.json` pins each of those files and the licence
by SHA-256. The upstream Felucca revision is
`ef1f561b6b105f20350f6b1eac0dc0789a357f00`. Felucca-derived files keep their
GPL-3.0-only identifiers and the copyright notices of Leo Kuroshita and
Hügelton Instruments.

The USB MIDI driver, the M-UPGRADE updater and the browser installer modules
come from BB1 BAND r85, source commit
`1df20dcf9feda197a6bd668cf24f97d99d4f8ae1`, under the same licence. Their
original copyright notices remain in the source. `docs/USB-UPDATER-REVIEW.md`
records the exact archive and build inputs.

The standard firmware package also contains the JieLi SDK boot/configuration
components inherited byte for byte from the reference package. Those
components retain Apache-2.0; its text is in `LICENSES/Apache-2.0.txt`.

## Effects added in 1.2

ROOM is adapted from BB1 2.3's `bb_fxbus.c`, which derives from the OG Synth
X0X effects bus by Charles Vestal. The adaptation is GPL-3.0-only; the origin
and copyright attribution remain in `board/fx.c`. ECHO and GLUE adaptations
are documented in the same source. This does not change the licences of
the 727 recordings or Hanan factory data below.

## The 727 sound bank

The TR-727 percussion recordings in the sound bank are owned by the Compás
author and are included under the same terms as the rest of Compás.
`board/727-sources.json` lists the fifteen recordings.

## Kumbia Kit

The four sounds and four factory patterns are from [Hanan Cumbia by Oficina
de Sonido](https://www.oficinadesonido.org/wp-content/uploads/2026/07/Hanan-Cumbia.pdf)
(build `hanan26Taller`, 2026-07-21), based on Bleep Labs' Bleep Drum. The
unmodified sample and pattern data retain their
[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) licence.
Compás applies the knob settings during playback.

## Stage art and the tribute

The Picnic and Rodeo splash marks (`docs/STAGE-DESIGN.md`) and the Johnny
Canales portrait with its supplied photograph (`docs/JOHNNY-CANALES.md`) are
cleared for public release with Compás. Their provenance is recorded in those
documents.

## Written-song arrangements

Most written songs are arranged from printed editions in the U.S. public
domain; the sources are recorded in `docs/REPERTOIRE.md` and `repertoire/`.
Three come from openly licensed editions. Their score data and MIDI keep the
original licence, and the original authors are credited here:

| Song | Edition | Licence |
| --- | --- | --- |
| Juana Quilina | Engraving by Cheposo (2011), Wikimedia Commons | [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) · [source](https://commons.wikimedia.org/wiki/File:Juana_Quilina_Partitura..png) |
| La Adelita | David Edward Kemp, recorder quartet (2016), IMSLP 666981 | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) · [source](https://imslp.org/wiki/La_Adelita_(Folk_Songs,_Mexican)) |
| La Valentina | Manuel María Ponce, arr.; guitar edition by Stefan Apke (2019), IMSLP 561574 | [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) · [source](https://imslp.org/wiki/5_Canciones_mexicanas_para_guitarra_(Ponce,_Manuel)) |

## Trademarks

M-VAVE, FM-1, Roland and TR-727 are trademarks of their respective owners.
Compás is an independent project, not affiliated with or endorsed by them.
