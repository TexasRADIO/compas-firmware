"""Reproduce saved-file planning with real app images and synthetic NOR inputs."""
import argparse
import json
from pathlib import Path
import runpy
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
P=runpy.run_path(str(ROOT/'tools/board-install-plan.py'))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bsp',type=Path,required=True)
    ap.add_argument('--prior',type=Path,required=True,help='Prior emu-store.xip')
    ap.add_argument('--prior-receipt',type=Path,required=True)
    ap.add_argument('--rehearsal',type=Path,required=True)
    ap.add_argument('--ota',type=Path,help='Checked offline loader wrapper')
    ap.add_argument('--inventory',type=Path)
    ap.add_argument('--loader-receipt',type=Path)
    ap.add_argument('output',type=Path);a=ap.parse_args()
    if any((a.ota,a.inventory,a.loader_receipt)) and not all((a.ota,a.inventory,a.loader_receipt)):
        ap.error('--ota, --inventory and --loader-receipt must be supplied together')
    out=a.output.resolve();out.mkdir()
    pack=P['pinned_packager'](a.bsp)
    pack.sdk_file=lambda name:b'HOST ONLY: '+name.encode()
    xip=(ROOT/'build/board/emu-store.xip').read_bytes();prior=a.prior.read_bytes()
    receipt=json.loads(a.rehearsal.read_text());old_receipt=json.loads(a.prior_receipt.read_text())
    if P['sha'](prior)!=old_receipt['imageSHA256']:raise ValueError('Prior app differs from receipt')
    P['check_rehearsal'](receipt,xip)
    legacy=pack.flash_image(prior[0x120:],0x1234)
    compact=P['LAYOUT']['compact_flash'](pack,xip[0x120:],0x1234)
    baseline=legacy+bytes((i*17+3)&255 for i in range(0x100000-len(legacy)))
    raw=pack.ufw(compact,a.ota.read_bytes() if a.ota else b'INERT PLANNER REHEARSAL; NO LOADER','COMPAS-FIXTURE')
    logical=b''.join(raw[i:i+47] for i in range(0,960,48))+raw[960:]
    subprocess.run(['node',str(ROOT/'tests/install-plan-show.mjs'),str(out/'regional.cpshow')],check=True)
    data={'baseline':baseline,'repeat':baseline,'legacy':legacy,'compact':compact,'logical':logical,
          'xip':xip,'rehearsal':a.rehearsal.read_bytes(),'show':(out/'regional.cpshow').read_bytes(),
          'wasm':(ROOT/'build/board/browser-board.wasm').read_bytes()}
    if a.ota:data.update(inventory=a.inventory.read_bytes(),loaderRehearsal=a.loader_receipt.read_bytes())
    manifest={'format':'compas-first-show-review-1','inputs':{}}
    for name,blob in data.items():
        (out/name).write_bytes(blob);manifest['inputs'][name]={'path':name,'sha256':P['sha'](blob)}
    source=out/'inputs.json';source.write_text(json.dumps(manifest,indent=2)+'\n')
    before={p.name:P['sha'](p.read_bytes()) for p in out.iterdir() if p.is_file()}
    result=subprocess.run([sys.executable,str(ROOT/'tools/board-install-plan.py'),str(source),str(out/'review'),'--bsp',str(a.bsp)],text=True,capture_output=True,check=True)
    report=json.loads(result.stdout)
    assert all(P['sha']((out/name).read_bytes())==digest for name,digest in before.items())
    report['rehearsalScope']='real Compas XIP and regional show; synthetic saved NOR; '+('checked offline loader wrapper' if a.ota else 'inert OTA')
    report['allInputsUnchanged']=True
    (out/'rehearsal.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
