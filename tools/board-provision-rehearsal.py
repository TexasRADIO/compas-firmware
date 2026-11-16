"""Model first provisioning after a compact update, then boot the linked app.

Synthetic raw flash only: no device connection, production marker or OTA file.
The preceding update is composed from verified envelopes, not replayed here.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import runpy
import struct
import subprocess
import tempfile
import sys
import shutil
from contextlib import contextmanager

@contextmanager
def retained_on_failure():
    work=Path(tempfile.mkdtemp(prefix="provision-",dir=BOARD))
    try:
        yield work
    except BaseException:
        print("Failed provisioning fixture retained at "+str(work),file=sys.stderr)
        raise
    else:
        shutil.rmtree(work)

ROOT=Path(__file__).resolve().parents[1]
BOARD=ROOT/'build/board'

def sha(b):return hashlib.sha256(b).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('bsp','emulator','prior-board','prior-rehearsal','rehearsal'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('linked_loader_probe',ROOT/'tools/board-loader-emu.py')
    probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)
    assert sha(args.emulator.read_bytes())==probe.EMU_SHA256
    probe.SHOW_SEED=True;probe.BOARD_VARIANT='store';probe.APP_END=0x90000
    current=probe.rehearsed_xip(BOARD,args.rehearsal)
    prior=probe.rehearsed_xip(args.prior_board,args.prior_rehearsal)
    pins=json.loads((ROOT/'board/BSP.json').read_text())['files']
    builder=args.bsp/'tools/fm1pkg_make.py'
    assert sha(builder.read_bytes())==pins['tools/fm1pkg_make.py']
    preflight=runpy.run_path(str(ROOT/'tools/board-updater-preflight.py'))
    pack=preflight['packager_at'](builder);key=preflight['FIXTURE_KEY']
    provisioning=runpy.run_path(str(ROOT/'tools/board-show-provision.py'))
    legacy=pack.flash_image(prior[0x120:],key)
    compact=probe.SHOW_LAYOUT['compact_flash'](pack,current[0x120:],key)
    # Known prior package owns the old tail. Other regions have non-empty data.
    baseline=legacy+bytes((i*17+3)&255 for i in range(0x100000-len(legacy)))
    updated=baseline[:0x4000]+compact[0x4000:]+baseline[0x90000:]
    symbols={line.split()[-1]:int(line.split()[0],16)-0x1c00000
             for line in (BOARD/'compas-store.sections').read_text().splitlines()
             if line.split() and re.fullmatch('[0-9a-f]{8}',line.split()[0])}
    with retained_on_failure() as work:
        # This generator imports the musical payload through the real WASM owner.
        subprocess.run(['node',str(ROOT/'tests/show-flash.mjs'),str(work)],check=True,capture_output=True)
        prepared=(work/'show-virtual.bin').read_bytes()
        plan=provisioning['prepare'](baseline,bytes(bytearray(baseline)),updated,legacy,compact,prepared)
        total=plan.receipt()['bytePrefixCuts']
        checkpoints={0,total-1,total-7};n=0
        for op in plan.operations:
            n+=len(op.data)
            if n<total:checkpoints.add(n)
        # Actual linked marker gate: even an attempted SAVE must not write.
        def boot_gate(name,raw,enabled,save=False):
            folder=work/name;folder.mkdir()
            area=bytearray(raw[0x4000:0x90000]);pack.sfc(area,0,len(area),0,key)
            mapped=raw[:0x4000]+area+raw[0x90000:]
            flash=folder/'virtual.bin';flash.write_bytes(mapped)
            ram=folder/'ram.bin'
            env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
            env.update(FM1_DUMPRAM=str(ram),FM1_BENCH_KEYS='1200:7,1290:7^,1500:9,1590:9^')
            run=subprocess.run([str(args.emulator.resolve()),'--bin',str(BOARD/'emu-store.xip'),
                '--entry','0x02000120','--midi','none','--flash',str(flash),'--bench','2200'],
                cwd=folder,env=env,capture_output=True,text=True,check=True,timeout=30)
            memory=ram.read_bytes()
            assert struct.unpack_from('<I',memory,symbols['store_enabled'])[0]==enabled
            count=int(re.search(r'flash: (\d+) erases / programs',run.stdout+run.stderr)[1])
            after=flash.read_bytes()
            if not save:
                assert count==0 and after==mapped
            else:
                assert count==17 and after[:0x90000]==mapped[:0x90000] and after[0x93000:]==mapped[0x93000:]
            # Restore encryption for the shared direct-boot/readback checker.
            area=bytearray(after[0x4000:0x90000]);pack.sfc(area,0,len(area),0,key)
            return after[:0x4000]+area+after[0x90000:],count
        for cut in sorted(checkpoints):
            torn=plan.run(cut)
            boot_gate('cut-'+str(cut),torn,0)
            assert plan.resume(torn)==plan.run()
        final=plan.run()
        initial=probe.boot_installed(work,'provisioned-boot',args.emulator.resolve(),final,pack,key,current,BOARD)
        saved,ops=boot_gate('first-save',final,1,True)
        generation,payload=probe.SHOW_LAYOUT['newest_show'](saved)
        assert generation==2 and payload==probe.SHOW_LAYOUT['newest_show'](final)[1]
        reopened=probe.boot_installed(work,'reopened',args.emulator.resolve(),saved,pack,key,current,BOARD)
        report=plan.receipt()
        report.update(scope=__doc__.strip(),priorImageSHA256=sha(prior),currentImageSHA256=sha(current),
            nativeValidatedShowSHA256=sha((work/'show.cpshow').read_bytes()),
            linkedDisabledMarkerCuts=sorted(checkpoints),linkedFirstSaveOperations=ops,
            initialBoot=initial,reopenedBoot=reopened,
            sourceSHA256={f:sha((ROOT/f).read_bytes()) for f in
                ['tools/board-show-provision.py','tools/board-provision-rehearsal.py','board/store.c','board/fm1.c']})
        print(json.dumps(report,indent=2))

if __name__=='__main__':main()
