#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse a Compás .fwsc that could reach a protected part of the FM-1's flash.

  python3 tools/compas-pkg-assert.py BSP COMPAS.fwsc BB1-r85.fwsc

The Compás counterpart of BB1 2.3's bb1_pkg_assert.py, for Compás's own
package layout. A Compás package is not a whole-flash image: it is BB1 r85's
update loader (ota.bin) plus a 0x93000-byte flash.bin whose head [0, 0x4000)
the loader never writes. So the protected regions are kept safe by four facts,
all checked here against the pinned BB1 r85 package that has installed on
real FM-1s:

  1. the package carries exactly BB1 r85's files, nothing more;
  2. ota.bin is BB1 r85's loader, byte for byte (it decides what is written);
  3. flash.bin ends at or before 0x93000, so nothing in it reaches
     BTIF 0xE9000-0xEA000, key_mac 0xFF000-0x100000 or anything above the app;
  4. the head [0, 0x4000), which holds the boot SPL and the chip key blob,
     is byte-identical to BB1 r85's.

A check that a protected range is "erased" would be wrong here: the head is
always present in flash.bin (it must match, not be blank), and BTIF and
key_mac are not in the package at all. Exit 0 only if every check passes.
"""
import hashlib, importlib.util, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_END = 0x93000
HEAD_END = 0x4000
PROTECTED = (("bootloader head", 0x000000, 0x004000),
             ("BTIF", 0x0E9000, 0x0EA000),
             ("key_mac", 0x0FF000, 0x100000))


def release_tool():
    spec = importlib.util.spec_from_file_location("compas_release", ROOT / "tools/board-release-package.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check(pack, rel, compas_raw, bb1_raw):
    """list of (ok, message)"""
    results = []
    bb1_ok = hashlib.sha256(bb1_raw).hexdigest() == rel.BB1_R85_SHA256
    results.append((bb1_ok, "reference is the pinned BB1 BAND r85 package"))
    if not bb1_ok:
        return results
    ours, theirs = rel.ufw_files(pack, compas_raw), rel.ufw_files(pack, bb1_raw)
    results.append((sorted(ours) == sorted(theirs), f"files are BB1 r85's: {', '.join(sorted(ours))}"))
    loader = ours.get("ota.bin", b"")
    results.append((loader == theirs["ota.bin"] and hashlib.sha256(loader).hexdigest() == rel.BB1_LOADER_SHA256,
                    "ota.bin is BB1 r85's loader, byte for byte"))
    flash = ours.get("flash.bin", b"")
    results.append((0 < len(flash) <= APP_END, f"flash.bin is {len(flash):#x} bytes, ending at or before {APP_END:#x}"))
    for name, start, end in PROTECTED:
        if start >= HEAD_END:
            results.append((len(flash) <= start, f"{name} {start:#08x}-{end:#08x}: not in the package"))
    results.append((flash[:HEAD_END] == theirs["flash.bin"][:HEAD_END],
                    f"bootloader head 0x000000-{HEAD_END:#08x}: identical to BB1 r85"))
    return results


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    rel = release_tool()
    pack = rel.packager(Path(argv[0]))
    results = check(pack, rel, Path(argv[1]).read_bytes(), Path(argv[2]).read_bytes())
    for ok, message in results:
        print(("OK    " if ok else "FAIL  ") + message)
    return 0 if all(ok for ok, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
