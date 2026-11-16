"""Verify the Compás application's USB replies and recovery entry offline."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
wire = runpy.run_path(str(root / "tools/board-loader-emu.py"))
identity = (root / "board/usb/identity.h").read_text()
full = re.search(r'#define FELUCCA_ID "([^"]+)"', identity)[1].encode()
family = re.search(r'#define FELUCCA_HANDSHAKE_ID "([^"]+)"', identity)[1].encode()
rom_request = bytes.fromhex("f02224357df7")
rom_at = 0x7FD80
ota_at = 0xE0000
ota_end = 0xE5000


def decode_app_message(message):
    """Keep updater replies distinct from validated application telemetry."""
    if message.startswith(bytes.fromhex("f00022243501")):
        if (len(message) != 19 or message[6] != 2 or message[-1] != 0xF7 or
                any(byte >= 0x80 for byte in message[1:-1])):
            raise ValueError("Invalid Compas Sideman report")
        return None
    if message in (b"\xf8", b"\xfa", b"\xfb", b"\xfc"):
        return None
    return wire["decode"](message)


def run(emulator, image, folder, name, keys, sends, duration, flash_seed=None, cut=None):
    run_dir = folder / name
    (run_dir / "work").mkdir(parents=True)
    flash = run_dir / "flash.bin"
    ram = run_dir / "ram.bin"
    seed = bytes([255]) * (1 << 20) if flash_seed is None else flash_seed
    flash.write_bytes(seed)
    timeline = run_dir / "usb.txt"
    timeline.write_text(";".join(f"{ms}:{msg.hex()}" for ms, msg in sends))
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_STRICT="1", FM1_NO_PLAY="1", FM1_BENCH_KEYS=keys,
               FM1_DUMPRAM=str(ram), FM1_USB_SEND="@" + str(timeline))
    if cut is not None:
        env["FM1_FLASH_CUT"] = str(cut)
    command = [str(emulator), "--bin", str(image), "--entry", "0x02000120", "--mhz", "240", "--midi", "none",
               "--flash", str(flash), "--usb", "--shot", str(duration)]
    result = subprocess.run(command, cwd=run_dir, env=env, capture_output=True, text=True, timeout=180)
    log = result.stdout + result.stderr
    (run_dir / "run.log").write_text(log)
    if result.returncode or not ram.is_file():
        raise RuntimeError(log[-2500:])
    messages = [(int(ms), bytes.fromhex(payload))
                for ms, payload in re.findall(r"\[\s*(\d+) ms\] usb in: +([0-9a-f ]+)", log)]
    replies = [(ms, *decoded) for ms, message in messages
               if (decoded := decode_app_message(message)) is not None]
    (run_dir / "usb-stream.json").write_text(json.dumps(
        [{"ms": ms, "hex": message.hex()} for ms, message in messages], indent=2))
    return log, ram.read_bytes(), flash.read_bytes(), replies


def package_parts(path, stock):
    script = """
import { readFileSync } from 'node:fs';
import { Package, logicalImage, sha256hex } from './web/installer/fm1pkg.js';
import { validateOfficialReturn } from './web/installer/compas-policy.mjs';
const bytes=readFileSync(process.argv[1]);
if (process.argv[2]==='stock') await validateOfficialReturn(bytes);
else if (await sha256hex(bytes)!=='7c3c9524d2b689fb095017a2ee2fb1c1a475a9be3182f5055faf62274d74e367')
  throw new Error('The BB1 input must be the pinned r85 package.');
const pkg=new Package(bytes);
const ota=pkg.entries.find(e=>e.type===100);
const flash=pkg.entries.find(e=>e.type===0);
console.log(JSON.stringify([ota.dataOff, ota.size, flash.dataOff,
  Buffer.from(logicalImage(bytes)).toString('base64')]));
