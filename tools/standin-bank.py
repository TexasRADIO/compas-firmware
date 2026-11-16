"""STAND-IN 727 bank for builds without the Studio WAVs (COMP_STUDIO).

Fifteen deterministic synthetic clips (decaying noise, 4,000 + 300 i frames),
packed with the same lossless encoder as tools/board-bank.py (zero token =
repeat). It is NOT the 727: every artefact built from it is STAND-IN. Golden
frames and the logic/framebuffer tests run on it; audio gates stay on the real
bank. Usage: standin-bank.py [folder]; default build/standin. Writes 727.pcm,
727.pack, 727.inc, 727-reference.inc and browser-bank.c (the browser build's
packed array), never touching the real-bank build in build/board."""
import struct
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
folder=Path(sys.argv[1]) if len(sys.argv)>1 else root/"build/standin"
folder.mkdir(parents=True,exist_ok=True)
frames=[4000+300*i for i in range(15)]
def clip(index,n):
    seed=0x9E3779B9^index; out=[]
    for t in range(n):
        seed=(seed*1103515245+12345)&0x7fffffff
        out.append(int(((seed>>12)%16383-8191)*(n-t)/n))
    return out
pcm=b"".join(struct.pack("<%dh"%n,*clip(i,n)) for i,n in enumerate(frames))
assert sum(frames)*2==len(pcm)
packed=bytearray(); entries=[]; off=0
def flush(n):
    packed.extend((0,n) if n>=3 else (64,)*n)
for n in frames:
    start=len(packed); prev=0; rep=0
    for (v,) in struct.iter_unpack("<h",pcm[off*2:(off+n)*2]):
        d=v-prev
        if not d:
            rep+=1
            if rep==255: packed.extend((0,rep)); rep=0
            continue
        if rep: flush(rep); rep=0
        if -63<=d<=63: packed.append(d+64)
        else: packed.extend((0x8000|(v+16384)).to_bytes(2,"big"))
        prev=v
    if rep: flush(rep)
    entries.append((start,off,n)); off+=n
(folder/"727.pcm").write_bytes(pcm); (folder/"727.pack").write_bytes(packed)
gain="static const uint16_t accent_gain_q12[] = {\n    "+", ".join(["4096"]*15)+"\n};\n"
for name,i in (("727.inc",0),("727-reference.inc",1)):
    (folder/name).write_text("static const Clip clips[] = {\n"+"".join("    {%d, %d},\n"%(e[i],e[2]) for e in entries)+"};\n"+gain)
(folder/"browser-bank.c").write_text("/* STAND-IN 727 bank (tools/standin-bank.py). Not the 727. */\n"+
    "const unsigned char compas_packed727[]={\n"+
    "\n".join(",".join(map(str,packed[i:i+32]))+"," for i in range(0,len(packed),32))+"\n};\n")
print("STAND-IN bank:",len(packed),"packed bytes in",folder)
