"""Audit the legacy Compás package format in memory without exporting firmware."""
import argparse
import json
from pathlib import Path
import runpy
import struct

parser = argparse.ArgumentParser(description=__doc__)
for name in ('bsp', 'prior', 'xip', 'rehearsal'):
    parser.add_argument(name, type=Path)
parser.add_argument('--compare-to', type=Path, metavar='ARCHIVED_FWSC',
                    help='require the modeled package to match an existing archive')
args = parser.parse_args()
plan = runpy.run_path(str(Path(__file__).with_name('board-install-plan.py')))
digest = plan['sha']
pack = plan['pinned_packager'](args.bsp)
host = runpy.run_path(str(args.bsp/'tools/fm1_install.py'))
prior = args.prior.read_bytes()
assert digest(prior) == '64c7fafa9a5c17ed373398e7e676c9a0ec1bd4385f841ffc984a7023e0d4c719'
assert host['product_of'](prior) == 'FM-1_92210001'
logical = host['logical_image'](prior)
flash = logical[0x400:0x400+pack.FLASH_SIZE]
ota = plan['inspect_logical'](pack, logical, flash)
key = plan['capture_key'](pack, flash)
plain = bytearray(flash[0x4000:])
pack.sfc(plain, 0, len(plain), 0, key)
slot_end = 0x120 + pack.APP_SLOT
old = bytes(plain[0x120:slot_end])
legacy = dict(app_end=pack.FLASH_SIZE, app_slot=pack.APP_SLOT)
layout = plan['LAYOUT']['inspect_flash']
# The prior package's padded app and cfg rows verify, but its enclosing CRC
# predates the app replacement. Identify the prior executable independently.
prior_app = old[:487732]
assert digest(prior_app) == '19478eb836167dbab6a6d6dfef79d2a1346e5331553944518d528da27f01aaf8'
assert old[len(prior_app):] == b'\xff' * (pack.APP_SLOT-len(prior_app))

xip = args.xip.read_bytes()
receipt = json.loads(args.rehearsal.read_text())
plan['check_rehearsal'](receipt, xip)
assert xip[:0x120] == b'\xff' * 0x120
app = xip[0x120:]
assert 0 < len(app) <= pack.APP_SLOT

# Keep the exact previously used SPL, chip record, region map, configuration and OTA.
# Change only the padded app payload and the CRCs of its two enclosing records.
def replace_app(blob, data):
    blob = bytearray(blob)
    blob[0x120:slot_end] = data + b'\xff' * (pack.APP_SLOT-len(data))
    def update_crc(offset, payload):
        struct.pack_into('<H', blob, offset+2, pack.crc16(payload))
        struct.pack_into('<H', blob, offset, pack.crc16(blob[offset+2:offset+32]))
    end, = struct.unpack_from('<I', blob, 8)
    update_crc(32, blob[0x120:slot_end])
    update_crc(0, blob[32:end])
    pack.sfc(blob, 0, len(blob), 0, key)
    return flash[:0x4000] + blob

checked_prior = replace_app(plain, old)
assert checked_prior[0x4020:] == flash[0x4020:]
layout(pack, checked_prior, key, old, **legacy)
candidate = replace_app(plain, app)
regions = layout(pack, candidate, key, app, **legacy)
assert candidate[:0x4000] == flash[:0x4000] and candidate[0x4000+slot_end:] == flash[0x4000+slot_end:]
assert candidate[0x4040:0x4120] == flash[0x4040:0x4120]
product = 'FM-1_92210002'
package = pack.ufw(candidate, ota, product)
assert host['product_of'](package) == product
assert plan['inspect_logical'](pack, host['logical_image'](package), candidate) == ota
if args.compare_to:
    archived, _ = plan['read_saved'](args.compare_to, 0x100000)
    if archived != package:
        parser.exit(1, 'Modeled package differs from the archived file.\n')
print(json.dumps(dict(product=product, packageSHA256=digest(package), priorSHA256=digest(prior),
    appSHA256=digest(app), xipSHA256=digest(xip), flashEnvelopeSHA256=digest(candidate),
    otaSHA256=digest(ota), appBytes=len(app), packageBytes=len(package),
    priorOuterAppCRCRecomputed=True, priorSPLConfigurationAndOTAReused=True,
    archiveCompared=args.compare_to is not None, packageExported=False,
    physicalInstallPerformed=False, **{k:v for k,v in regions.items() if k!='appBytes'}),indent=2))
