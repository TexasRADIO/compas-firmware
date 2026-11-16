"""Integration: BSP public-seed EN.xip ES.xip EN.fwsc ES.fwsc."""
from pathlib import Path
import importlib.util,sys
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('public_reproduce',root/'tools/board-public-reproduce.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
if len(sys.argv)!=7:raise SystemExit(__doc__)
bsp,seed,en,es,ep,sp=map(Path,sys.argv[1:]);raw=seed.read_bytes()
for lang,image,package in [('en',en,ep),('es',es,sp)]:
 assert m.reproduce(bsp,raw,image.read_bytes())==package.read_bytes(),lang
 print('PASS exact public-seed reproduction',lang)
app=en.read_bytes()
for name,badseed,badapp in [('tampered seed',bytes([raw[0]^1])+raw[1:],app),('bad prefix',raw,b'X'+app[1:]),('oversize app',raw,b'\xff'*0x120+m.PRODUCT.encode()+b'\0'+bytes(581565)),('wrong identity',raw,app.replace(m.PRODUCT.encode(),b'XX-X_00000000'))]:
 try:m.reproduce(bsp,badseed,badapp)
 except ValueError:print('PASS refused',name)
 else:raise AssertionError(name)