"""
    output = subprocess.check_output(["node", "--input-type=module", "-e", script, str(path),
                                      "stock" if stock else "custom"], cwd=root)
    offset, size, flash, image = json.loads(output)
    return offset, size, flash, base64.b64decode(image)


def stage_rehearsal(emulator, image, home, path, stock):
    """The running application stages a real loader in virtual NOR."""
    ota, size, boot, logical = package_parts(path, stock)
    requests = [(0,512), (512,512), (ota,512)]
    if stock:
        requests.extend((boot+n,512) for n in range(0,0x4000,512))
    requests.append((ota,512))
    requests.extend((ota+32+n, min(512,size-32-n)) for n in range(0,size-32,512))
    expected = requests + [(0xE0000000,8)]
    sends = [(1600,wire["UPGRADE"])]
    sends.extend((1850+120*i,wire["reply"](off,logical[off:off+n])) for i,(off,n) in enumerate(requests))
    sends.append((1850+120*len(requests),wire["reply"](0xE0000000,b"success\0")))
    seed = bytearray([255]) * (1 << 20)
    # The standard release app now extends past 0x90000. Put the
    # unchanged-region sentinel outside the entire application envelope.
    seed[0x93000:0x96000] = bytes([0xA5])*0x3000
    seed[:0x4000] = logical[boot:boot+0x4000]
    seed[ota_at:ota_end] = bytes([255])*(ota_end-ota_at)
    if stock:
        cut_log, _, cut_flash, _ = run(emulator, image, home, "stage-stock-cut", "0:0,0:1",
            sends, sends[-1][0]+1000, bytes(seed), cut=8)
        assert "power cut at op 8" in cut_log
        assert cut_flash[:ota_at] == seed[:ota_at] and cut_flash[ota_end:] == seed[ota_end:]
        assert cut_flash[0xE4F00:0xE4F00+112] == bytes([255])*112
        assert cut_flash[ota_at:ota_end] != seed[ota_at:ota_end]
        seed = cut_flash
    log, ram, flash, replies = run(emulator, image, home, "stage-" + ("stock" if stock else "bb1"),
        "0:0,0:1", sends, sends[-1][0]+1000, bytes(seed))
    observed = [(off,n) for _,off,n in wire["read_requests"](replies)]
    assert observed[:len(expected)] == expected, (path.name, len(observed), observed[-5:])
    assert flash[:ota_at] == seed[:ota_at] and flash[ota_end:] == seed[ota_end:]
    assert flash[ota_at+32:ota_at+size] == logical[ota+32:ota+size], path.name
    record = flash[0xE4F00:0xE4F00+112]
    assert record[2:8] == bytes.fromhex("0d5a015a4154") and struct.unpack_from("<I",record,72)[0] == ota_at
    assert ram[rom_at+8:rom_at+120] == record, path.name
    assert "usb: Detached" in log
    return {"readRequests":len(expected), "stagedBytes":size-32,
            "appAndOtherRegionsUnchanged":True, "durableUpdateRecord":True,
            "powerCutRetried":stock,
            "scope":"running Compás app through loader handoff"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emulator", type=Path)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--stock-v15", type=Path)
    parser.add_argument("--bb1", type=Path)
    args = parser.parse_args()
    emulator = args.emulator.resolve()
    image = (root / "build/board/emu-store.xip").resolve()
    assert hashlib.sha256(emulator.read_bytes()).hexdigest() == wire["EMU_SHA256"]
    section = (root / "build/board/compas-store.sections").read_text()
    stats = int(re.search(r"^([0-9a-f]+).*\sboard_stats$", section, re.M)[1], 16) - 0x01C00000
    view = int(re.search(r"^([0-9a-f]+).*\sview$", section, re.M)[1], 16) - 0x01C00000
    home = args.work.resolve() if args.work else Path(tempfile.mkdtemp(prefix="compas-usb-"))
    query = [(1800, wire["frame"](0x11, b"")), (2000, wire["frame"](0x11, b"\1"))]
    checks = {}
    for name, keys, sends, duration, normal, expects_rom in (
        ("normal", "", query, 2400, True, False),
        ("safe", "0:0,0:1", query, 2400, False, False),
        ("playing", "1000:pot:760,1250:12,1340:12^",
         query+[(2200,wire["UPGRADE"])], 2600, True, False),
        ("direct-rom", "0:0,0:1,0:12", [], 1600, False, True),
        ("normal-to-rom", "", query + [(2200, rom_request)], 2700, True, True),
        ("safe-to-rom", "0:0,0:1", query + [(2200, rom_request)], 2700, False, True),
    ):
        log, ram, flash, replies = run(emulator, image, home, name, keys, sends, duration)
        marker = ram[rom_at:rom_at+16].startswith(b"usb_update_mode\0")
        callbacks = struct.unpack_from("<I", ram, stats)[0]
        if sends:
            assert log.count('usb: Configured { product: "Compas"') == 1, name
            assert expects_rom or "usb: Detached" not in log, name
            assert all(command == 0x11 for _,command,_ in replies), (name, replies)
            assert [payload.rstrip(b"\0") for _, command, payload in replies if command == 0x11] == [family, full], (name, replies)
        assert (callbacks > 0) == normal and marker == expects_rom, (name, callbacks, marker)
        assert bool(ram[view]) == (name == "playing"), name
        assert flash == bytes([255]) * (1 << 20), name
        checks[name] = {"callbacks":callbacks, "romMailbox":marker, "replies":[
            payload.rstrip(b"\0").decode() for _,command,payload in replies if command==0x11]}
    stages = {}
    if args.stock_v15:
        stages["officialV15"] = stage_rehearsal(emulator, image, home, args.stock_v15.resolve(), True)
    if args.bb1:
        stages["bb1R85"] = stage_rehearsal(emulator, image, home, args.bb1.resolve(), False)
    print(json.dumps({"imageSHA256":hashlib.sha256(image.read_bytes()).hexdigest(),
                      "work":str(home), "checks":checks, "otaStages":stages, "physicalUnitTouched":False}, indent=2))


if __name__ == "__main__":
    main()
