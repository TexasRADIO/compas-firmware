"""Check direct native opening, saving and cold replay against the paired build."""
import array
import hashlib
import json
import os
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT/'build/board'
EMULATOR = Path(sys.argv[1]).resolve()
BROWSER_SHOW = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
IMAGE = BOARD/'emu-store.xip'
WASM = BOARD/'browser-board.wasm'
LAUNCHER = ROOT/'tools/play-prepared-set.py'
LAYOUT = runpy.run_path(str(ROOT/'tools/board-show-layout.py'))
ENV = {key:value for key,value in os.environ.items() if not key.startswith('FM1_')}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(source, folder, *, wasm_sha=None, success=True):
    args = [sys.executable, str(LAUNCHER), str(source), '--emulator', str(EMULATOR),
            '--image', str(IMAGE), '--image-sha', digest(IMAGE.read_bytes()),
            '--state-dir', str(folder), '--prepare-only']
    if wasm_sha is not None:
        args.extend(('--show-wasm', str(WASM), '--show-wasm-sha', wasm_sha))
    result = subprocess.run(args, capture_output=True, text=True, env=ENV)
    if success:
        assert result.returncode == 0, result.stderr
        return Path(result.stdout.strip())
    assert result.returncode and not list(folder.rglob('*.bin')), result.stdout
    return result.stderr


def replay(flash, folder, actions, duration):
    folder.mkdir()
    ram, audio = folder/'ram.bin', folder/'audio.raw'
    env = dict(ENV, FM1_STRICT='1', FM1_BENCH_KEYS=actions,
               FM1_DUMPRAM=str(ram), FM1_BENCH_WAV=str(audio))
    result = subprocess.run([str(EMULATOR), '--bin', str(IMAGE), '--entry', '0x02000120',
                             '--midi', 'none', '--flash', str(flash), '--bench', str(duration)],
                            capture_output=True, text=True, cwd=folder, env=env, timeout=120)
    assert result.returncode == 0, result.stdout[-2000:] + result.stderr[-2000:]
    metrics = json.loads(subprocess.check_output(
        [sys.executable, str(ROOT/'tools/board-emu.py'), str(BOARD), 'store',
         '--native-ram', str(ram)], text=True, env=ENV))
    at = next(int(line.split()[0], 16)-0x01c00000
              for line in (BOARD/'compas-store.sections').read_text().splitlines()
              if line.split()[-1:] == ['view'])
    view = ram.read_bytes()[at:at+76]
    assert not view[14]
    samples = array.array('h', audio.read_bytes())
    assert samples[::2] == samples[1::2]
    return view, samples[::2], metrics


with tempfile.TemporaryDirectory(prefix='compas-prepared-show-') as temporary:
    work = Path(temporary)
    subprocess.run(['node', str(ROOT/'tests/show-flash.mjs'), str(work)],
                   check=True, capture_output=True, env=ENV)
    sha = digest(WASM.read_bytes())
    source = work/'show.cpshow'
    original = source.read_bytes()
    expected = (work/'show-virtual.bin').read_bytes()
    assert 'needs the paired browser WASM' in prepare(source, work/'missing', success=False)
    assert 'differs from the paired browser build' in prepare(source, work/'bad-wasm',
                                                               wasm_sha='0'*64, success=False)
    assert prepare(work/'invalid.cpshow', work/'invalid', wasm_sha=sha, success=False)
    flash = prepare(source, work/'state', wasm_sha=sha)
    assert flash.read_bytes() == expected
    generation, payload = LAYOUT['newest_show'](expected)
    assert generation == 1 and payload[26] == 113

    # Use a filename that would collide with the launcher's staging path.
    legacy = work/'prepared.bin'
    legacy.write_bytes((ROOT/'repertoire/regional-audition-set.json').read_bytes())
    old_flash = prepare(legacy, work/'state')
    subprocess.run([sys.executable, str(ROOT/'tools/board-set.py'), '--virtual-flash',
                    str(legacy), str(work/'old-virtual.bin')], check=True, env=ENV)
    assert old_flash.read_bytes() == (work/'old-virtual.bin').read_bytes()

    save_view, _, save_metrics = replay(flash, work/'save',
        '900:pot:760,1250:e2:+1,1450:7,1540:7^,1750:9,1840:9^', 2800)
    saved = flash.read_bytes()
    generation, payload = LAYOUT['newest_show'](saved)
    assert generation == 2 and save_view[6] == payload[26] == 114, (generation, list(save_view))
    assert payload[1365] == 114 and payload[1370] == 2
    assert struct.unpack_from('<2I', payload, 1360+16+3*8) == (0x11111111, 0x01000100)
    assert prepare(source, work/'state', wasm_sha=sha) == flash
    assert flash.read_bytes() == saved and source.read_bytes() == original
    view, samples, play_metrics = replay(flash, work/'cold',
        '900:pot:760,1200:12,1290:12^,4400:12,4490:12^', 5400)
    assert not view[0] and view[6] == 114 and view[68] == 2, list(view)
    assert max(map(abs, samples)) > 100 and not any(samples[-22050:])
    assert flash.read_bytes() == saved

    browser = None
    if BROWSER_SHOW:
        exported = BROWSER_SHOW.read_bytes()
        flash = prepare(BROWSER_SHOW, work/'browser', wasm_sha=sha)
        _, payload = LAYOUT['newest_show'](flash.read_bytes())
        show = json.loads(exported)
        assert payload == bytes(show['set']+show['rhythms'])
        origins = [payload[1360+151*i+140] for i in range(2)]
        assert origins == [1,2]
        first, second = (replay(flash, work/f'browser-{index}', actions, time)
            for index, actions, time in (
                (0,'900:pot:760,1200:12,1290:12^', 4800),
                (1,'900:pot:760,1200:12,1290:12^,2500:e6:+1', 9400)))
        assert first[0][0] and second[0][0] and first[0][28] == 0 and second[0][28] == 1
        assert all(max(map(abs, frames)) > 100 for _,frames,_ in (first,second))
        assert BROWSER_SHOW.read_bytes() == exported
        browser = dict(originalSHA256=digest(exported), twoAttachmentsByteExact=True,
                       origins=origins, liveSlotCue=True, metrics=[first[2],second[2]])

    print(json.dumps(dict(imageSHA256=digest(IMAGE.read_bytes()), validatorSHA256=sha,
        validatedShowByteExact=True, invalidInputsRefused=True, legacyJSONByteExact=True,
        journalGeneration=generation, savedTempo=114, rhythmMode=2,
        nativeSave=save_metrics, coldReplay=play_metrics, browserExport=browser), indent=2))
