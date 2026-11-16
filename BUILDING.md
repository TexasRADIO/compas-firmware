# Reproduce Compás 1.2

This release contains the corresponding firmware source, installer modules and
public packaging helpers. The exact application and package hashes for each language are in
[RELEASE.json](RELEASE.json).

The public 727, Hanan and pinned Felucca inputs are included in
`release-inputs/`, with their notices. Compiler binaries are external: use the
JieLi `jieli-linux-toolchains-20260730.1` PI32v2 toolchain and the Docker image
pinned by `tools/board-target.sh`.

From the source directory:

```sh
export COMP_STUDIO="$PWD/release-inputs/studio"
export COMP_BSP="$PWD/release-inputs/felucca"
export COMP_HANAN="$PWD/release-inputs/hanan"
# Set COMP_JIELI to the unpacked pinned toolchain.
COMP_USB=1 COMP_EMU_STORE=1 COMP_RELEASE=1 COMPAS_LANG_DEFAULT=0 sh tools/board-target.sh
python3 -B tools/board-emu.py build/board store prepare
```

Use `COMPAS_LANG_DEFAULT=1` in a separate source/build directory for Spanish.
Both released applications are 573,588 bytes, with 7,976 bytes free in the
application slot.

To reproduce the package, first obtain the already-public
[Compás 1.1 English seed](https://github.com/TexasRADIO/compas-firmware/releases/download/compas-v1.1/compas-1.1-en.fwsc).
Its SHA-256 is
`b2236ebe924b4dd1f690bcc0fa765ffac7a92131502c542fb27a7d7ea0b12dea`.

```sh
python3 -B tools/board-public-reproduce.py "$COMP_BSP" compas-1.1-en.fwsc build/board/emu-store.xip compas-1.2-en.fwsc
```

For the Spanish build, use the Spanish XIP and output name. This verifies the
whole seed hash, unchanged loader and boot head, application identity and size,
the standard flash layout, and the exact decode round trip. Both released
packages were reproduced byte for byte through this public-input path.

The standard application writes occupy `0x4000`–`0x93000`; update staging uses
`0xE0000`–`0xE5000`. Boot/SPL, BTIF and device identity data remain outside the
application write range. The standard build uses session-only show storage.

Public reproduction uses the Compás seed above. Packaging tools produce local
files only; installation is a separate operation.
