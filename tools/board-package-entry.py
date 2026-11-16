"""Check the emulator's package entry against a validated synthetic saved plan."""
import argparse
import importlib.util
import json
from pathlib import Path
import runpy
import struct
import tempfile
import zlib

TOOLS = Path(__file__).resolve().parent
BOARD = TOOLS.parent / 'build/board'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('emulator', 'bsp', 'saved_plan', 'receipt'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    if args.receipt.exists():
        parser.error('Choose a new receipt path')
    spec = importlib.util.spec_from_file_location('loader_probe', TOOLS / 'board-loader-emu.py')
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    probe.require(probe.digest(args.emulator.read_bytes()) == probe.EMU_SHA256, 'Wrong emulator')
    planner = runpy.run_path(str(TOOLS / 'board-install-plan.py'))
    provision = runpy.run_path(str(TOOLS / 'board-show-provision.py'))
    saved = args.saved_plan.resolve()

    with tempfile.TemporaryDirectory(prefix='compas-package-entry-') as temporary:
        work = Path(temporary)
        # Revalidate the same saved inputs through the ordinary read-only planner.
        report = planner['prepare'](saved / 'inputs.json', args.bsp, work / 'review')
        data = {name: (saved / name).read_bytes() for name in
                ('baseline', 'repeat', 'legacy', 'compact', 'logical', 'xip', 'loaderRehearsal')}
        for name, value in data.items():
            probe.require(probe.digest(value) == report['inputSHA256'][name], 'Saved input changed: ' + name)
        probe.require((BOARD / 'emu-store.xip').read_bytes() == data['xip'],
                      'Use the build directory linked to the saved-plan XIP')
        baseline, compact = data['baseline'], data['compact']
        pack = planner['pinned_packager'](args.bsp)
        key = planner['capture_key'](pack, baseline)
        probe.require(key == 0x1234, 'Expected synthetic fixture key')
        ota = planner['inspect_logical'](pack, data['logical'], compact)
        package = work / 'synthetic.fwsc'
        raw = pack.ufw(compact, ota, 'COMPAS-FIXTURE')
        loader = json.loads(data['loaderRehearsal'])
        probe.require(probe.digest(raw) == loader['packagedLoader']['fixturePackageSHA256'],
                      'Package bytes differ from the checked synthetic loader fixture')
        package.write_bytes(raw)

        record = (work / 'review/provision-record.bin').read_bytes()
        payload = record[64+12288:]
        probe.require(len(payload) == 3776 and probe.digest(payload) == report['showPayloadSHA256'],
                      'Unexpected provisioned show')
        show = bytearray(b'\xff' * 0x100000)
        show[0x90100:0x90100+len(payload)] = payload
        show[0x92000:0x92008] = b'CMPASIM1'
        crc = zlib.crc32(struct.pack('<I', 1) + payload)
        struct.pack_into('<III', show, 0x90000, 0x32535043, 1, crc)
        updated = baseline[:0x4000] + compact[0x4000:] + baseline[0x90000:]
        installed = provision['prepare'](baseline, data['repeat'], updated, data['legacy'], compact, show).run()
        probe.require(probe.digest(installed) == report['expectedAfterShowBeforeCleanupSHA256'],
                      'Synthetic NOR differs from the saved plan')

        probe.APP_END = 0x90000
        probe.BOARD_VARIANT = 'store'
        probe.SHOW_SEED = work / 'review/provision-record.bin'
        evidence = {}
        for mode, source in (('rawEntry', None), ('packageEntry', package)):
            result = probe.boot_installed(work, mode, args.emulator.resolve(), installed, pack,
                                          key, data['xip'], BOARD, package=source)
            timing = result['timing']
            probe.require(not any(timing[name] for name in ('deadlines', 'lateHalves', 'railSamples')),
                          mode + ' recorded audio faults')
            probe.require(timing['maxIrqWindowUs']+timing['irqUnmeasuredAllowanceUs'] < timing['deadlineUs'],
                          mode + ' exceeded the audio deadline')
            probe.require(timing['recoveredShowPayloadSHA256'] == report['showPayloadSHA256'],
                          mode + ' recovered a different show')
            result['pcmSHA256'] = probe.digest((work / mode / 'audio.raw').read_bytes())
            evidence[mode] = result
        probe.require(evidence['rawEntry']['pcmSHA256'] == evidence['packageEntry']['pcmSHA256'],
                      'Raw and package app entry rendered different PCM')

    output = dict(scope='synthetic package parsing and application entry',
                  sourceCommit=report['sourceCommit'], physicalBootQualified=False,
                  savedPlanSHA256=report['manifestSHA256'], **evidence)
    with args.receipt.open('x') as out:
        json.dump(output, out, indent=2)
        out.write('\n')
    print(json.dumps(output, indent=2))


if __name__ == '__main__':
    main()
