"""Run the compiled provisioning core and pinned NOR driver on synthetic flash."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
work,emulator=map(lambda x:Path(x).resolve(),sys.argv[1:])
sha=lambda b:hashlib.sha256(b).hexdigest()
spec=importlib.util.spec_from_file_location('boot_probe',ROOT/'tools/board-loader-emu.py')
boot=importlib.util.module_from_spec(spec);spec.loader.exec_module(boot)
assert sha(emulator.read_bytes())==boot.EMU_SHA256
boot.SHOW_SEED=True;boot.BOARD_VARIANT='store';boot.APP_END=0x90000
raw=(work/'provision.elf').read_bytes();header=struct.unpack_from('<16sHHIIIIIHHHHHH',raw)
assert header[0][:6]==b'\x7fELF\x01\x01' and header[2]==0xf1 and header[4]==0x1c0a800
rows=[struct.unpack_from('<10I',raw,header[6]+i*header[11]) for i in range(header[12])]
strings=raw[rows[header[13]][4]:rows[header[13]][4]+rows[header[13]][5]]
allocated={strings[r[0]:].split(b'\0',1)[0].decode():(r[3],r[5]) for r in rows if r[2]&2 and r[5]}
assert set(allocated)=={'.text','.bss'}
assert all(0x1c0a800<=a<a+n<=0x1c1e800 for a,n in allocated.values())
symbols={line.split()[-1]:int(line.split()[0],16)-0x1c00000 for line in (work/'provision.dis').read_text().splitlines()
         if line.split() and re.fullmatch('[0-9a-f]{8}',line.split()[0])}
assert symbols['_bss_start']+allocated['.bss'][1]==symbols['_bss_end']
assert symbols['_stack_top']==0x70000 and symbols['_sstack_top']==0x72000
probe=(work/'entry.bin').read_bytes()
assert probe.endswith((work/'loader.bin').read_bytes()) and 0xc0000+len(probe)<0xf1000
image=work/'entry.xip';image.write_bytes(b'\xff'*0xbc000+probe)
seed=bytearray((work/'seed.bin').read_bytes());seed[0xc0000:0xc0000+len(probe)]=probe
expected=bytearray((work/'expected.bin').read_bytes());expected[0xc0000:0xc0000+len(probe)]=probe

def run(name,source,cut=None,fail=0,corrupt=0):
    folder=work/name;folder.mkdir();flash=folder/'flash.bin';ram=folder/'ram.bin'
    original=bytearray(source)
    if fail or corrupt:struct.pack_into('<III',original,0xf1000,0x46525043,fail,corrupt)
    else:original[0xf1000:0xf100c]=source[0xf1000:0xf100c]
    flash.write_bytes(original)
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_BENCH_KEYS='',FM1_DUMPRAM=str(ram))
    if cut:env['FM1_FLASH_CUT']=str(cut)
    proc=subprocess.run([str(emulator),'--bin',str(image),'--entry','0x020BC000','--midi','none',
                         '--flash',str(flash),'--bench','12000'],cwd=folder,env=env,capture_output=True,text=True,timeout=90)
    log=proc.stdout+proc.stderr;(folder/'run.log').write_text(log)
    assert proc.returncode==0,log[-2000:]
    m=re.search(r'flash: (\d+) erases / programs(?:, power cut at op (\d+))?',log)
    assert m and m[2]==(str(cut) if cut else None),log[-2000:]
    state=ram.read_bytes();after=flash.read_bytes()
    assert after[:0x90000]==original[:0x90000] and after[0x93000:]==original[0x93000:]
    result={n:struct.unpack_from('<i' if n=='provision_result' else '<I',state,symbols[n])[0]
            for n in ('provision_result','provision_done','provision_reads','provision_erases','provision_programs')}
    assert result['provision_done']==0x43505256,result
    result['flashOperations']=int(m[1])
    return bytes(after),result

complete,normal=run('complete',seed)
assert complete==expected and normal['provision_result']==0 and normal['flashOperations']==20,normal
same,idempotent=run('idempotent',complete)
assert same==complete and idempotent['flashOperations']==0 and idempotent['provision_result']==0
cuts=[]
for n in range(1,21):
    torn,stopped=run('cut-'+str(n),seed,cut=n)
    # The emulator keeps executing the CPU after cutting NOR writes. Later
    # attempted operations can be counted although they cannot change flash.
    assert stopped['provision_result']<0 and n<=stopped['flashOperations']<=20,(n,stopped)
    assert torn[0x92000:0x92008]!=b'CMPASIM1'
    resumed,result=run('resume-'+str(n),torn)
    assert resumed==complete and result['provision_result']==0 and result['flashOperations']==20,result
    cuts.append({'cut':n,'attemptedOperations':stopped['flashOperations'],'result':stopped['provision_result']})
faults=[]
for count in (2,50,66,68):
    for kind in ('fail','corrupt'):
        failed,result=run(kind+'-'+str(count),seed,**{kind:count})
        assert result['provision_result']<0,result
        target=bytearray(complete);target[0xf1000:0xf100c]=failed[0xf1000:0xf100c]
        if failed[0x92000:0x92008]==b'CMPASIM1':assert failed==target
        else:assert result['flashOperations']<20
        faults.append({'read':count,'kind':kind,**result})
wrong=bytearray(seed);wrong[0x5000]^=1
refused,wrong_result=run('wrong-app',wrong)
assert refused==wrong and wrong_result['provision_result']==-1 and wrong_result['flashOperations']==0
unknown=bytearray(seed);unknown[0x91080]^=1
refused,unknown_result=run('unknown-tail',unknown)
assert refused==unknown and unknown_result['provision_result']==-4 and unknown_result['flashOperations']==0
pre=__import__('runpy').run_path(str(ROOT/'tools/board-updater-preflight.py'))
pack=pre['packager_at'](work/'tools/fm1pkg_make.py')
app=(ROOT/'build/board/emu-store.xip').read_bytes()
played=boot.boot_installed(work,'app-boot',emulator,complete,pack,pre['FIXTURE_KEY'],app,ROOT/'build/board')
report={'scope':__doc__,'emulatorSHA256':sha(emulator.read_bytes()),'imageSHA256':sha((work/'loader.bin').read_bytes()),
        'allocated':allocated,'ramFreeBytes':0x1c1e800-max(a+n for a,n in allocated.values()),
        'normal':normal,'idempotent':idempotent,'powerCutResumeOperations':cuts,'readFaults':faults,
        'wrongApp':wrong_result,'unknownTail':unknown_result,'boot':played,
        'coreSourceSHA256':sha((ROOT/'board/provision.c').read_bytes()),
        'fixtureSourceSHA256':sha((ROOT/'tests/board-provision-target.c').read_bytes()),
        'fixtureHeaderSHA256':sha((work/'fixture.h').read_bytes()),'finalFlashSHA256':sha(complete)}
print(json.dumps(report,indent=2))
