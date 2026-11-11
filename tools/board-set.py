"""Compile an offline set list into version-1 Compás song snapshots."""
import argparse
from pathlib import Path
import json
import re
import struct
import zlib

root=Path(__file__).resolve().parents[1]
studies=json.loads((root/"src/score.json").read_text())["studies"]
ids={study["id"]:i for i,study in enumerate(studies)}
scores={entry["id"]:(i,entry) for i,entry in enumerate(
    json.loads((root/"repertoire/board-scores.json").read_text()),1)}
allowed={"title","study","bpm","feel","root","clave","kit","section","phrases","levels","mute","guide","countIn","transpose"}

def number(song,key,default,lo,hi):
    value=song.get(key,default)
    if type(value) is not int or not lo<=value<=hi:
        raise ValueError(f"{song['title']}: {key} must be {lo}–{hi}")
    return value

def compile_set(config):
    if set(config)!={"format","songs"} or type(config["format"]) is not int or config["format"]!=1:
        raise ValueError("Expected format 1 set")
    songs=config["songs"]
    if not isinstance(songs,list) or not 1<=len(songs)<=16:
        raise ValueError("A set must contain 1–16 songs")
    records=[]
    for song in songs:
        if not isinstance(song,dict) or set(song)-allowed:
            raise ValueError("Unknown song fields")
        title=song["title"]
        if not re.fullmatch(r"[A-ZÁÉÍÓÚÑÜ0-9 /+\-]{1,18}",title) or not title.strip():
            raise ValueError("Use 1–18 panel characters in each title")
        piano, score = scores.get(song["study"],(0,{}))
        guide=song.get("guide",False)
        if type(guide) is not bool or (guide and "guide" not in score and "guide_part" not in score):
            raise ValueError(f"{title}: guide is available for a piano score")
        count_in=song.get("countIn",False)
        if type(count_in) is not bool or (count_in and piano):
            raise ValueError(f"{title}: countIn is available for grooves")
        sid=ids["bolero" if piano else song["study"]]
        section=["base","lift","break"].index(song.get("section","base"))
        phrases=number(song,"phrases",0,0,64)
        kit=song.get("kit",False)
        if piano and (section or phrases or kit or any(
                song.get(key, 0) for key in ("root", "clave", "mute")) or
                song.get("levels", [100]*6) != [100]*6):
            raise ValueError(f"{title}: the piano score uses its written form; use transpose for pitch")
        if type(kit) is not bool:
            raise ValueError("kit must be true or false")
        bpm=number(song,"bpm",score["bpm"] if piano else studies[sid]["bpm"],60,180)
        feel=number(song,"feel",0 if piano else studies[sid].get("feel",30),0,0 if piano else 100)
        root_note=number(song,"root",0,0,11 if sid else 0)
        transpose=number(song,"transpose",0,-6 if piano else 0,5 if piano else 0)
        if piano: root_note=transpose%12
        clave=number(song,"clave",0,0,1 if sid in (2,6) else 0)
        mute=number(song,"mute",0,0,31 if not sid else 63)
        levels=song.get("levels",[100]*6)
        if not isinstance(levels,list) or len(levels)!=6 or any(type(x) is not int or not 0<=x<=100 for x in levels):
            raise ValueError("levels must contain six integers from 0 through 100")
        blob=bytearray(55)
        blob[:11]=bytes([67,80,83,49,1,1,sid,bpm,feel,mute,clave])
        struct.pack_into("<I",blob,11,0x434f4d50)
        blob[15:21]=bytes(levels)
        blob[45]=root_note
        struct.pack_into("<I",blob,51,zlib.crc32(blob[:51]))
        panel_title=title.translate(str.maketrans("ÁÉÍÓÚÑÜ","aeiounv"))
        records.append((panel_title, bytes(blob), bytes((int(kit),section,phrases,piano,int(guide if piano else count_in)))))
    return records


def c_source(records):
    rows=[]
    for title, blob, settings in records:
        rows.append("    {"+json.dumps(title)+",{"+",".join(map(str,blob))+"},{0},"+
                    ",".join(map(str,settings))+"},")
    return "static const Song factory_set[] = {\n"+"\n".join(rows)+"\n};\n"


def virtual_flash(records):
    # Mirror BoardStore's packed Song records and first journal generation.
    # DEL in the first title byte hides an unused imported slot. A zero
    # title still means fall back to the factory slot for older saves.
    rows = [title.encode("ascii").ljust(19,b"\0") + blob + bytes(6) + settings
            for title, blob, settings in records]
    rows.extend([b"\x7f" + bytes(84)] * (16-len(rows)))
    payload = b"".join(rows)
    assert len(payload) == 1360
    generation = struct.pack("<I",1)
    record = b"CPS1" + generation + struct.pack("<I",zlib.crc32(generation+payload))
    image = bytearray(b"\xff" * 0x100000)
    image[0x90000:0x90000+len(record)] = record
    image[0x90100:0x90100+len(payload)] = payload
    image[0x92000:0x92008] = b"CMPASIM1"
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?", default=root/"board/set.json")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--virtual-flash", action="store_true",
                        help="create a new emulator-only flash file at output")
    args = parser.parse_args()
    if args.virtual_flash and args.output is None:
        parser.error("--virtual-flash needs an output path")
    try:
        if args.source.stat().st_size > 65536:
            raise ValueError("Set file is too large")
        records = compile_set(json.loads(args.source.read_text()))
        if args.virtual_flash:
            with args.output.open("xb") as out:
                out.write(virtual_flash(records))
        else:
            output = args.output or root/"build/board/set.inc"
            output.parent.mkdir(parents=True,exist_ok=True)
            output.write_text(c_source(records))
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, f"Compás set: {error}\n")


if __name__=="__main__":
    main()
