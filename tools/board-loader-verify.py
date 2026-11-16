"""Verify the linked PI32 recovery loader's RAM layout and record its digest."""
import hashlib
import json
from pathlib import Path
import struct
import sys


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def verify(folder, image, preserve_show=False):
    origin, end = 0x01C0A800, 0x01C1E800
    blob = (folder / "loader.elf").read_bytes()
    require(blob[:6] == b"\x7fELF\x01\x01", "Expected ELF32 little endian")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", blob)
    require(header[1:4] == (2, 0xF1, 1) and header[4] == origin,
            "Expected a PI32 executable at the SPL RAM entry")
    _, section_size, count, names = header[10:]
    rows = [struct.unpack_from("<10I", blob, header[6] + i * section_size) for i in range(count)]
    string_table = rows[names]
    strings = blob[string_table[4]:string_table[4] + string_table[5]]
    alloc = {}
    for name, kind, flags, address, offset, size, *_ in rows:
        if not (flags & 2 and size):
            continue
        label = strings[name:].split(b"\0", 1)[0].decode()
        require(origin <= address < address + size <= end,
                f"Allocated section {label} exceeds the loader RAM window")
        alloc[label] = (kind, address, offset, size)
    require(set(alloc) == {".text", ".bss"}, f"Unexpected allocated sections: {alloc}")
    kind, address, offset, text_size = alloc[".text"]
    bss_kind, bss_address, _, bss_size = alloc[".bss"]
    require(kind == 1 and address == origin and bss_kind == 8 and bss_address >= origin + text_size,
            "Expected the loaded image before the zeroed BSS")
    text = blob[offset:offset + text_size]
    require(b"FELUCCA-LOADER-1" in text[:64] and b"ota-COMPAS-FIXTURE\0" in text,
            "Missing SPL loader signature or bench-only identity")
    require(b"ota-FM-1_900\0" not in text, "Unexpected live update identity")
    symtab = next(row for row in rows if row[1] == 2)
    symstr = rows[symtab[6]]
    names = blob[symstr[4]:symstr[4] + symstr[5]]
    symbols = {}
    for location in range(symtab[4], symtab[4] + symtab[5], symtab[9]):
        name, address, size = struct.unpack_from("<III", blob, location)
        symbols[names[name:].split(b"\0", 1)[0].decode()] = (address, size)
    bss_end = bss_address + bss_size
    require(symbols["_bss_start"][0] == bss_address and symbols["_bss_end"][0] == bss_end,
            "Startup clear symbols do not cover the whole BSS")
    rx, rx_size = symbols["ota_dec"]
    require(rx_size > 0 and bss_address <= rx < rx + rx_size <= bss_end,
            "USB receive scratch lies outside startup clear bounds")
    require(symbols["_stack_top"][0] == 0x01C70000 and
            symbols["_sstack_top"][0] == 0x01C72000,
            "Loader stack tops differ from the retained BSP")
    first_show=(folder/"firmware/loader/loader-provision.h").exists()
    wire=(folder/"provision-transfer.c").exists()
    if wire:
        require("fixture_original" not in symbols and "fixture_payload" not in symbols,
                "Transfer profile still contains embedded show inputs")
        received, received_size = symbols["provision_inputs"]
        require(received_size == 16064 and bss_address <= received < received + received_size <= bss_end,
                "Received show buffer must be entirely within startup-cleared BSS")
    bsp = json.loads(Path("board/BSP.json").read_text())
    return {
        "scope": "linked RAM loader inspection with synthetic USB identity",
        "appWriteWindow": [0x4000,0x90000 if preserve_show else 0x93000],
        "profile": "first-show-wire" if wire else "first-show-fixture" if first_show else "preserve-show-offline" if preserve_show else "legacy-app-only",
        "provisionSourceSHA256": ({name:hashlib.sha256((folder/name).read_bytes()).hexdigest() for name in (("provision.c","provision.h","provision-transfer.c","provision-transfer.h","firmware/loader/loader-provision.h") if wire else ("provision.c","provision.h","fixture.h","firmware/loader/loader-provision.h"))} if first_show else None),
        "compiledCoreSHA256": hashlib.sha256((folder/"firmware/loader/ldr_core.c").read_bytes()).hexdigest(),
        "bsp": bsp["source"],
        "containerImage": image,
        "toolchain": (folder / "toolchain.sha256").read_text().splitlines(),
        "sourceSHA256": {
            name: hashlib.sha256(Path(name).read_bytes()).hexdigest()
            for name in ("board/loader-policy.patch", "board/loader-target.patch",
                         "board/loader-flash-policy.h", "board/loader-show-policy.patch")
        },
        "elfSHA256": hashlib.sha256(blob).hexdigest(),
        "loadSHA256": hashlib.sha256(text).hexdigest(),
        "entry": hex(origin),
        "loadedBytes": text_size,
        "zeroedBytes": bss_size,
        "usbReceiveScratchAddress": hex(rx),
        "usbReceiveScratchBytes": rx_size,
        "loaderRamEnd": hex(bss_end),
        "loaderRamLimit": hex(end),
        "loaderRamFreeBytes": end - bss_end,
        "flashRangeTest": (folder / "ranges.txt").read_text().strip(),
    }


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1]), sys.argv[2], len(sys.argv)>3 and sys.argv[3]=="1"), indent=2))
