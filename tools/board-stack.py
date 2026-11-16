"""Conservative direct-call stack bound for the linked PI32 audio path.

Combine each function's maximum DWARF CFA offset with its largest direct
callee. This overcounts tail calls where the compiler has released a frame.
Assembly IRQ entry is checked separately. Fatal IRQ nesting is out of scope.
"""
import json
from pathlib import Path
import re
import subprocess
import sys

folder = Path(sys.argv[1])
variant=sys.argv[2] if len(sys.argv)>2 else "offline"
assert variant in ("offline","store")
prefix="compas-store" if variant=="store" else "compas"
elf = folder / ("compas-"+variant+".elf")
frames = subprocess.check_output(["xcrun", "dwarfdump", "--debug-frame", str(elf)],
                                 stderr=subprocess.DEVNULL, text=True)
symbols = {}
for line in (folder / (prefix+".sections")).read_text().splitlines():
    match = re.match(r"^([0-9a-f]{8})\s+\w\s+F\s+\.(?:text|ram_text)\s+([0-9a-f]+)\s+(\S+)$", line)
    if match:
        symbols[int(match[1], 16)] = (match[3], int(match[2], 16))
cfa = {}
for entry in re.split(r"\bFDE\b", frames)[1:]:
    address = re.search(r"pc=([0-9a-f]+)\.\.\.", entry)
    if address:
        cfa[int(address[1], 16)] = max([0] + [
            int(n) for n in re.findall(r"DW_CFA_def_cfa_offset:\s*\+?(\d+)", entry)])
disasm = (folder / (prefix+".disassembly")).read_text()
code = disasm.split("\ncompas_packed727:", 1)[0]
if variant=="store":
    code += "\n"+disasm.split("Disassembly of section .ram_text:",1)[1]
instructions = {}
current = None
for line in code.splitlines():
    match = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2}(?: [0-9a-f]{2})*)\s+(.*)", line)
    if match:
        current = int(match[1], 16)
        instructions[current] = match[3]
    elif current is not None and line.startswith(" ") and "<" in line:
        instructions[current] += " " + line.strip()
address_by_name = {name: addr for addr,(name,_) in symbols.items()}

resolved_far={}
def far_target(name,body,index):
    # The pinned BSP fl_far is an identity function obscuring the long-range
    # address from the compiler. Resolve only this audited instruction shape.
    expected={"fm1_cstart":"fl_jedec_ram","store_read":"fl_read_ram",
              "store_erase":"fl_erase4k_ram","store_program":"fl_prog_ram",
              "usb_supported_flash":"fl_jedec_ram","ota_erase":"fl_erase4k_ram",
              "ota_fread":"fl_read_ram","ota_prog":"fl_prog_ram"}
    if variant!="store" or name not in expected: raise ValueError("unknown indirect caller: "+name)
    identity=address_by_name["fl_far"]
    assert [text for addr,text in instructions.items() if identity<=addr<identity+symbols[identity][1]]==[
        "sp += -4","[sp] = r0","r0 = [sp]","sp += 4","rts"]
    register=re.fullmatch(r"call (r\d+)",body[index])[1]
    window=body[max(0,index-8):index]
    far=next(i for i,t in enumerate(window) if re.search(r"call .*<fl_far :",t))
    target=expected[name]
    assert far>0
    load=window[far-1]
    if name=="ota_prog" and re.fullmatch(r"r0 = r(?:[4-9]|1[0-3])",load):
        # The program loop keeps the approved callee address in one saved register.
        register_with_target=load.split()[-1]
        assignments=[t for t in body if re.match(register_with_target+r" (?:[+*/&|^-]?=)",t)]
        assert len(assignments)==1
        load=assignments[0].replace(register_with_target,"r0",1)
    assert re.search(r"r0 = \d+ <"+target+r" :",load), (name,load)
    assert not any(re.search(r"\bcall\b|\bgoto\b|\bif\b",t) for t in window[far+1:])
    assigned=[t for t in window[far+1:] if re.match(register+r" (?:[+*/&|^-]?=)",t)]
    if register!="r0": assert window[far+1]==register+" = r0"
    assert assigned==([] if register=="r0" else [register+" = r0"])
    resolved_far[name]=target
    return target

def bound(name, seen=()):
    if name in seen:
        raise ValueError("recursive call: " + name)
    address = address_by_name[name]
    own = cfa[address]
    size = symbols[address][1]
    direct = set()
    body = [line for addr,line in instructions.items() if address<=addr<address+size]
    for index,line in enumerate(body):
        target = re.search(r"<[^<>]+:\s*([0-9a-f]+)\s*>", line)
        if re.search(r"\bcall\b", line):
            if not target:
                direct.add(far_target(name,body,index))
            else:
                direct.add(symbols[int(target[1],16)][0])
        if re.search(r"\bgoto\b", line):
            # A goto can tail-call another function. Ignore local branch destinations.
            if target:
                addr = int(target[1],16)
                if addr in symbols and addr != address:
                    direct.add(symbols[addr][0])
            elif re.search(r"goto\s+r\d+", line):
                if not any("<.GJT" in instruction for instruction in body):
                    raise ValueError("unresolved goto in " + name + ": " + line)
    return own + max([0] + [bound(child,seen+(name,)) for child in sorted(direct)])

stub = code.split("\nisr_alnk0:\n",1)[1].split("\n\n",1)[0]
assert "[--sp] = {psr, rets, reti}" in stub and "[--sp] = {r3-r0}" in stub
assert "sp += -4" in stub and len(re.findall(r"\[--sp\]",stub)) == 2 and re.search(r"call .*<fm1_alnk0_irq :",stub)
roots = {name: bound(name) for name in ("fm1_alnk0_irq", "fm1_cstart", "cp_render")}
print(json.dumps({"softwareIrqEntryBytes":32,"audioPathBytes":roots["fm1_alnk0_irq"]+32,
                  "foregroundCPathBytes":roots["fm1_cstart"], "resolvedFarCalls":resolved_far, "roots":roots},indent=2))
