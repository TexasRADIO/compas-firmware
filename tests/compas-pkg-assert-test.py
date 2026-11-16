#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The package check refuses each kind of unsafe package.

  python3 tests/compas-pkg-assert-test.py BSP COMPAS.fwsc BB1-r85.fwsc

Starts from a good Compás package, then rebuilds it three bad ways with the
pinned packager: a changed boot head, a flash.bin reaching BTIF and a
different loader; and checks against an unpinned reference. The good one must
pass and every bad one must fail.
"""
import importlib.util, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("compas_pkg_assert", ROOT / "tools/compas-pkg-assert.py")
pa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pa)


def passes(pack, rel, raw, bb1):
    return all(ok for ok, _ in pa.check(pack, rel, raw, bb1))


def main(argv):
    rel = pa.release_tool()
    pack = rel.packager(Path(argv[0]))
    good, bb1 = Path(argv[1]).read_bytes(), Path(argv[2]).read_bytes()
    files = rel.ufw_files(pack, good)
    identity = "FM-1_92210003"
    flash, loader = bytearray(files["flash.bin"]), files["ota.bin"]
    assert passes(pack, rel, good, bb1), "the good package must pass"
    assert passes(pack, rel, pack.ufw(bytes(flash), loader, identity), bb1), "a plain rebuild must pass"
    head = bytearray(flash); head[0x200] ^= 0xFF
    long = bytes(flash) + b"\xFF" * (0xEA000 - len(flash))
    other_loader = bytearray(loader); other_loader[-1] ^= 0x01
    cases = {"changed boot head": pack.ufw(bytes(head), loader, identity),
             "flash.bin reaching BTIF": pack.ufw(long, loader, identity),
             "different loader": pack.ufw(bytes(flash), bytes(other_loader), identity)}
    for name, raw in cases.items():
        assert not passes(pack, rel, raw, bb1), f"{name} must fail"
        print(f"refused: {name}")
    assert not passes(pack, rel, good, good[:-1] + b"\0"), "a wrong reference must fail"
    print("refused: an unpinned reference")
    print("package check ok: the good package passes, every unsafe one is refused")


if __name__ == "__main__":
    main(sys.argv[1:])
