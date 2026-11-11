# Licensing and credits

Compas source code is licensed under GPL-3.0-only. The full licence is in
`LICENSE`. There is no warranty.

## Felucca and updater components

Compas uses Felucca board support pinned to commit
`ef1f561b6b105f20350f6b1eac0dc0789a357f00`. The retained source and licence are
in `release-inputs/felucca/`; `board/BSP.json` records the file hashes. Felucca
copyright notices remain in the derived files.

The USB updater code and release loader originate in the GPL-3.0-only BB1 BAND
r85 package. `tools/board-release-package.py` verifies the loader hash and a
byte-identical package round trip before producing a Compas package. The SDK
files carried by that package are covered by the Apache-2.0 text in
`LICENSES/Apache-2.0.txt`.

## Sound material

The fifteen TR-727-style recordings listed in `board/727-sources.json` were
recorded by the Compas author and are released with Compas under GPL-3.0-only.

The four Hanan Cumbia sounds and four factory patterns in
`release-inputs/hanan/` are from Oficina de Sonido's Hanan Cumbia build
`hanan26Taller` (2026-07-21), based on Bleep Labs' Bleep Drum. They retain
[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/); provenance is
recorded in `release-inputs/hanan/SOURCE.txt`.

## Repertoire data

The release includes original rhythm studies, public-domain score adaptations
and three openly licensed notation sources. Source and adaptation notes stay
with the corresponding JSON files in `repertoire/`.

- `juana-quilina-fragment.json`: Cheposo engraving, CC BY-SA 3.0.
- `la-adelita-kemp-quartet.json`: David Edward Kemp arrangement, CC BY 4.0.
- `la-valentina-apke-guitar.json`: Stefan Apke edition, CC BY-SA 4.0.

M-VAVE, FM-1, Roland and TR-727 are trademarks of their respective owners.
Compas is an independent project and is not affiliated with or endorsed by
those companies.
