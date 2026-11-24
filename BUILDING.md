# Reproduce Compás 1.3

This tag contains the complete corresponding firmware source and the licensed
release inputs used to build Compás 1.3:

- `release-inputs/studio`: declared TR-727 recordings;
- `release-inputs/hanan`: Hanan Cumbia factory sounds and patterns;
- `release-inputs/felucca`: pinned GPL board support and packaging helpers.

Compiler binaries are external. Use the JieLi PI32v2
`jieli-linux-toolchains-20260730.1` toolchain with the Docker image pinned by
`tools/board-target.sh`. Use a separate clean build tree for each language.
No physical FM-1 is used by these commands.

```sh
export COMP_STUDIO="$PWD/release-inputs/studio"
export COMP_BSP="$PWD/release-inputs/felucca"
export COMP_HANAN="$PWD/release-inputs/hanan"
# Set COMP_JIELI to the unpacked pinned JieLi toolchain.
COMP_USB=1 COMP_EMU_STORE=1 COMP_RELEASE=1 COMPAS_LANG_DEFAULT=0 sh tools/board-target.sh
python3 -B tools/board-emu.py build/board store prepare
```

Use `COMPAS_LANG_DEFAULT=1` for Spanish. Both applications are 565,852 bytes.
The journal-safe application limit is 577,248 bytes, leaving 11,396 bytes.
The packaged application starts after the 0x120 entry prefix in the XIP image.

Public packaging uses the already-published Compás 1.1 English package as the
verified loader/head seed:

- URL: `https://github.com/TexasRADIO/compas-firmware/releases/download/compas-v1.1/compas-1.1-en.fwsc`
- SHA-256: `b2236ebe924b4dd1f690bcc0fa765ffac7a92131502c542fb27a7d7ea0b12dea`

```sh
python3 tools/board-public-reproduce.py \
  "$COMP_BSP" compas-1.1-en.fwsc build/board/emu-store.xip \
  compas-1.3-en.fwsc --unit-store
```

Repeat with the Spanish XIP and filename. The expected package hashes are in
`RELEASE.json`. This route preserves the verified loader/head and produces the
same package bytes used for qualification.

The application write envelope is 0x4000–0x92FFF. Update staging is
0xE0000–0xE4FFF. Boot/SPL, BTIF and unit identity/key data lie outside those
ranges. Firmware updates rewrite the CPS6 A/B journal at 0x91000–0x92FFF;
export the show before updating.

`make test` runs engine, screen, editor, store and converter checks. The native
qualification receipts used for the release are summarized in
`docs/QUALIFICATION-1.3.md`. Native emulator timing does not establish an
analogue output voltage or physical IRQ timing.

The optional browser build is freestanding WebAssembly. Put a compatible
`clang` and `wasm-ld` on PATH, or set `COMPAS_WASM_CC` and `COMPAS_WASM_LD`
explicitly, then run `make`. Those tools are not needed for the FM-1 package.
