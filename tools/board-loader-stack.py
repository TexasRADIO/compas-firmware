"""Conservative DWARF/direct-call stack bound for the pinned RAM loader."""
import json,re,subprocess,sys
from pathlib import Path
folder=Path(sys.argv[1]);text=(folder/'loader.dis').read_text()
symbols={int(m[1],16):(m[3],int(m[2],16)) for m in re.finditer(r'^([0-9a-f]{8})\s+\w\s+F\s+\.text\s+([0-9a-f]+)\s+(\S+)$',text,re.M)}
frames=subprocess.check_output(['xcrun','dwarfdump','--debug-frame',str(folder/'loader.elf')],stderr=subprocess.DEVNULL,text=True)
cfa={}
for block in re.split(r'\bFDE\b',frames)[1:]:
 m=re.search(r'pc=([0-9a-f]+)\.\.\.',block)
 if m:cfa[int(m[1],16)]=max([0]+list(map(int,re.findall(r'DW_CFA_def_cfa_offset:\s*\+?(\d+)',block))))
instructions={};current=None
for line in text.splitlines():
 m=re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]{2}(?: [0-9a-f]{2})*)\s+(.*)',line)
 if m:current=int(m[1],16);instructions[current]=m[3]
 elif current is not None and line.startswith(' ') and '<' in line:instructions[current]+=' '+line.strip()
byname={name:at for at,(name,size) in symbols.items()};cache={}
def bound(name,seen=()):
 if name in cache:return cache[name]
 assert name not in seen,('recursion',name)
 at=byname[name];assert at in cfa,('missing frame',name)
 body=[v for k,v in instructions.items() if at<=k<at+symbols[at][1]];children=set()
 for line in body:
  target=re.search(r'<[^<>]+:\s*([0-9a-f]+)\s*>',line)
  if re.search(r'\bcall\b',line):
   assert target,('indirect call',name,line)
   children.add(symbols[int(target[1],16)][0])
  if re.search(r'\bgoto\b',line):
   if target:
    dest=int(target[1],16)
    if dest in symbols and dest!=at:children.add(symbols[dest][0])
   elif re.search(r'goto\s+r\d+',line):
    assert any('<.GJT' in b for b in body),('unknown indirect branch',name,line)
 result=cfa[at]+max([0]+[bound(child,seen+(name,)) for child in children]);cache[name]=result;return result
roots={name:bound(name) for name in ('ldr_cstart','ldr_main','provision_run') if name in byname}
print(json.dumps({'scope':'C path; loader runs with interrupts off; startup assembly excluded','boundsBytes':roots,'functionsChecked':len(cache)},indent=2))
