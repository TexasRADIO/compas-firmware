"""Prepare an offline installer review. The package has an inert loader."""
import hashlib
import json
from pathlib import Path
import re
import runpy
import shutil
import sys

root = Path(__file__).resolve().parents[1]
bsp = Path(sys.argv[1])
out = Path(sys.argv[2]).resolve()
preflight = runpy.run_path(str(root / "tools/board-updater-preflight.py"))
layout = runpy.run_path(str(root / "tools/board-show-layout.py"))
builder = bsp / "tools/fm1pkg_make.py"
pins = json.loads((root / "board/BSP.json").read_text())["files"]
if hashlib.sha256(builder.read_bytes()).hexdigest() != pins["tools/fm1pkg_make.py"]:
    raise SystemExit("The packager differs from the pinned BSP.")
pack = preflight["packager_at"](builder)
xip = (root / "build/board/emu-store.xip").read_bytes()
if xip[:0x120] != bytes([255])*0x120:
    raise SystemExit("Expected the offline storage XIP.")
product = re.search(r'#define FELUCCA_ID "([^"]+)"', (root / "board/usb/identity.h").read_text())[1]
if product.encode()+b"\0" not in xip:
    raise SystemExit("Build the USB review target before generating the installer.")
app = xip[0x120:]
key = preflight["FIXTURE_KEY"]
flash = layout["compact_flash"](pack, app, key)
raw = pack.ufw(flash, b"INERT, EMULATOR ONLY", product)
manifest = {"status":"OFFLINE_REVIEW", "product":product, "sha256":hashlib.sha256(raw).hexdigest(),
            **layout["inspect_flash"](pack, flash, key, app)}
out.mkdir(parents=True)
for name in ("index.html", "compas-policy.mjs", "fm1pkg.js", "fm1ota.js", "package.json"):
    shutil.copyfile(root / "web/installer" / name, out / name)
(out / "Compas-USB-review.fixture").write_bytes(raw)
(out / "review-manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
print(json.dumps(manifest, indent=2))
