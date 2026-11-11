# Building Compas 1.1

This release tree contains the Compas source, pinned Felucca board support,
release score data, sound-bank inputs and the scripts needed to regenerate the
English and Spanish applications.

## Host and WebAssembly engine

Requirements: a C11 compiler, Clang with the WebAssembly target, Python 3.10+
and `make`.

```sh
make check
```

This compiles the host engine and regenerates every checked-in board input from
the public release files. Run `make all` to build the WebAssembly engine too.

## FM-1 application

The FM-1 application additionally needs the PI32v2 JieLi toolchain and Docker.
Set `COMP_JIELI` to the toolchain directory; the pinned BSP and sound inputs
default to the copies in `release-inputs/`.

```sh
COMP_JIELI=/path/to/jieli COMP_USB=1 COMP_EMU_STORE=1 COMP_RELEASE=1 \
  COMPAS_LANG_DEFAULT=0 make board
```

Use `COMPAS_LANG_DEFAULT=1` for the Spanish application. The linked binary is
written to `build/board/compas-store.xip`.

## Release package

`tools/board-release-package.py` creates the `.fwsc`. It requires the published
BB1 BAND r85 package because Compas 1.1 deliberately uses that package's loader
and SDK files byte-for-byte. The script verifies a byte-identical BB1 round trip,
the Compas USB identity and the decoded output before it writes a package.

```sh
python3 tools/board-release-package.py \
  release-inputs/felucca /path/to/BB1-r85.fwsc \
  build/board/compas-store.xip 1.1 en build/release
```

The release checksums are in `RELEASE-SHA256SUMS`.
