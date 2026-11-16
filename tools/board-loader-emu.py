"""Exercise the exact linked Compás loader over headless emulated USB."""
import argparse
import array
import hashlib
import runpy
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys


BOARD_BUILD = Path(__file__).resolve().parents[1] / "build/board"
RAM_BASE = 0x01C00000
# Place the probe outside the app window so a fresh emulator can load it
# from the same NOR image after the application has changed.
PROBE_OFFSET = 0x94000
PROBE_ENTRY = 0x02000000 + PROBE_OFFSET - 0x4000
IDENTITY = b"ota-COMPAS-FIXTURE".ljust(27, b"\0")
UPGRADE = bytes.fromhex("f02224357ff7")
EMU_SHA256 = "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def repack(data, source_bits, dest_bits):
    value = bits = 0
    packed = bytearray()
    for byte in data:
        value |= byte << bits
        bits += source_bits
        while bits >= dest_bits:
            packed.append(value & ((1 << dest_bits) - 1))
            value >>= dest_bits
            bits -= dest_bits
    return packed, value


def frame(command, body=b""):
    raw = bytes((0, 0x59, command)) + len(body).to_bytes(3, "little") + body
    raw += bytes((~sum(body) & 255,))
    data, tail = repack(raw, 8, 7)
    if len(raw) * 8 % 7:
        data.append(tail)
    return b"\xf0" + data + b"\xf7"


def decode(wire):
    require(wire[:1] == b"\xf0" and wire[-1:] == b"\xf7" and all(b < 128 for b in wire[1:-1]),
            "Invalid emulator USB SysEx frame")
    data, tail = repack(wire[1:-1], 7, 8)
    require(not tail and len(data) >= 7 and data[:2] == b"\0\x59" and
            len(data) == 7 + int.from_bytes(data[3:6], "little") and
            data[-1] == ~sum(data[6:-1]) & 255, "Invalid emulator OTA response")
    return data[2], bytes(data[6:-1])


def reply(offset, data):
    return frame(0x30, b"\0" + struct.pack("<I", offset) + len(data).to_bytes(3, "little") + data)


def run(emulator, image, folder, original_flash, sends, duration, cut=None, finishing=False):
    folder.mkdir()
    (folder / "work").mkdir()
    flash = folder / "flash.bin"
    flash.write_bytes(original_flash)
    ram = folder / "ram.bin"
    script = folder / "usb.txt"
    script.write_text(";".join(f"{ms}:{wire.hex()}" for ms, wire in sends))
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_BENCH_KEYS="", FM1_DUMPRAM=str(ram), FM1_USB_SEND="@" + str(script))
    if cut is not None:
        env["FM1_FLASH_CUT"] = str(cut)
    command = [str(emulator), "--bin", str(image), "--entry", hex(PROBE_ENTRY), "--midi", "none",
               "--flash", str(flash), "--usb", "--shot", str(duration)]
    result = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True, timeout=300)
    log = result.stdout + result.stderr
    operations = re.search(r"flash: (\d+) erases / programs(?:, power cut at op (\d+))?", log)
    summary = "\n".join(line for line in log.splitlines() if "usb out:" not in line)
    require(result.returncode == 0 and ram.exists() and operations,
            "Loader probe did not complete\n" + summary[-2200:])
    detach = [int(ms) for ms in re.findall(r"\[\s*(\d+) ms\] usb: Detached", log)]
    require(log.count("usb: Configured {") == 1 and
            (not detach or (finishing and len(detach)==1 and detach[0]>sends[-1][0])),
            "Unexpected USB reconnect or early detach\n" + summary[-2200:])
    require(operations[2] == (str(cut) if cut else None), "Unexpected cut state: " + operations[0])
    received = []
    for ms, payload in re.findall(r"\[\s*(\d+) ms\] usb in: +([0-9a-f ]+)", log):
        received.append((int(ms), *decode(bytes.fromhex(payload))))
    return received, ram.read_bytes(), flash.read_bytes(), int(operations[1])


def refuse(emulator, image, folder, original_flash, sends, duration):
    messages, ram, current, operations = run(emulator, image, folder, original_flash, sends, duration)
    require(not operations and current == original_flash, f"Refusal changed flash in {folder.name}")
    return messages, ram


