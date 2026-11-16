# Compás 1.2 with Big Beat Editor

Compás turns the M-VAVE FM-1 into a Latin rhythm section: cumbia, salsa,
merengue and bolero, with English and Spanish interfaces. This release adds
the shared Big Beat browser editor to Compás 1.2. It includes ROOM reverb,
tempo-synced ECHO, GLUE compression, 29 groove studies, written-song
arrangements, TR-727 percussion and Hanan Kumbia Kit sounds.

- [Install Compás](https://thebigbeat.app/compas/install/)
- [Open Big Beat Editor](https://thebigbeat.app/editor/)
- [Play in the browser](https://thebigbeat.app/compas/play/)
- [Playing guide](https://thebigbeat.app/compas/)
- [Release downloads](https://github.com/TexasRADIO/compas-firmware/releases/tag/compas-v1.2-editor.1)

Choose the English-default or Spanish-default editor firmware. Both include
the same editor adapter and use updater identity `FM-1_92210003`. The screen
language can also be changed on the instrument: GLO, then ALGORITHM.

Connect the FM-1 by USB and open Big Beat Editor in desktop Chrome or Edge.
Allow MIDI access including SysEx and select Compás. The editor reads the
instrument state and sends edits through the firmware’s shared protocol.
Tempo, feel, groove, section, key, clave, mixer lanes and effects use the
Compás instrument model. Working show edits last for the current device
session. Portable show files retain their running order and rhythms.

Exact package hashes are in [RELEASE.json](RELEASE.json). The complete
corresponding firmware source and public build inputs are included here.
See [BUILDING.md](BUILDING.md) for reproduction and [LICENSING.md](LICENSING.md)
for licences and credits.

Compás is GPL-3.0-only. Included third-party material keeps its stated licence.
