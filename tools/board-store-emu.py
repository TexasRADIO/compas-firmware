"""Verify the Compás save journal on a synthetic NOR image via fm1-live."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

board = Path(__file__).resolve().parents[1] / "build/board"
emulator = Path(sys.argv[1]).resolve()
assert hashlib.sha256(emulator.read_bytes()).hexdigest() == (
    "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
), "Expected fm1-live v0.4.0 for Apple Silicon"
image = board / "emu-store.xip"
symbols = (board / "compas-store.sections").read_text().splitlines()
captures, size = next((int(row.split()[0],16)-0x01C00000, int(row.split()[-2],16))
                      for row in symbols if row.endswith(" captures"))
view = next(int(row.split()[0],16)-0x01C00000 for row in symbols if row.endswith(" view"))
rhythm_address, rhythm_size = next((int(row.split()[0],16)-0x01C00000, int(row.split()[-2],16))
                      for row in symbols if row.endswith(" rhythm_slots"))
show_size=size+rhythm_size
slot = 15 * (size // 16)
a, b = 0x90000, 0x91000
seed = bytearray(b"\xff" * (1 << 20))
seed[0x92000:0x92008] = b"CMPASIM1"
save_tejano = "850:7,950:7^,1060:20,1140:20^,1270:e0:+9,1560:9,1660:9^"
save_piano = "850:7,950:7^,1060:22,1140:22^,1280:8,1360:8^,1450:22,1530:22^,1610:7,1690:7^,1750:e0:+20,2070:9,2170:9^"
recall = "850:7,950:7^,1060:29,1160:29^"


def record(data, offset):
    magic, generation, crc = struct.unpack_from("<4sII", data, offset)
    length=size if magic==b"CPS1" else show_size if magic==b"CPS2" else 0
    payload = data[offset + 256:offset + 256 + length]
    if length and zlib.crc32(struct.pack("<I", generation) + payload) == crc:
        return generation, payload
    return None


def run(work, flash, keys, cut=None, panel=None, duration=2600, strict=False):
    env = {key: value for key, value in os.environ.items() if not key.startswith("FM1_")}
    env.update(FM1_BENCH_KEYS=keys, FM1_DUMPRAM=str(work / "ram.bin"))
    if strict:
        env.update(FM1_STRICT="1", FM1_BENCH_WAV=str(work / "audio.raw"))
    if cut is not None:
        env["FM1_FLASH_CUT"] = str(cut)
    else:
        env.pop("FM1_FLASH_CUT", None)
    if panel is not None:
        env["FM1_BENCH_PANEL"] = str(panel)
    response = subprocess.run(
        [str(emulator), "--bin", str(image), "--entry", "0x02000120",
         "--midi", "none", "--flash", str(flash), "--bench", str(duration)],
        cwd=work, env=env, capture_output=True, text=True, check=True, timeout=25,
    )
    return response.stdout, (work / "ram.bin").read_bytes()


with tempfile.TemporaryDirectory(prefix="compas-save-", dir=board) as name:
    work = Path(name)
    flash = work / "virtual.bin"
    flash.write_bytes(seed)
    output, _ = run(work, flash, save_tejano, panel=board / "store-save.ppm")
    original = flash.read_bytes()
    generation, first = record(original, a)
    assert generation == 1 and first[slot:slot+19] == b"TEJANO CUMBIA".ljust(19,b"\0")
    assert record(original, b) is None and original[:a] == seed[:a] and original[b:] == seed[b:]
    _, ram = run(work, flash, recall)
    assert ram[captures:captures+size] == first[:size] and ram[view+6] == 105
    assert ram[rhythm_address:rhythm_address+rhythm_size] == first[size:]

    output, _ = run(work, flash, save_piano, panel=board / "store-save-updated.ppm")
    latest = flash.read_bytes()
    generation, second = record(latest, b)
    assert generation == 2 and record(latest, a) == (1, first)
    assert latest[:b] == original[:b] and latest[b+4096:] == original[b+4096:]
    assert second[slot:slot+19] == b"LA PALOMA".ljust(19,b"\0") and second != first
    assert second[size-2:size] == bytes((1,1)), "Expected La Paloma with guide in slot 16"
    _, ram = run(work, flash, recall, panel=board / "store-recall.ppm")
    assert ram[captures:captures+size] == second[:size] and ram[view+6] == 88

    cut_results = []
    for cut in range(1,18):
        flash.write_bytes(original)
        output, _ = run(work, flash, save_piano, cut)
        torn = flash.read_bytes()
        assert f"power cut at op {cut}" in output
        assert torn[:b] == original[:b] and torn[b+4096:] == original[b+4096:]
        assert record(torn, b) is None
        _, ram = run(work, flash, recall)
        assert ram[captures:captures+size] == first[:size] and ram[view+6] == 105
        assert ram[rhythm_address:rhythm_address+rhythm_size] == first[size:]
        cut_results.append(cut)

    damaged = bytearray(latest)
    damaged[b+256+slot+19] ^= 1
    flash.write_bytes(damaged)
    assert record(damaged, b) is None
    _, ram = run(work, flash, recall)
    assert ram[captures:captures+size] == first[:size] and ram[view+6] == 105
    assert ram[rhythm_address:rhythm_address+rhythm_size] == first[size:]

    unprovisioned = b"\xff" * len(seed)
    flash.write_bytes(unprovisioned)
    output, _ = run(work, flash, save_tejano)
    assert "flash: 0 erases / programs" in output and flash.read_bytes() == unprovisioned

with tempfile.TemporaryDirectory(prefix="compas-import-", dir=board) as name:
    work = Path(name)
    show = work / "from-builder.json"
    flash = work / "imported.bin"
    show.write_text(json.dumps({"format": 1, "songs": [
        {"title": "MERENGUE START", "study": "merengue", "bpm": 138, "kit": True, "section": "lift"},
        {"title": "PALOMA SPOT", "study": "la-paloma", "bpm": 97, "guide": True},
        {"title": "TEJANO FINALE", "study": "tejano", "bpm": 111},
    ]}))
    compiler = [sys.executable, str(Path(__file__).with_name("board-set.py")), "--virtual-flash"]
    subprocess.run([*compiler, str(show), str(flash)], check=True, capture_output=True)
    imported = flash.read_bytes()
    generation, payload = record(imported, a)
    assert generation == 1 and payload[3*(size//16):3*(size//16)+1] == b"\x7f"
    retry = subprocess.run([*compiler, str(show), str(flash)], capture_output=True)
    assert retry.returncode and flash.read_bytes() == imported
    show.write_text('{"format": 1, "songs": [{"title": "BAD TEMPO", "study": "mx", "bpm": 0}]}')
    invalid = work / "invalid.bin"
    rejected = subprocess.run([*compiler, str(show), str(invalid)], capture_output=True)
    assert rejected.returncode and not invalid.exists()

    _, ram = run(work, flash, "1000:pot:760,1230:12,1320:12^",
                 panel=board / "import-first.ppm")
    assert ram[view] and ram[view+28] == 0 and ram[view+6] == 138 and ram[view+2] == 1
    assert ram[captures:captures+size] == payload
    _, ram = run(work, flash, "1200:e6:+1", panel=board / "import-score.ppm")
    assert ram[view+28] == 1 and ram[view+6] == 97 and ram[view+31] and ram[view+36]
    _, ram = run(work, flash, "1200:e6:+1,1480:e6:+1,1760:e6:+1",
                 panel=board / "import-last.ppm")
    assert ram[view+28] == 2 and ram[view+6] == 111 and flash.read_bytes() == imported
    playback = "1000:pot:760,1230:12,1320:12^,1970:e6:+1,5500:e6:+1"
    log, ram = run(work, flash, playback, duration=9000, strict=True)
    assert "flash: 0 erases / programs" in log and flash.read_bytes() == imported
    assert ram[view] and ram[view+28] == 2 and ram[view+6] == 111
    audio_peak = max(abs(left) for left, _ in struct.iter_unpack("<hh", (work / "audio.raw").read_bytes()))
    stats = next(int(row.split()[0],16)-0x01C00000 for row in symbols if row.endswith(" board_stats"))
    counters = struct.unpack_from("<10I",ram,stats)
    assert audio_peak>100 and counters[0]>2850 and counters[9]>0 and counters[9]+240<128*544
    assert not any(counters[2:5])

    save_imported = "1200:e6:+1,1450:7,1540:7^,1650:e0:+20,2040:9,2130:9^"
    _, ram = run(work, flash, save_imported)
    saved_generation, saved_payload = record(flash.read_bytes(), b)
    assert saved_generation == 2 and saved_payload[:slot] == payload[:slot]
    assert saved_payload[slot:slot+19] == b"PALOMA SPOT".ljust(19,b"\0")
    _, ram = run(work, flash, recall)
    assert ram[view+28] == 15 and ram[view+6] == 97 and ram[view+36]


with tempfile.TemporaryDirectory(prefix="compas-show-import-", dir=board) as name:
    work=Path(name)
    subprocess.run(["node",str(Path(__file__).resolve().parents[1]/"tests/show-flash.mjs"),str(work)],check=True,capture_output=True)
    flash=work/"show-virtual.bin"
    imported=flash.read_bytes();generation,payload=record(imported,a)
    assert generation==1 and len(payload)==show_size
    _,ram=run(work,flash,"1000:pot:760,1230:12,1320:12^",duration=9000,strict=True)
    assert flash.read_bytes()==imported and ram[view] and ram[view+6]==113
    assert ram[captures:captures+size]==payload[:size]
    assert ram[rhythm_address:rhythm_address+rhythm_size]==payload[size:]
    mode=next(int(row.split()[0],16)-0x01C00000 for row in symbols if row.endswith(" rack_sequence"))
    assert ram[mode]==2
    show_counters=struct.unpack_from("<10I",ram,stats)
    assert not any(show_counters[2:5]) and show_counters[9]+240<128*544
    show_peak=max(abs(left) for left,_ in struct.iter_unpack("<hh",(work/"audio.raw").read_bytes()))
    assert show_peak>100
    # SAVE the recalled show, reboot again and compare both complete payloads.
    _,ram=run(work,flash,"1200:7,1290:7^,1500:9,1590:9^")
    gen,saved=record(flash.read_bytes(),b)
    assert gen==2 and saved==payload
    _,ram=run(work,flash,"")
    assert ram[rhythm_address:rhythm_address+rhythm_size]==payload[size:]

print(json.dumps({"virtualOnly": True,"portableShowReboot":True,"showPlaybackCallbacks":show_counters[0],
                  "showPlaybackPeakIrqWindowUs":show_counters[9]/24,"showPlaybackPeakSample":show_peak, "setBytes": size,"showBytes":show_size, "restoredSlot": 16,
                  "restoredBpm": [105, 88], "restoredGuide": True,
                  "retainedPreviousAfterCut": cut_results, "retainedPreviousAfterCorruption": True,
                  "writeWithoutMarker": False, "preparedSetSlots": 3,
                  "emptyImportedSlotsHideFactory": True, "importRefusedOverwrite": True,
                                    "invalidImportCreatedNoFlash": True, "importedSetSaveAndRecall": True,
                  "preparedPlaybackCallbacks": counters[0],
                  "preparedPlaybackPeakIrqWindowUs": counters[9]/24,
                  "preparedPlaybackPeakSample": audio_peak}, indent=2))