def read_requests(messages):
    result = []
    for ms, command, body in messages:
        if command == 0x30:
            require(len(body) == 8 and body[0] == 0, "Invalid loader read request")
            result.append((ms, *struct.unpack_from("<IH", body, 1)))
    return result


APP_START, APP_END = 0x4000, 0x93000
SHOW_SEED = None
BOARD_VARIANT = "rehearsal"
SHOW_LAYOUT = runpy.run_path(str(Path(__file__).with_name("board-show-layout.py")))
FLASH_ENTRY = 0x400
FINISH = 0xF0000000
RECORDS = (0xE4F00, 0xE8F00)


def bb1_replies(installer, logical, wanted):
    # The BB1 server consumes wire requests and produces the reply bytes.
    # A prefix used for power-cut injection still needs a virtual finish
    # to end the host loop. That extra reply is never sent to the emulator.
    complete = wanted if wanted[-1][0] == FINISH else wanted + [(FINISH, 8)]
    requests = [frame(0x30, b"\0" + struct.pack("<I", offset) + length.to_bytes(3,"little")).hex()
                for offset, length in complete]
    runner = Path(__file__).with_name("board-bb1-serve.mjs")
    result = subprocess.run(["node", str(runner), str(installer)],
                            input=json.dumps({"image":logical.hex(),"requests":requests}),
                            capture_output=True, text=True, timeout=30)
    require(result.returncode == 0, "BB1 response loop failed: " + result.stderr)
    replay = json.loads(result.stdout)
    return [bytes.fromhex(data) for data in replay["replies"][:len(wanted)]], replay["pins"]


def transfer_schedule(logical, sectors=None, installer=None, provision_reads=()):
    wanted = [(0, 512), (512, 512), (FLASH_ENTRY + APP_START, 32)]
    wanted += [(FLASH_ENTRY + offset, 512) for offset in range(0, APP_END, 512)]
    wanted += list(provision_reads)
    wanted += [(FLASH_ENTRY + offset, 512)
               for offset in range(APP_START, APP_END if sectors is None else APP_START + sectors * 4096, 512)]
    if sectors is None:
        wanted.append((FINISH, 8))
    bb1, pins = bb1_replies(installer, logical, wanted) if installer else (None, None)
    sends = [(240, frame(0x11)), (360, UPGRADE)]
    when = 460
    app_pass = 3 + APP_END // 512 + len(provision_reads)
    for i, (offset, length) in enumerate(wanted):
        data = b"success\0" if offset == FINISH else logical[offset:offset + length]
        require(len(data) == length, "Fixture data is shorter than the requested range")
        expected = reply(offset, data)
        if bb1 is not None:
            require(bb1[i] == expected, f"BB1 wire reply differs at request {i}")
        sends.append((when, bb1[i] if bb1 is not None else expected))
        last_page = i >= app_pass and (i - app_pass) % 8 == 7
        when += 120 if last_page else 45
    # Snapshot shortly after cleanup. The headless emulator may step far
    # enough to see the expected USB detach during the final delay.
    return sends, wanted, sends[-1][0] + (115 if sectors is None else 700), pins


