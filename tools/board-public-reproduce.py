#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce a standard-layout package using the already-public 1.1 EN seed.

Usage: board-public-reproduce.py BSP PUBLIC_1_1_EN.fwsc APP.xip OUT.fwsc [--unit-store]

This is an offline reproducibility path, not a device installer. The release
qualification path remains board-release-package.py + package-check against
pinned r85. Public rebuilds do not require that private reference binary.
"""
from pathlib import Path
import hashlib, importlib.util, json, sys

SEED_SHA256 = 'b2236ebe924b4dd1f690bcc0fa765ffac7a92131502c542fb27a7d7ea0b12dea'
PRODUCT = 'FM-1_92210003'
SEED_URL = 'https://github.com/TexasRADIO/compas-firmware/releases/download/compas-v1.1/compas-1.1-en.fwsc'
ROOT = Path(__file__).resolve().parents[1]
def digest(b): return hashlib.sha256(b).hexdigest()
def reproduce(bsp, seed, xip, unit_store=False):
    if digest(seed) != SEED_SHA256:
        raise ValueError('Not the pinned public Compas 1.1 English package')
    spec=importlib.util.spec_from_file_location('release',ROOT/'tools/board-release-package.py')
    rel=importlib.util.module_from_spec(spec);spec.loader.exec_module(rel)
    pack=rel.packager(bsp)
    files=rel.ufw_files(pack,seed)
    if sorted(files)!=['flash.bin','ota.bin'] or digest(files['ota.bin'])!=rel.BB1_LOADER_SHA256:
        raise ValueError('Unexpected reference files or loader')
    flash=files['flash.bin'];parts=rel.sdk_parts(pack,flash)
    pack.sdk_file=lambda n:parts[n]
    if pack.flash_image(parts['app'],pack.KEY)!=flash or pack.ufw(flash,files['ota.bin'],PRODUCT)!=seed:
        raise ValueError('Public reference does not round-trip exactly')
    if xip[:rel.XIP_PREFIX]!=b'\xff'*rel.XIP_PREFIX:
        raise ValueError('Expected the linked XIP entry prefix')
    app=xip[rel.XIP_PREFIX:]
    limit=0x91000-0x4120 if unit_store else pack.APP_SLOT
    if not 0<len(app)<=limit or PRODUCT.encode()+b'\0' not in app:
        raise ValueError('Wrong application size or updater identity')
    rebuilt=pack.flash_image(app,pack.KEY)
    if len(rebuilt)!=0x93000 or rebuilt[:0x4000]!=flash[:0x4000]:
        raise ValueError('Changed protected head or application envelope')
    result=pack.ufw(rebuilt,files['ota.bin'],PRODUCT)
    check=rel.ufw_files(pack,result)
    if check['flash.bin']!=rebuilt or check['ota.bin']!=files['ota.bin'] or rel.sdk_parts(pack,rebuilt)['app'][:len(app)]!=app:
        raise ValueError('Output does not decode to the exact application and loader')
    return result

def main(argv):
    unit_store=argv[-1:] == ['--unit-store']
    if unit_store:argv=argv[:-1]
    if len(argv)!=4:raise SystemExit(__doc__)
    bsp,seed,xip,out=map(Path,argv)
    raw=reproduce(bsp,seed.read_bytes(),xip.read_bytes(),unit_store)
    out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(raw)
    print(json.dumps({'file':out.name,'sha256':digest(raw),'bytes':len(raw),'seedSHA256':SEED_SHA256,'seedURL':SEED_URL,'physicalWrite':False}))
if __name__=='__main__':main(sys.argv[1:])
