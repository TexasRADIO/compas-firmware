"""Prepare pinned synthetic first-show inputs; no device or transport access."""
from pathlib import Path
import os,hashlib,json,runpy,subprocess,zlib
root=Path.cwd();work=Path(__import__('sys').argv[1]);bsp=Path(os.environ['COMP_BSP'])
pins=json.loads((root/'board/BSP.json').read_text())['files']
for name in ['firmware/loader/crt0_ldr.S','firmware/loader/loader.ld','firmware/hal/fm1_cc.h','firmware/hal/fm1_time.h','firmware/hal/fm1_sys.h','firmware/hal/fm1_flash.h','firmware/hal/fm1_xip.h','tools/fm1pkg_make.py']:
 data=(bsp/name).read_bytes();assert hashlib.sha256(data).hexdigest()==pins[name]
 dest=work/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
ld=work/'firmware/loader/loader.ld';ld.write_text(ld.read_text().replace('*(.sbss*) *(COMMON)','*(.sbss*) *(.pool) *(COMMON)'))
for source,dest in [('board/provision.c','provision.c'),('board/provision.h','provision.h'),('tests/board-provision-target.c','fixture.c'),('tests/board-loader-entry.S','entry.S')]:
 (work/dest).write_bytes((root/source).read_bytes())
pre=runpy.run_path(str(root/'tools/board-updater-preflight.py'))
pack=pre['packager_at'](bsp/'tools/fm1pkg_make.py');key=pre['FIXTURE_KEY']
layout=runpy.run_path(str(root/'tools/board-show-layout.py'))
app=(root/'build/board/emu-store.xip').read_bytes();prior=(Path(os.environ['COMP_PROVISION_PRIOR'])/'emu-store.xip').read_bytes()
for blob,name in [(app,'COMP_PROVISION_RECEIPT'),(prior,'COMP_PROVISION_PRIOR_RECEIPT')]:
 assert hashlib.sha256(blob).hexdigest()==json.loads(Path(os.environ[name]).read_text())['imageSHA256']
legacy=pack.flash_image(prior[0x120:],key);compact=layout['compact_flash'](pack,app[0x120:],key)
original=legacy+bytes((i*17+3)&255 for i in range(0x100000-len(legacy)))
seed=original[:0x4000]+compact[0x4000:]+original[0x90000:]
subprocess.run(['node',str(root/'tests/show-flash.mjs'),str(work)],check=True,capture_output=True)
_,payload=layout['newest_show']((work/'show-virtual.bin').read_bytes())
(work/'original.bin').write_bytes(original)
(work/'seed.bin').write_bytes(seed)
plan=runpy.run_path(str(root/'tools/board-show-provision.py'))['prepare'](original,original,seed,legacy,compact,(work/'show-virtual.bin').read_bytes())
(work/'expected.bin').write_bytes(plan.run())
(work/'fixture.h').write_text('#define FIXTURE_HEAD_CRC '+hex(zlib.crc32(seed[:0x4000]))+'u\n#define FIXTURE_APP_CRC '+hex(zlib.crc32(seed[0x4000:0x90000]))+'u\n'+''.join('static const uint8_t '+name+'[] = {'+','.join(map(str,data))+'};\n' for name,data in [('fixture_original',original[0x90000:0x93000]),('fixture_payload',payload)]))
