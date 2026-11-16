"""Full RAM USB loader plus first-show commit, exclusively synthetic flash."""
import hashlib,importlib.util,json,os,struct,sys,zlib,subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
work,emulator=map(lambda p:Path(p).resolve(),sys.argv[1:])
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('loader_probe',Path(__file__).with_name('board-loader-emu.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.PROBE_OFFSET=0xC0000;m.PROBE_ENTRY=0x020BC000;m.APP_END=0x90000;m.BOARD_VARIANT='store'
assert m.digest(emulator.read_bytes())==m.EMU_SHA256
receipt=json.loads((work/'loader-target.json').read_text());probe=(work/'entry.bin').read_bytes()
assert probe.endswith((work/'loader.bin').read_bytes()) and m.digest((work/'loader.bin').read_bytes())==receipt['loadSHA256']
packaged=None
if os.environ.get('COMP_LOADER_PACKAGE')=='1':
 packaged=json.loads((work/'loader-package.json').read_text())
 assert packaged['stagedDecodedMatchesLinked'] and packaged['loadSHA256']==receipt['loadSHA256']
 assert (work/'staged-loader.bin').read_bytes()==(work/'loader.bin').read_bytes()
 assert probe.endswith((work/'staged-loader.bin').read_bytes())
image=work/'entry.xip';image.write_bytes(b'\xff'*(m.PROBE_OFFSET-0x4000)+probe)
pre=m.runpy.run_path(str(root/'tools/board-updater-preflight.py'));pack=pre['packager_at'](work/'tools/fm1pkg_make.py');key=pre['FIXTURE_KEY']
xip=(root/'build/board/emu-store.xip').read_bytes()
compact=m.SHOW_LAYOUT['compact_flash'](pack,xip[0x120:],key)
raw=pack.ufw(compact,(work/'ota.bin').read_bytes() if packaged else b'INERT HOST FIXTURE','COMPAS-FIXTURE')
logical=b''.join(raw[i:i+47] for i in range(0,960,48))+raw[960:]
record=bytearray(112);record[2:8]=bytes.fromhex('0d5a015a4154');struct.pack_into('<H',record,0,pack.crc16(record[2:80]))
def decorated(data):
 b=bytearray(data);b[m.PROBE_OFFSET:m.PROBE_OFFSET+len(probe)]=probe
 for at in m.RECORDS:b[at:at+len(record)]=record
 return bytes(b)
original=decorated((work/'original.bin').read_bytes());updated=decorated((work/'seed.bin').read_bytes());expected=bytearray(decorated((work/'expected.bin').read_bytes()))
for at in m.RECORDS:expected[at&~4095:(at&~4095)+4096]=b'\xff'*4096
wire=receipt['profile']=='first-show-wire'
installer=Path(os.environ['COMP_BB1_INSTALLER']) if os.environ.get('COMP_BB1_INSTALLER') else None
provision_reads=()
if wire:
 protocol=m.runpy.run_path(str(root/'tools/board-provision-wire.py'))
 baseline=(work/'original.bin').read_bytes();updated_base=(work/'seed.bin').read_bytes()
 def with_show(show):
  return protocol['attach'](logical,baseline,baseline,updated_base,baseline[:0x93000],compact,show)
 served,planned=with_show((work/'show-virtual.bin').read_bytes())
 assert planned==(work/'expected.bin').read_bytes()
 provision_reads=[(0x100000,64)]+[(0x100040+off,min(512,16064-off)) for off in range(0,16064,512)]
else:served=logical
sends,wanted,duration,pins=m.transfer_schedule(served,installer=installer,provision_reads=provision_reads)
def run(name,seed,frames=sends,cut=None,finish=True):
 messages,ram,final,ops=m.run(emulator,image,work/name,seed,frames,duration+1500,cut=cut,finishing=finish)
 print(name+": "+str(ops)+" operations",file=sys.stderr,flush=True)
 return final,ops,messages
prewrite_checks=None
if wire:
 # Separate pre-write refusal from an unchanged app that would merely skip writes.
 header_index=3+m.APP_END//512
 def reject_before_app(name,data,count):
  frames,requests,_,_=m.transfer_schedule(data,installer=installer,provision_reads=provision_reads)
  end=header_index+len(provision_reads)
  messages,_,flash,n=m.run(emulator,image,work/name,original,frames[:end+2],frames[end+1][0]+1000)
  assert [(off,size) for _,off,size in m.read_requests(messages)]==requests[:count]
  assert flash==original and n==0
  return dict(case=name,operations=n,requests=count,applicationInitiallyDifferent=True,noAppWritePassRequested=True)
 bad_body=bytearray(served);bad_body[0x100040+13000]^=1
 bad_app=bytearray(served);bad_app[0x100000+32]^=1
 struct.pack_into('<I',bad_app,0x100000+56,zlib.crc32(bad_app[0x100000:0x100000+56]))
 with ThreadPoolExecutor(max_workers=2) as pool:
  first=pool.submit(reject_before_app,'prewrite-corrupt-body',bad_body,header_index+len(provision_reads))
  second=pool.submit(reject_before_app,'prewrite-wrong-app',bad_app,header_index+1)
  prewrite_checks=[first.result(),second.result()]
 if os.environ.get('COMP_PROVISION_PREWRITE_ONLY')=='1':
  print(json.dumps(dict(scope='pre-write sidecar refusal with older app',profile=receipt,prewriteChecks=prewrite_checks,bb1ReplyServerSHA256=pins),indent=2));sys.exit(0)
# Unknown tail must refuse before even the first app erase, despite valid package.
unknown=bytearray(original);unknown[0x91020]^=1
with ThreadPoolExecutor(max_workers=3) as pool:
 tasks=[pool.submit(run,'unknown-tail',unknown,finish=False),pool.submit(run,'unconfirmed',updated,sends[:-1],finish=False),pool.submit(run,'install',original)]
 bad,refused_ops,_=tasks[0].result();assert bad==unknown and not refused_ops
 bad,refused_ops,_=tasks[1].result();assert bad==updated and not refused_ops
 final,ops,messages=tasks[2].result()
assert final==expected,(ops,'complete NOR mismatch')
assert [(off,n) for _,off,n in m.read_requests(messages)]==wanted
app_ops=sum(original[i:i+4096]!=updated[i:i+4096] for i in range(0x4000,0x90000,4096))*17
assert ops==app_ops+22,(ops,app_ops)
# Exercise erase, payload/header/marker and cleanup boundaries after a completed
# application. Cases above 20 cut staging-record cleanup after commit.
def interrupt(at):
 torn,n,_=run('cut-'+str(at),updated,cut=at,finish=at>20)
 assert torn[:0x90000]==updated[:0x90000]
 assert torn[0x93000:0xE4000]==updated[0x93000:0xE4000]
 assert torn[0xE9000:]==updated[0xE9000:]
 if at<=20:
  assert torn[0x92000:0x92008]!=b'CMPASIM1'
  for rec in m.RECORDS:assert torn[rec:rec+112]==record
 recovered,resume_ops,_=run('resume-'+str(at),torn)
 assert recovered==expected
 return dict(cut=at,attemptedOperations=n,resumeOperations=resume_ops)
with ThreadPoolExecutor(max_workers=4) as pool:
 cuts=list(pool.map(interrupt,(19,20,22) if wire else (1,2,3,4,18,19,20,21,22)))
again,again_ops,_=run('already-complete',final);assert again==final and not again_ops
wire_checks=None
if wire:
 # Every negative transfer uses genuine pinned BB1 host responses to the changed bytes.
 def refused_wire(name,blob):
  frames,_,_,_=m.transfer_schedule(blob,installer=installer,provision_reads=provision_reads)
  result,n,_=run(name,updated,frames,finish=False)
  assert result==updated and not n
  return name
 damaged=bytearray(served);damaged[0x100040+13000]^=1
 mismatched=bytearray(served);mismatched[0x100000+32]^=1
 struct.pack_into('<I',mismatched,0x100000+56,zlib.crc32(mismatched[0x100000:0x100000+56]))
 alternate=work/'alternate';alternate.mkdir()
 subprocess.run(['node',str(root/'tests/show-flash.mjs'),str(alternate),'127'],check=True,capture_output=True)
 alternate_served,alternate_expected=with_show((alternate/'show-virtual.bin').read_bytes())
 alternate_expected=bytearray(decorated(alternate_expected))
 for at in m.RECORDS:alternate_expected[at&~4095:(at&~4095)+4096]=b'\xff'*4096
 alternate_frames,_,_,_=m.transfer_schedule(alternate_served,installer=installer,provision_reads=provision_reads)
 with ThreadPoolExecutor(max_workers=3) as pool:
  bad1=pool.submit(refused_wire,'corrupt-sidecar',damaged)
  bad2=pool.submit(refused_wire,'wrong-app-sidecar',mismatched)
  different=pool.submit(run,'different-show-same-loader',updated,alternate_frames)
  bad1.result();bad2.result();other,n,_=different.result()
 assert other==alternate_expected and other!=expected and n==22
 wire_checks=dict(recordBytes=16128,recordReadRequests=len(provision_reads),corruptBodyZeroWrites=True,wrongAppZeroWrites=True,sameLoaderDifferentShow=True,alternateNorSHA256=m.digest(other))
m.SHOW_SEED=work/'show-virtual.bin'
boot=m.boot_installed(work,'installed-boot',emulator,final,pack,key,xip,root/'build/board')
report=dict(scope='synthetic USB update, provision and recovery cleanup as one linked transaction',profile=receipt,packagedLoader=packaged,packageReadRequests=len(wanted),bb1ReplyServerSHA256=pins,installOperations=ops,appOperations=app_ops,showOperations=20,cleanupOperations=2,unknownTailRefusedBeforeAppWrites=True,unconfirmedShowUnchanged=True,interruptionCases=cuts,alreadyCompleteWrites=again_ops,wireChecks=wire_checks,prewriteChecks=prewrite_checks,installedBoot=boot)
print(json.dumps(report,indent=2))
