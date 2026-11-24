"""Prepare private raw XIP emulator input and decode TIMER4/stack measurements."""
from pathlib import Path
import json
import re
import struct
import sys
import wave

folder=Path(sys.argv[1])
variant=sys.argv[2]
if variant not in ("offline","stress","sweep","compound","piano","golondrina","demo","master","store","rehearsal","regional","rack-editor","conga","show-slots"):
    raise SystemExit("Expected offline, stress, piano, golondrina, demo, master, store, rehearsal or regional")
image_variant=variant if variant in ("stress","store","sweep","compound") else "offline"
elf=folder/f"compas-{image_variant}.elf"
sections=folder/("compas"+(f"-{image_variant}" if image_variant!="offline" else "")+".sections")
symbols={}
for line in sections.read_text().splitlines():
    if line.strip():
        words=line.split()
        if re.fullmatch(r"[0-9a-f]{8}",words[0]):
            symbols[words[-1]]=int(words[0],16)
watermark=symbols["compas_supervisor_watermark"]
stats=symbols["board_stats"]
base=min(watermark,stats)
words_needed=(max(watermark+4,stats+40)-base)//4
if sys.argv[3]=="prepare":
    raw=elf.read_bytes()
    assert raw[:6]==b"\x7fELF\x01\x01"
    h=struct.unpack_from("<16sHHIIIIIHHHHHH",raw)
    off,size,count,names=h[6],h[11],h[12],h[13]
    rows=[struct.unpack_from("<10I",raw,off+i*size) for i in range(count)]
    table=rows[names]
    strings=raw[table[4]:table[4]+table[5]]
    items={}
    for idx,t,flags,addr,start,length,*_ in rows:
        items[strings[idx:].split(b"\0",1)[0].decode()]=(addr,start,length)
    addr,start,length=items[".text"]
    assert addr==0x02000120
    image=bytearray(b"\xff"*0x120+raw[start:start+length])
    # A store build must end before the saved show at flash 0x90000 (XIP
    # 0x8C000). A release build (COMPAS_NO_SHOW_STORE: store_boot is gone)
    # uses the BB1 slot instead, which covers 0x90000-0x93000 and is never
    # written: 0x120 + 0x8DFBC, as in board-install-plan.py.
    # A 1.3 release (COMPAS_UNIT_STORE) keeps its A/B show at 0x91000-0x92FFF,
    # so it must end before flash 0x91000 (XIP 0x8D000).
    limit=(0x91000-0x4000 if "compas_unit_store_end" in symbols else
           0x90000-0x4000 if "store_boot" in symbols else 0x120+0x8DFBC)
    if variant in ("store","sweep","compound"):
        _,start,length=items[".ram_text"]
        load=symbols["_rt_load"]-0x02000000
        assert length and load>=len(image) and load+length<=limit
        image.extend(b"\xff"*(load-len(image)))
        image.extend(raw[start:start+length])
    else:
        assert not items[".ram_text"][2]
    # Initialized RAM follows the linked code in XIP. The USB/Sideman build
    # now has a nonzero .data section; fm1_cstart copies it from _data_load.
    _,start,length=items[".data"]
    if length:
        load=symbols["_data_load"]-0x02000000
        assert load>=len(image) and load+length<=limit
        image.extend(b"\xff"*(load-len(image)))
        image.extend(raw[start:start+length])
    (folder/("emu-"+variant+".xip")).write_bytes(image)
    print(hex(base),words_needed)
else:
    if sys.argv[3]=="--native-ram":
        words=struct.unpack_from(f"<{words_needed}I",Path(sys.argv[4]).read_bytes(),base-0x01C00000)
        emulator="fm1-live v0.4.0, FM1_STRICT=1 cycle-cost mode"
    else:
        words=[]
        for line in Path(sys.argv[3]).read_text().splitlines():
            row=re.match(r"\s*0x([0-9a-f]+): ([\da-f ]+)$",line)
            if row and int(row[1],16)==base+4*len(words):
                words.extend(int(s,16) for s in row[2].split())
        emulator="fm1-emu v0.4.0 batch, timed LCD"
    assert len(words)==words_needed
    stat_index=(stats-base)//4
    words=[words[(watermark-base)//4],*words[stat_index:stat_index+10]]
    keys=("supervisorWatermarkBytes","callbacks","maxTicks","deadlines","lateHalves",
          "railSamples","chokeCuts","rackPeak","sequencedPCMPeak","totalPCMPeak","maxIrqTicks")
    assert len(words)==len(keys) and words[1]>0
    result=dict(zip(keys,words))
    assert words[10]>words[2] and words[10]+240<128*544
    result.update(maxCallbackUs=words[2]/24, maxIrqWindowUs=words[10]/24,
                  irqUnmeasuredAllowanceUs=10, deadlineUs=128*544/24,variant=variant,
                  emulator=emulator)
    assert not any(words[i] for i in (3,4,5))
    if variant=="stress":
        assert words[9]==10 and words[7]==8 and words[8]>=2
    if variant=="master":
        with wave.open(str(folder/"master/work/master.wav"),"rb") as wav:
            assert (wav.getnchannels(),wav.getsampwidth(),wav.getframerate())==(2,2,44100)
            frames=wav.getnframes()
            pcm=wav.readframes(frames)
        # The recorder starts on the first DMA half. Align its tail with
        # the run end and sample clear windows between panel actions.
        offset=3650*44100//1000-frames
        assert 0<=offset<44100
        peaks={}
        for phase,start,end in (
            ("boot",450,1300),("mutedPlay",1490,1610),("masterUp",1820,2130),
            ("masterDown",2350,2650),("masterRestored",2920,3310),("stop",3500,3620),
        ):
            a=start*44100//1000-offset
            b=end*44100//1000-offset
            assert 0<=a<b<=frames
            peaks[phase]=max(abs(sample) for sample, in struct.iter_unpack("<h",pcm[4*a:4*b]))
        assert not any(peaks[key] for key in ("boot","mutedPlay","masterDown","stop"))
        assert peaks["masterUp"]>0 and peaks["masterRestored"]>0
        result["stereoPeakByStage"]=peaks
    print(json.dumps(result,indent=2))