def boot_installed(work, name, emulator, installed, pack, fixture_key, xip, board_build, *, package=None):
    # Check the recovered app bytes in either mode. Package entry leaves
    # decryption and the app mapping to the emulator's package reader.
    area = bytearray(installed[APP_START:APP_END])
    pack.sfc(area, 0, len(area), 0, fixture_key)
    entry_offset = 0x120
    recovered = b"\xff" * entry_offset + area[entry_offset:len(xip)]
    require(recovered == xip, "Installed application did not decode to the rehearsed XIP")

    folder = work / name
    folder.mkdir()
    image, flash, ram, audio, bench = [folder / name for name in
        ("app.xip", "flash.bin", "ram.bin", "audio.raw", "bench.json")]
    if package is None:
        image.write_bytes(recovered)
        # Raw entry fetches through the NOR file, so use a plaintext mapping.
        flash_bytes = installed[:APP_START] + area + installed[APP_END:]
        entry = ["--bin", str(image), "--entry", "0x02000120"]
        scope = "direct app entry from decrypted virtual NOR readback"
        entry_hash = {"plaintextMappingSHA256": digest(flash_bytes)}
    else:
        flash_bytes = installed
        entry = ["--fwsc", str(package.resolve())]
        scope = "emulator package entry with encrypted synthetic NOR"
        entry_hash = {"packageSHA256": digest(package.read_bytes())}
    flash.write_bytes(flash_bytes)
    duration = 5100
    keys = "1000:pot:760,1400:12,1510:12^,4000:12,4110:12^"
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_STRICT="1", FM1_BENCH_KEYS=keys, FM1_DUMPRAM=str(ram),
               FM1_BENCH_WAV=str(audio), FM1_BENCH_JSON=str(bench))
    command = [str(emulator), *entry, "--flash", str(flash), "--midi", "none",
               "--bench", str(duration)]
    result = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True, timeout=60)
    log = result.stdout + result.stderr
    require(result.returncode == 0, scope + " failed\n" + log[-2200:])
    require(flash.read_bytes() == flash_bytes, scope + " changed the NOR fixture")
    operations = re.search(r"flash: (\d+) erases / programs", log)
    require(operations and int(operations[1]) == 0, "Expected no NOR operations during app entry")
    emu_tool = Path(__file__).with_name("board-emu.py")
    metrics = json.loads(subprocess.check_output(
        [sys.executable, str(emu_tool), str(board_build), BOARD_VARIANT, "--native-ram", str(ram)],
        text=True))
    require(metrics["callbacks"] >= 1400, "Installed app did not sustain playback callbacks")
    samples = array.array("h", audio.read_bytes())
    if sys.byteorder != "little":
        samples.byteswap()
    left, right = samples[::2], samples[1::2]
    require(left == right, "Installed app stereo output differs between channels")
    emulated_ms = json.loads(bench.read_text())["emulatedMs"]
    offset = emulated_ms * 44100 // 1000 - len(left)
    require(0 <= offset < 44100, "Unexpected installed-boot PCM capture origin")

    def peak(start, end):
        lo, hi = (ms * 44100 // 1000 - offset for ms in (start, end))
        require(0 <= lo < hi <= len(left), "Installed-boot PCM window is outside the capture")
        return max(map(abs, left[lo:hi]))

    peaks = {"beforePlay": peak(900, 1300), "playing": peak(1900, 3900),
             "afterStop": peak(4400, 5000)}
    require(peaks["playing"] > 100 and not peaks["beforePlay"] and not peaks["afterStop"],
            "Expected silence, groove playback, and silence after STOP from the installed app")
    if SHOW_SEED:
        generation,payload=SHOW_LAYOUT["newest_show"](installed)
        symbols={row.split()[-1]:int(row.split()[0],16)-RAM_BASE for row in
                 (board_build/'compas-store.sections').read_text().splitlines()
                 if row.split() and re.fullmatch(r'[0-9a-f]{8}',row.split()[0])}
        memory=ram.read_bytes()
        require(memory[symbols['captures']:symbols['captures']+1360]==payload[:1360],
                "Installed storage app did not recover saved song rows")
        require(memory[symbols['rhythm_slots']:symbols['rhythm_slots']+2416]==payload[1360:],
                "Installed storage app did not recover rhythm/performance rows")
        metrics['recoveredShowGeneration']=generation
        metrics['recoveredShowPayloadSHA256']=digest(payload)
    return {
        "scope": scope,
        "xipSHA256": digest(recovered),
        "installedNorSHA256": digest(installed),
        **entry_hash,
        "flashOperations": 0,
        "pcmPeaks": peaks,
        "keyScript": keys,
        "timing": metrics,
    }


def rehearsed_xip(board_build, receipt):
    path = board_build / ("emu-"+BOARD_VARIANT+".xip")
    require(path.is_file() and (board_build / ("compas-store.sections" if SHOW_SEED else "compas.sections")).is_file(),
            f"Missing the rehearsed XIP or linked sections in {board_build}")
    xip = path.read_bytes()
    expected = json.loads(receipt.read_text())["imageSHA256"]
    require(digest(xip) == expected and xip[:0x120] == b"\xff" * 0x120,
            "Expected the exact rehearsed application image")
    return xip


def bb1_storage_seed(seed, installer, probe_bytes):
    runner = Path(__file__).with_name("board-bb1-serve.mjs")
    result = subprocess.run(["node", str(runner), str(installer), "--layout"],
                            capture_output=True, text=True, timeout=10)
    require(result.returncode == 0, "BB1 storage layout could not be verified: " + result.stderr)
    layout = json.loads(result.stdout)
    regions = layout["regions"]
    # BB1 read-device-evidence.mjs reads the 240 KiB sound library here.
    regions.append({"id":"sound-library","base":0xA0000,"bytes":0x3C000})
    writes = [(APP_START, APP_END), (PROBE_OFFSET, PROBE_OFFSET+probe_bytes)]
    writes.extend((record & ~0xFFF, (record & ~0xFFF)+0x1000) for record in RECORDS)
    for region in regions:
        lo, length = region["base"], region["bytes"]
        hi = lo+length
        require(0 <= lo < hi <= len(seed) and not any(lo < end and start < hi for start, end in writes),
                f"BB1 protected region overlaps another fixture window: {region['id']}")
        writes.append((lo, hi))
        for address in range(lo, hi, 32):
            seed[address:min(address+32,hi)] = hashlib.sha256(str(address).encode()).digest()[:min(32,hi-address)]
    return layout


def protected_receipt(layout, before, installed, restored):
    digests = {}
    for region in layout["regions"]:
        lo, hi = region["base"], region["base"]+region["bytes"]
        expected = before[lo:hi]
        require(installed[lo:hi] == expected == restored[lo:hi],
                f"BB1 protected region changed: {region['id']}")
        digests[region["id"]] = digest(expected)
    return {"layout":layout["id"], "regionSHA256":digests}


def transfer(work, emulator, image, seed, xip, prior_xip, prior_build, installer, layout):
    # Reuse the pinned package builder and synthetic key from the portable
    # preflight. The only executable in the envelope is the app under test.
    preflight = runpy.run_path(str(Path(__file__).with_name("board-updater-preflight.py")))
    pack = preflight["packager_at"](work / "tools/fm1pkg_make.py")
    fixture_key = preflight["FIXTURE_KEY"]
    def package(app):
        flash = SHOW_LAYOUT["compact_flash"](pack,app,fixture_key) if SHOW_SEED else pack.flash_image(app, fixture_key)
        package = pack.ufw(flash, b"INERT HOST FIXTURE", "COMPAS-FIXTURE")
        logical = b"".join(package[i:i + 47] for i in range(0, 960, 48)) + package[960:]
        require(logical[FLASH_ENTRY:FLASH_ENTRY + len(flash)] == flash and len(flash) == APP_END,
                "Fixture package layout differs from the target test")
        return logical, flash

    prior_app = prior_xip[0x120:] if prior_xip else b"SYNTHETIC PRIOR APP\0" * 128
    original, old_flash = package(prior_app)
    candidate, new_flash = package(xip[0x120:])
    seed = bytearray(seed)
    seed[:APP_END] = old_flash
    if SHOW_SEED:
        saved=SHOW_SEED.read_bytes()
        SHOW_LAYOUT["newest_show"](saved)
        seed[0x90000:0x93000]=saved[0x90000:0x93000]
    record = bytearray(112)
    record[2:8] = bytes.fromhex("0d5a015a4154")
    struct.pack_into("<H", record, 0, pack.crc16(record[2:80]))
    for offset in (*RECORDS, 0xE5F00, 0xEAF00):
        seed[offset:offset + len(record)] = record

    if SHOW_SEED:
        # The old padded envelope is valid but must be refused before any write.
        raw=pack.ufw(pack.flash_image(xip[0x120:],fixture_key),b"INERT HOST FIXTURE","COMPAS-FIXTURE")
        legacy=b"".join(raw[i:i+47] for i in range(0,960,48))+raw[960:]
        sends=[(240,frame(0x11)),(360,UPGRADE),(460,reply(0,legacy[:512])),(620,reply(512,legacy[512:1024]))]
        refused,_=refuse(emulator,image,work/"legacy-refused",seed,sends,1000)
        require([(a,n) for _,a,n in read_requests(refused)]==[(0,512),(512,512)],"Legacy package was not refused at header")
    first_change = next((offset for offset in range(APP_START, APP_END, 4096)
                         if old_flash[offset:offset + 4096] != new_flash[offset:offset + 4096]), None)
    require(first_change is not None, "The prior and candidate app windows must differ")
    sends, wanted, duration, bb1_pins = transfer_schedule(
        candidate, sectors=(first_change-APP_START)//4096+1, installer=installer)
    cut, _, torn, cut_ops = run(emulator, image, work / "cut", seed, sends, duration, cut=2)
    reads = [(addr, length) for _, addr, length in read_requests(cut)]
    require(reads[:len(wanted)] == wanted, f"Interrupted transfer diverged at request {len(reads)}: {reads[-10:]}")
    end_change = first_change + 4096
    require(torn[:first_change] == seed[:first_change] and torn[end_change:] == seed[end_change:]
            and torn[first_change:end_change] not in
            (seed[first_change:end_change], new_flash[first_change:end_change]),
            "Power cut did not preserve bytes outside the first changed sector")

    def complete(name, logical, initial, expected_app):
        sends, wanted, duration, _ = transfer_schedule(logical, installer=installer)
        frames, _, final, ops = run(emulator, image, work / name, initial, sends, duration, finishing=True)
        reads = [(addr, length) for _, addr, length in read_requests(frames)]
        require(reads == wanted, f"{name} expected {len(wanted)} reads, saw {len(reads)}: {reads[-10:]}")
        expected = bytearray(initial)
        expected[APP_START:APP_END] = expected_app[APP_START:APP_END]
        expected_ops = sum(initial[i:i + 4096] != expected[i:i + 4096]
                           for i in range(APP_START, APP_END, 4096)) * 17
        for offset in RECORDS:
            if initial[offset:offset + len(record)] == record:
                sector = offset & ~0xFFF
                expected[sector:sector + 4096] = b"\xff" * 4096
                expected_ops += 1
        require(final == expected and ops == expected_ops, f"{name}: NOR or operation count mismatch ({ops} vs {expected_ops})")
        return final, ops, len(reads)

    installed, install_ops, install_reads = complete("resume", candidate, torn, new_flash)
    boot = boot_installed(work, "installed-boot", emulator, installed, pack, fixture_key, xip, BOARD_BUILD)
    restored, restore_ops, restore_reads = complete("restore", original, installed, old_flash)
    result = {
        "rehearsedXipSHA256": digest(xip),
        "powerCutAtOperation": 2,
        "tornSectorOffset": first_change,
        "operationsReportedAfterCut": cut_ops,
        "resumeReads": install_reads,
        "resumeEraseAndProgramOperations": install_ops,
        "restoreReads": restore_reads,
        "restoreEraseAndProgramOperations": restore_ops,
        "appAndProtectedFlashVerified": True,
        "installedAppBoot": boot,
        "restoredAppWindowSHA256": digest(restored[APP_START:APP_END]),
    }
    if SHOW_SEED:
        protected=seed[0x90000:0x93000]
        require(installed[0x90000:0x93000]==protected==restored[0x90000:0x93000], "Saved show changed during update/restore")
        result["showPreservation"]={"range":[0x90000,0x93000],"sha256":digest(protected),"legacyEnvelopeRefused":True,
            "compactPackage":SHOW_LAYOUT["inspect_flash"](pack,new_flash,fixture_key,xip[0x120:])}
    if bb1_pins:
        result["bb1ReplyServerSHA256"] = bb1_pins
        result["bb1ProtectedRegions"] = protected_receipt(layout, seed, installed, restored)
        result["bb1RepliedToEmulatedLoaderReads"] = len(wanted) + install_reads + restore_reads
    if prior_xip:
        result["restoredAppBoot"] = boot_installed(
            work, "restored-boot", emulator, restored, pack, fixture_key, prior_xip, prior_build)
    else:
        result["restoredSyntheticAppSHA256"] = result["restoredAppWindowSHA256"]
    return result


def verify(work, emulator, rehearsal, prior_build, prior_rehearsal, installer):
    require(digest(emulator.read_bytes()) == EMU_SHA256, "Expected pinned fm1-live v0.4.0 for Apple Silicon")
    xip = rehearsed_xip(BOARD_BUILD, rehearsal) if rehearsal else None
    prior_xip = rehearsed_xip(prior_build, prior_rehearsal) if prior_rehearsal else None
    receipt = json.loads((work / "loader-target.json").read_text())
    loader = (work / "loader.bin").read_bytes()
    require(digest(loader) == receipt["loadSHA256"], "Emulator loader differs from linked image")
    probe = (work / "entry.bin").read_bytes()
    require(probe.endswith(loader), "Linked probe does not embed the verified loader")
    image = work / "entry.xip"
    image.write_bytes(b"\xff" * (PROBE_OFFSET - 0x4000) + probe)
    flash = bytearray(b"\xff" * 0x100000)
    layout = bb1_storage_seed(flash, installer, len(probe)) if installer else None
    flash[PROBE_OFFSET:PROBE_OFFSET + len(probe)] = probe
    for offset in (0xE4F00, 0xE8F00, 0xEAF00):
        flash[offset:offset + 16] = b"VIRTUAL-KEEP-123"
    cold, ram = refuse(emulator, image, work / "cold", flash, [], 275)
    rx = int(receipt["usbReceiveScratchAddress"], 16) - RAM_BASE
    rx_size = receipt["usbReceiveScratchBytes"]
    ram_end = int(receipt["loaderRamEnd"], 16) - RAM_BASE
    load_start = int(receipt["entry"], 16) - RAM_BASE
    require(not cold and ram[rx:rx + rx_size] == bytes(rx_size) and
            ram[ram_end:ram_end + 32] == bytes([0xA5]) * 32 and
            ram[load_start:load_start + len(loader)] == loader,
            "Cold boot did not copy the loader or clear its receive scratch")

    query = frame(0x11)
    bad_header = bytes(512)
    malformed, _ = refuse(emulator, image, work / "malformed", flash,
                       [(240, query), (360, UPGRADE), (460, reply(0, bad_header)),
                        (620, reply(512, bad_header)), (800, query), (900, UPGRADE)], 1050)
    bad_reads = read_requests(malformed)
    require([(addr, length) for _, addr, length in bad_reads] == [(0, 512), (512, 512), (0, 512)] and
            900 <= bad_reads[-1][0] < 1000 and
            [body for _, cmd, body in malformed if cmd == 0x11] == [IDENTITY, IDENTITY],
            f"Malformed package did not return to USB polling: {malformed}")

    timeout, _ = refuse(emulator, image, work / "timeout", flash,
                     [(240, query), (360, UPGRADE), (470, UPGRADE),
                      (15500, query), (15800, UPGRADE)], 16000)
    read_times = read_requests(timeout)
    require([(addr, length) for _, addr, length in read_times] == [(0, 512)] * 4 and
            [body for _, cmd, body in timeout if cmd == 0x11] == [IDENTITY, IDENTITY] and
            360 <= read_times[0][0] < 470 and 15800 <= read_times[-1][0] < 15950 and
            all(4900 <= b[0] - a[0] <= 5100 for a, b in zip(read_times[:2], read_times[1:3])),
            f"Timeout did not stop until a fresh start: {timeout}")
    result = {
        "scope": "linked PI32 loader, synthetic XIP boot shim, headless emulated USB and NOR",
        "emulatorSHA256": EMU_SHA256,
        "linkedLoadSHA256": receipt["loadSHA256"],
        "bootstrapSHA256": digest(probe),
        "coldBootClearedPoisonedReceiveBytes": rx_size,
        "usbIdentity": IDENTITY.rstrip(b"\0").decode(),
        "malformedHeaderAndExplicitRestartReadsMs": [ms for ms, _, _ in bad_reads],
        "timeoutAndExplicitRestartReadsMs": [ms for ms, _, _ in read_times],
        "flashOperationsInRefusalRuns": 0,
    }
    if rehearsal:
        result["transfer"] = transfer(work, emulator, image, flash, xip, prior_xip, prior_build, installer, layout)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    parser.add_argument("emulator", type=Path)
    parser.add_argument("--rehearsal", type=Path)
    parser.add_argument("--restore-board", type=Path)
    parser.add_argument("--restore-rehearsal", type=Path)
    parser.add_argument("--bb1-installer", type=Path)
    parser.add_argument("--preserve-show", type=Path, help="use compact layout and preserve this virtual show")
    args = parser.parse_args()
    if args.preserve_show:
        if not args.rehearsal: parser.error("--preserve-show needs --rehearsal")
        APP_END=0x90000;SHOW_SEED=args.preserve_show.resolve();BOARD_VARIANT="store"
    if args.bb1_installer and not args.rehearsal:
        parser.error("--bb1-installer needs --rehearsal")
    if bool(args.restore_board) != bool(args.restore_rehearsal) or (args.restore_board and not args.rehearsal):
        parser.error("A prior build requires --rehearsal, --restore-board and --restore-rehearsal")
    print(json.dumps(verify(args.work.resolve(), args.emulator.resolve(), args.rehearsal,
                            args.restore_board, args.restore_rehearsal, args.bb1_installer), indent=2))
