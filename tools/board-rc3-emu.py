"""Measure RC3's 180-BPM, maximum-effects R1 display path on each live page.

Uses the release-layout image and verifies the resulting tempo, page and
effect values from target RAM rather than assuming every encoder edge landed.
"""
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
board = root / "build" / "board"
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('emulator',type=Path)
parser.add_argument('--usb-clock',action='store_true')
parser.add_argument('--study',type=int,default=None,help='boot a named study from an ordinary virtual saved show')
args=parser.parse_args();emulator=args.emulator.resolve();usb_clock=args.usb_clock
studies=[s for s in json.loads((root/'src/score.json').read_text())['studies'] if not s.get('testOnly')]
if args.study is not None:
    assert 0<=args.study<len(studies)
    import runpy
    compiler=runpy.run_path(str(root/'tools/board-set.py'))
    saved=compiler['virtual_flash'](compiler['compile_set']({'format':1,'songs':[
        {'title':studies[args.study]['short'],'study':studies[args.study]['id'],'feel':0}]}),unit_store=True)
assert hashlib.sha256(emulator.read_bytes()).hexdigest() == (
    "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
)
subprocess.run([sys.executable, str(Path(__file__).with_name("board-emu.py")),
                str(board), "store", "prepare"], check=True, capture_output=True)
image = board / "emu-store.xip"
sections = (board / "compas-store.sections").read_text()
symbol = lambda name: int(re.search(r"^([0-9a-f]+).*\s" + name + r"$", sections, re.M)[1], 16) - 0x01c00000
stats_at, display_at = symbol("board_stats"), symbol("compas_display_stats")
fx_size = int(re.search(r"^[0-9a-f]+.*\s([0-9a-f]+)\s+fx$", sections, re.M)[1], 16)
fx_params_at = symbol("fx") + fx_size - 12  # param[5], pad, bpm[2], active, pad[3]

def tap(at, key):
    return [f"{at}:{key}", f"{at + 90}:{key}^"]

# MIX twice enters FX.  K1-K4 set ROOM/ECHO/TIME/REPEAT respectively; ten
# OCT+ taps take GLUE to 100.  These are panel inputs, not test-only commands.
setup = ["900:pot:760"]
# Separate physical detents avoid the scanner dropping synthetic instant jumps.
setup += [f"{1000+i*80}:e2:+1" for i in range(130)]
setup += [*tap(11700, 2), *tap(12000, 2)]
for i in range(130):
    for enc in (2, 3, 4, 5):
        setup.append(f"{12300+i*80+enc*4}:e{enc}:+1")
for index in range(10):
    setup.extend(tap(23000 + index * 200, 1))
setup += tap(25200, 12)

page_actions = {
    "Band": [*tap(25600, 8)],
    "Mixer": [*tap(25600, 2)],
    "Effects": [*tap(25600, 8), *tap(25800, 2), *tap(26000, 2)],
    "Kumbia": [*tap(25600,5),*tap(25900,5),*tap(26200,2),
               *tap(26500,8),*tap(26800,5),*tap(27100,5)],
}
out = Path(tempfile.mkdtemp(prefix="rc3-", dir=board))
print(out, flush=True)

def run(item):
    name, extra = item
    folder = out / name
    folder.mkdir()
    flash_args=[]
    if args.study is not None:
        (folder/'flash.bin').write_bytes(saved)
        flash_args=['--flash',str(folder/'flash.bin')]
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_STRICT="1", FM1_BENCH_KEYS=",".join(setup + extra),
               FM1_NO_PLAY='1',FM1_DUMPRAM=str(folder / "ram.bin"), FM1_BENCH_PANEL=str(folder / "panel.ppm"))
    with (folder / "bench.log").open("w") as log:
        subprocess.run([str(emulator), "--bin", str(image), "--entry", "0x02000120",
                        "--midi", "none", *flash_args, *(['--usb','--shot'] if usb_clock else ['--bench']), "32000"], cwd=folder, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
    ram = (folder / "ram.bin").read_bytes()
    stats = struct.unpack_from("<10I", ram, stats_at)
    display = struct.unpack_from("<12I", ram, display_at)
    view = ram[symbol("view"):symbol("view")+80]
    params = list(ram[fx_params_at:fx_params_at+5])
    # The FX scanner temporarily sets panel.page=MIX inside its call; use
    # the accepted display model so a mid-instruction RAM dump is stable.
    page = ram[symbol("model")]
    assert view[0] and view[6] == 180 and not view[31], (name, list(view[:10]))
    if args.study is not None:
        assert view[1]==args.study and (folder/'flash.bin').read_bytes()==saved
    assert page == {"Band": 0, "Mixer": 1, "Effects": 8,'Kumbia':7}[name], (name, page)
    if name=='Kumbia':assert ram[symbol('view')+78]==1
    assert params == [100, 100, 5, 100, 90], (name, params)
    assert not any(stats[2:5]) and stats[9] + 240 < 128 * 544, (name, stats)
    compute, transfer, complete, pixels = display[:4]
    passed = 0 < compute <= 96000 and 0 < complete <= 288000 and 0 < pixels <= 6000
    gates={name:{'computeMs':display[i]/24000,'transferMs':display[i+1]/24000,
                 'completeMs':display[i+2]/24000,'pixels':display[i+3]}
           for name,i in (('R2',4),('R3',8))}
    cancelled=struct.unpack_from('<4I',ram,symbol('compas_display_superseded'))
    paint=struct.unpack_from('<I',ram,symbol('compas_foreground_paint_ticks'))[0]
    log=(folder/'bench.log').read_text()
    clocks=len(re.findall(r'usb in: +f8\s*$',log,re.M))
    if usb_clock:assert clocks>50,(name,clocks)
    return {"R1": {"computeMs": compute / 24000, "transferMs": transfer / 24000,
                   "completeMs": complete / 24000, "pixels": pixels},
            "peakIrqUs": stats[9] / 24, "deadlineMisses": stats[2], "clips": stats[4],
            "observedStudy":view[1],"observedTempoBpm": view[6], "observedPage": page, "observedFxParams": params,
            "R1Passed": passed,**gates,
            'R2Passed':0<display[6]<=960000,
            'R3Passed':0<display[8]<=960000 and 0<display[10]<=2880000 and 0<display[11]<=57600,
            'superseded':{'count':cancelled[0],'maxTotalPixels':cancelled[1],
                'maxTotalMs':cancelled[2]/24000,'maxLatestStateCompleteMs':cancelled[3]/24000},
            'supersededPassed':cancelled[3]<=2880000,
            'maxPaintPassMs':paint/24000,'paintPassPassed':paint<=480000,'usbClockPackets':clocks}

with ThreadPoolExecutor(max_workers=3) as pool:
    pages = dict(zip(page_actions, pool.map(run, page_actions.items())))
receipt = {"imageSHA256": hashlib.sha256(image.read_bytes()).hexdigest(), "tempoBpm": 180,
           'study':None if args.study is None else studies[args.study]['id'],
           'usbClockEnabled':usb_clock,
           "effects": {"room": 100, "echo": 100, "time": "1/2", "repeat": 90, "glue": 100},
           "pages": pages, "measurement": "Completed frames R1/R2/R3, plus interrupted full-render traffic and latest-state completion; inputs use panel controls."}
(out / "rc3.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
assert all(page['R1Passed'] and page['R2Passed'] and page['R3Passed'] and page['supersededPassed'] and page['paintPassPassed'] for page in pages.values()), "Display budget exceeded; see retained receipt"
