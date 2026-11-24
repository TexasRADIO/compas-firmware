"""Keep RC3 stopped for ten emulated minutes, then prove PLAY clears its chase."""
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import struct
import subprocess
import sys
import tempfile

board = Path(__file__).resolve().parents[1] / "build" / "board"
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('emulator',type=Path)
parser.add_argument('--release',action='store_true',help='use the full USB/release-store build')
args=parser.parse_args()
emulator = args.emulator.resolve()
variant='store' if args.release else 'offline'
wire=runpy.run_path(str(Path(__file__).with_name('board-loader-emu.py')))
assert hashlib.sha256(emulator.read_bytes()).hexdigest() == (
    "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
)
subprocess.run([sys.executable, str(Path(__file__).with_name("board-emu.py")),
                str(board), variant, "prepare"], check=True, capture_output=True)
image = board / ('emu-'+variant+'.xip')
sections = (board / ('compas-store.sections' if args.release else 'compas.sections')).read_text()
symbol = lambda name: int(re.search(r"^([0-9a-f]+).*\s" + name + r"$", sections, re.M)[1], 16) - 0x01c00000
view_at, stats_at, idle_at = symbol("view"), symbol("board_stats"), symbol("idle_phase")
out = Path(tempfile.mkdtemp(prefix="rc3-idle-", dir=board))
print(out, flush=True)

def run(item):
    name, duration, keys = item
    folder = out / name
    folder.mkdir()
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_STRICT="1", FM1_BENCH_KEYS=keys, FM1_DUMPRAM=str(folder / "ram.bin"),
               FM1_BENCH_PANEL=str(folder / "panel.ppm"),FM1_NO_PLAY='1')
    if args.release:
        timeline=folder/'usb.txt'
        timeline.write_text(';'.join(f'{ms}:{wire["frame"](0x11,payload).hex()}' for ms,payload in ((1000,b''),(1200,b'\1'),(598000,b''),(598200,b'\1'))))
        env['FM1_USB_SEND']='@'+str(timeline)
    with (folder / "bench.log").open("w") as log:
        subprocess.run([str(emulator), "--bin", str(image), "--entry", "0x02000120",
                        "--midi", "none", *(['--usb','--shot'] if args.release else ['--bench']), str(duration)], cwd=folder, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
    ram = (folder / "ram.bin").read_bytes()
    stats = struct.unpack_from("<10I", ram, stats_at)
    assert not any(stats[2:5]) and stats[9] + 240 < 128 * 544, (name, stats)
    identities=[]
    if args.release:
        for _,payload in re.findall(r'\[\s*(\d+) ms\] usb in: +([0-9a-f ]+)',(folder/'bench.log').read_text()):
            message=bytes.fromhex(payload)
            if message[:1]==b'\xf0' and not message.startswith(bytes.fromhex('f00022243501')):
                cmd,body=wire['decode'](message)
                if cmd==0x11:identities.append(body.rstrip(b'\0').decode())
        assert identities==['FM-1_922','FM-1_92210003']*2,identities
    return {"idlePhase": ram[idle_at], "playing": bool(ram[view_at]),'identities':identities,
            "peakIrqUs": stats[9] / 24, "deadlineMisses": stats[2], "clips": stats[4]}

# At this duration the normal stopped state has run the 4-second delay and
# theatre chase for more than nine minutes.  The second sample is only 40 ms
# after the physical PLAY edge; input debounce and the foreground redraw must
# already have cleared the idle state.
plans = [("tenMinutesStopped", 600000, "900:pot:760"),
         ("playAt40ms", 600040, "900:pot:760,600000:12,600090:12^")]
with ThreadPoolExecutor(max_workers=2) as pool:
    results = dict(zip((name for name, _, _ in plans), pool.map(run, plans)))
assert results["tenMinutesStopped"]["idlePhase"]
assert not results["tenMinutesStopped"]["playing"]
assert not results["playAt40ms"]["idlePhase"] and results["playAt40ms"]["playing"]
receipt = {"imageSHA256": hashlib.sha256(image.read_bytes()).hexdigest(), "durationMs": 600000,
           "stopped": results["tenMinutesStopped"], "playAt40ms": results["playAt40ms"],
           "usbEnabled": args.release, "effectSettings": "default-off",'variant':variant}
(out / "idle.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
