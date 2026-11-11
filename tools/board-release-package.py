#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the installable Compás .fwsc with BB1's own loader and SDK files.

  python3 tools/board-release-package.py BSP BB1-r85.fwsc APP.xip VERSION LANG OUT_DIR

Compas 1.1 is packaged by the same pinned packager
(BSP tools/fm1pkg_make.py). Every part except the app comes from BB1 BAND r85's
published package, which has installed on real FM-1s:

  ota.bin       BB1's update loader, byte for byte (no Compás loader patch)
  uboot.boot, cfg_tool.bin, cfg/eq_cfg_hw.bin
                the SDK files exactly as BB1 shipped them, taken from its flash

Before writing anything this script proves the extraction: it rebuilds BB1
r85's flash image and package from the parts it took out of them, and both must
match the published bytes exactly. Only then does it put the Compás app in the
same standard layout (app slot 0x4000-0x93000) under the Compás identity.

The standard layout is why 1.1 keeps no saved show on the device: BB1's loader
rewrites 0x4000-0x93000, which holds the area 0x90000-0x93000 the emulator
store used. Build the app with COMP_RELEASE=1 (-DCOMPAS_NO_SHOW_STORE).
"""
import hashlib, importlib.util, json, re, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BB1_R85_SHA256 = "7c3c9524d2b689fb095017a2ee2fb1c1a475a9be3182f5055faf62274d74e367"
BB1_PRODUCT = "FM-1_92200201"
BB1_LOADER_SHA256 = "a48ee499520cb16750e2504264bd8a8cd3e13ea953bbf319537cb840eb0d2115"
XIP_PREFIX = 0x120


def packager(bsp):
    path = bsp / "tools/fm1pkg_make.py"
    pins = json.loads((ROOT / "board/BSP.json").read_text())["files"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != pins["tools/fm1pkg_make.py"]:
        raise SystemExit("The packager differs from the pinned BSP.")
    spec = importlib.util.spec_from_file_location("fm1pkg_release", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def logical(raw):
    """the .fwsc without its 20 identity marker bytes"""
    out = bytearray()
    for i in range(20):
        out += raw[i * 0x30:i * 0x30 + 0x2F]
    return bytes(out + raw[20 * 0x30:])


def ufw_files(pack, raw):
    """{name: bytes} from a .fwsc, read the way fm1pkg.js reads it"""
    img = logical(raw)
    head = bytearray(img[:0x40])
    pack.enc(head, 0, 0x40)
    count = struct.unpack_from("<H", head, 8)[0]
    files = {}
    for i in range(count):
        e = bytearray(img[0x40 + i * 0x50:0x90 + i * 0x50])
        pack.enc(e, 0, 0x50)
        offset, size = struct.unpack_from("<II", e, 8)
        name = bytes(e[0x40:0x50]).split(b"\0")[0].decode()
        files[name] = img[offset:offset + size]
    return files


def entry_fields(blob):
    """(offset, size) of a 32-byte JLFS entry"""
    return struct.unpack_from("<II", blob, 4)


def sdk_parts(pack, flash):
    """uboot.boot, cfg_tool.bin and cfg/eq_cfg_hw.bin as BB1's flash carries them"""
    head = bytearray(flash[:0xA0])
    for o in range(0, len(head), 32):
        pack.enc(head, o, 32)
    spl_off, spl_len = entry_fields(head[32:64])
    region = bytearray(flash[0x4000:])
    pack.sfc(region, 0, len(region), 0, pack.KEY)
    cfg_off, cfg_len = entry_fields(region[32 + 32:32 + 64])        # second app-area entry: cfg_tool.bin
    block = 0x120 + pack.APP_SLOT + cfg_len
    eq_off = block + 32                                             # cfg directory, then its eq entry
    _, eq_len = entry_fields(region[eq_off:eq_off + 32])
    return {"uboot.boot": bytes(flash[spl_off:spl_off + spl_len]),
            "cfg_tool.bin": bytes(region[cfg_off:cfg_off + cfg_len]),
            "cfg/eq_cfg_hw.bin": bytes(region[eq_off + 32:eq_off + 32 + eq_len]),
            "app": bytes(region[0x120:0x120 + pack.APP_SLOT])}


def main(argv):
    if len(argv) != 6:
        raise SystemExit(__doc__)
    bsp, bb1_path, xip_path, version, lang, out = Path(argv[0]), Path(argv[1]), Path(argv[2]), argv[3], argv[4], Path(argv[5])
    if not re.fullmatch(r"[0-9A-Za-z.-]+", version) or lang not in ("en", "es"):
        raise SystemExit("version is letters, digits, dots; language is en or es")
    pack = packager(bsp)
    bb1 = bb1_path.read_bytes()
    if hashlib.sha256(bb1).hexdigest() != BB1_R85_SHA256:
        raise SystemExit("Not the pinned BB1 BAND r85 package.")
    files = ufw_files(pack, bb1)
    flash, loader = files["flash.bin"], files["ota.bin"]
    if hashlib.sha256(loader).hexdigest() != BB1_LOADER_SHA256:
        raise SystemExit("BB1's loader differs from the pin.")
    parts = sdk_parts(pack, flash)
    pack.sdk_file = lambda name: parts[name]
    # Proof: BB1's flash and package rebuild exactly from the extracted parts.
    if pack.flash_image(parts["app"], pack.KEY) != flash:
        raise SystemExit("BB1's flash image does not rebuild from its parts.")
    if pack.ufw(flash, loader, BB1_PRODUCT) != bb1:
        raise SystemExit("BB1's package does not rebuild from its parts.")
    xip = xip_path.read_bytes()
    if xip[:XIP_PREFIX] != b"\xFF" * XIP_PREFIX:
        raise SystemExit("Expected the linked XIP image (0x120 bytes of 0xFF, then the app).")
    app = xip[XIP_PREFIX:]
    identity = re.search(r'#define FELUCCA_ID "([^"]+)"', (ROOT / "board/usb/identity.h").read_text())[1]
    if identity.encode() + b"\0" not in app:
        raise SystemExit("The app does not carry the Compás updater identity; build with COMP_USB=1.")
    if len(app) > pack.APP_SLOT:
        raise SystemExit(f"app {len(app)} B exceeds the slot {pack.APP_SLOT} B")
    compas_flash = pack.flash_image(app, pack.KEY)
    raw = pack.ufw(compas_flash, loader, identity)
    check = sdk_parts(pack, compas_flash)
    if check["app"][:len(app)] != app or any(check[k] != parts[k] for k in ("uboot.boot", "cfg_tool.bin", "cfg/eq_cfg_hw.bin")):
        raise SystemExit("The Compás package does not decode back to its parts.")
    out.mkdir(parents=True, exist_ok=True)
    name = f"compas-{version}-{lang}.fwsc"
    (out / name).write_bytes(raw)
    manifest = {"file": name, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(), "product": identity,
                "language": lang, "app": {"bytes": len(app), "slot": pack.APP_SLOT, "free": pack.APP_SLOT - len(app),
                "sha256": hashlib.sha256(app).hexdigest()},
                "loader": {"from": "BB1 BAND r85", "sha256": BB1_LOADER_SHA256},
                "bb1RoundTrip": "flash image and package rebuilt byte-identical",
                "layout": "standard BB1 (app 0x4000-0x93000); no saved show on the device"}
    (out / f"compas-{version}-{lang}.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main(sys.argv[1:])
