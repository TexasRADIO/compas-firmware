"""Bind a committed-source rebuild to its app, validator and offline evidence."""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import re
import runpy
import subprocess
import tarfile

FORMAT='compas-offline-candidate-1'
REQUIRED_SOURCES=('src/engine.c','board/board.c','board/fm1.c','board/store.c',
                  'web/board-bench.c','tools/board-target.sh','tools/board-browser.py')
GENERATED=('score.inc','panel-score.inc','board-grooves.inc','board/piano.inc',
           'board/set.inc','board/727.inc','board/727.pack','board/727.json')


def sha(data):return hashlib.sha256(data).hexdigest()
def canonical(data):return json.dumps(data,sort_keys=True,separators=(',',':')).encode()
def artifact(path):
    data=path.read_bytes();return {'sha256':sha(data),'bytes':len(data)}


def source_tree(repo,snapshot):
    source=json.loads((snapshot/'source-inventory.json').read_text())
    commit=source['commit']
    if not re.fullmatch('[0-9a-f]{40}',commit):raise ValueError('Expected a full source commit')
    archive=subprocess.check_output(['git','-C',str(repo),'archive','--format=tar',commit,'side-projects/compas-fm1'])
    if sha(archive)!=source['archiveSHA256'] or archive!=(snapshot/'source.tar').read_bytes():
        raise ValueError('Snapshot archive differs from the Git commit')
    prefix='side-projects/compas-fm1/';files={}
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        for item in tar.getmembers():
            if item.isdir():continue
            if not item.isfile() or not item.name.startswith(prefix):raise ValueError('Unexpected source archive member')
            name=item.name[len(prefix):]
            if name in files or '..' in Path(name).parts:raise ValueError('Unsafe or repeated source path')
            files[name]=sha(tar.extractfile(item).read())
    if files!=source['files']:raise ValueError('Source inventory differs from archive')
    project=snapshot/'source/side-projects/compas-fm1'
    for name,digest in files.items():
        if sha((project/name).read_bytes())!=digest:raise ValueError('Rebuild source changed: '+name)
    return project,{'commit':commit,'archiveSHA256':sha(archive),'files':files,
                    'filesSHA256':sha(canonical(files))}


def resources(project,variant):
    prefix='compas-store' if variant=='store' else 'compas'
    sections=(project/'build/board'/f'{prefix}.sections').read_text()
    sizes={m[1]:int(m[2],16) for m in re.finditer(r'^\s*\d+\s+(\.\S+)\s+([0-9a-f]+)',sections,re.M)}
    names=('.ram_text','.data','.bss','.pool','.noinit')
    stack=json.loads(subprocess.check_output(['python3',str(project/'tools/board-stack.py'),str(project/'build/board'),variant],text=True))
    return {'ramSections':{name:sizes[name] for name in names},
            'staticRamBytes':sum(sizes[name] for name in names),'stack':stack}


def check_audio_display(receipt,xip_sha):
    if receipt.get('imageSHA256')!=xip_sha:raise ValueError('Audio receipt belongs to a different app')
    if receipt.get('durationMs',0)<138000 or receipt.get('strictCallbacks',0)<47000 or any(receipt.get(k)!=0 for k in ('deadlines','lateHalves','rails')):
        raise ValueError('Full audio rehearsal is incomplete or failing')
    def number(value):return type(value) in (int,float) and math.isfinite(value) and value>=0
    irq=[receipt.get(k) for k in ('peakIrqWindowUs','irqUnmeasuredAllowanceUs','deadlineUs')]
    if not all(map(number,irq)) or irq[0]+irq[1]>=irq[2]:raise ValueError('Audio deadline fails')
    for gate in ('R1','R2','R3'):
        values=receipt.get(gate,{})
        if not all(number(values.get(k)) for k in ('computeMs','transferMs','completeMs','pixels')):
            raise ValueError('Missing display timing: '+gate)
    if receipt['R1']['computeMs']>4 or receipt['R1']['pixels']>6000 or receipt['R1']['completeMs']>12 or receipt['R2']['completeMs']>40 or receipt['R3']['computeMs']>40 or receipt['R3']['completeMs']>120:
        raise ValueError('Display budget fails')


def verify_binding(inventory,xip,wasm,rehearsal):
    """Consume a caller-reviewed inventory; this is not a signature verifier."""
    if inventory.get('format')!=FORMAT or inventory.get('releaseReady') is not False:
        raise ValueError('Expected an offline candidate inventory')
    source=inventory.get('source',{});files=source.get('files',{})
    if (not re.fullmatch('[0-9a-f]{40}',source.get('commit','')) or
        not re.fullmatch('[0-9a-f]{64}',source.get('archiveSHA256','')) or
        any(k not in files for k in REQUIRED_SOURCES) or
        any(not isinstance(v,str) or not re.fullmatch('[0-9a-f]{64}',v) for v in files.values()) or
        source.get('filesSHA256')!=sha(canonical(files))):
        raise ValueError('Incomplete source inventory')
    for name,blob in (('xip',xip),('wasm',wasm)):
        if inventory.get('artifacts',{}).get(name)!={'sha256':sha(blob),'bytes':len(blob)}:
            raise ValueError('Candidate inventory mismatch: '+name)
    if inventory.get('evidence',{}).get('audioReceiptSHA256')!=sha(rehearsal):
        raise ValueError('Candidate inventory mismatch: rehearsal')
    check_audio_display(json.loads(rehearsal),sha(xip))
    if inventory.get('checks',{}).get('committedSourceUnchanged') is not True or inventory['checks'].get('loaderMatchesFullTransaction') is not True:
        raise ValueError('Candidate inventory lacks rebuild checks')
    return source['commit']


def build(args):
    project,source=source_tree(args.repo,args.snapshot)
    board=project/'build/board'
    artifacts={name:artifact(project/path) for name,path in {
        'xip':'build/board/emu-store.xip','wasm':'build/board/browser-board.wasm',
        'browser':'build/Compas-Sonidero-Board.html','regularElf':'build/board/compas-offline.elf',
        'storageElf':'build/board/compas-store.elf'}.items()}
    audio=args.audio_receipt.read_bytes();receipt=json.loads(audio)
    check_audio_display(receipt,artifacts['xip']['sha256'])
    loader=json.loads((board/'first-show-wire-prewrite/loader-target.json').read_text())
    full_bytes=args.loader_receipt.read_bytes();full=json.loads(full_bytes)
    if loader['profile']!='first-show-wire' or loader['loadSHA256']!=full['profile']['loadSHA256'] or full['installedBoot']['xipSHA256']!=artifacts['xip']['sha256']:
        raise ValueError('Rebuilt loader/app differs from full transaction evidence')
    checks=full.get('wireChecks',{})
    if not all(checks.get(k) is True for k in ('sameLoaderDifferentShow','wrongAppZeroWrites','corruptBodyZeroWrites')) or full.get('alreadyCompleteWrites')!=0:
        raise ValueError('Loader transfer checks missing')
    fresh=json.loads((board/'first-show-wire-prewrite/loader-emu.json').read_text())
    if len(fresh.get('prewriteChecks',[]))!=2 or any(c.get('operations')!=0 or c.get('noAppWritePassRequested') is not True for c in fresh['prewriteChecks']):
        raise ValueError('Reproduced loader refusal checks failed')
    bsp=json.loads((project/'board/BSP.json').read_text())['files']
    for name,digest in bsp.items():
        if sha((args.bsp/name).read_bytes())!=digest:raise ValueError('BSP drift: '+name)
    samples={}
    for row in json.loads((project/'board/727-sources.json').read_text())['recordings']:
        digest=sha((args.studio/'public/bigbeat/audio/collection'/(row['sampleId']+'.wav')).read_bytes())
        if digest!=row['processedSha256']:raise ValueError('Sample bank drift')
        samples[row['sampleId']]=digest
    toolchain={}
    for line in (board/'toolchain.sha256').read_text().splitlines():
        digest,name=line.split();relative=name.removeprefix('/opt/jieli/')
        if relative==name or sha((args.jieli/relative).read_bytes())!=digest:raise ValueError('PI32 toolchain drift')
        toolchain[relative]=digest
    toolchain['wasm-clang']=artifact(args.wasm_clang)
    toolchain['wasm-ld']=artifact(args.wasm_clang.parent/'wasm-ld')
    result={'format':FORMAT,'releaseReady':False,'source':source,'artifacts':artifacts,
        'inventoryGenerator':artifact(Path(__file__)),
        'generated':{name:artifact(project/'build'/name) for name in GENERATED},
        'dependencies':{'bsp':bsp,'samples':samples,'toolchain':toolchain,
            'containerImage':loader['containerImage']},
        'resources':{'regular':resources(project,'offline'),'storage':resources(project,'store'),
            'loader':{'loadedBytes':loader['loadedBytes'],'zeroedBytes':loader['zeroedBytes'],
                      'freeBytes':loader['loaderRamFreeBytes'],
                      'stack':json.loads((board/'first-show-wire-prewrite/stack.json').read_text())}},
        'loaderLoadSHA256':loader['loadSHA256'],
        'evidence':{'audioReceiptSHA256':sha(audio),'fullLoaderReceiptSHA256':sha(full_bytes),
            'freshLoaderReceiptSHA256':sha((board/'first-show-wire-prewrite/loader-emu.json').read_bytes()),
            'audio':{k:receipt[k] for k in ('durationMs','strictCallbacks','peakIrqWindowUs','irqUnmeasuredAllowanceUs','deadlineUs','R1','R2','R3')}},
        'checks':{'committedSourceUnchanged':True,'loaderMatchesFullTransaction':True},
        'openGates':['release review','OTA packaging and boot','current physical identity/state','hardware validation','musician acceptance']}
    verify_binding(result,(board/'emu-store.xip').read_bytes(),(board/'browser-board.wasm').read_bytes(),audio)
    if full.get('packagedLoader') is not None:
        checker=runpy.run_path(str(Path(__file__).with_name('board-loader-package.py')))
        packed=checker['check_receipt'](full,loader['loadSHA256'],artifacts['xip']['sha256'])
        result['packagedLoader']={k:packed[k] for k in ('wrapperSHA256','wrapperBytes','loadSHA256','loadedBytes')}
        result['openGates'][1]='physical OTA boot/recovery'
    with args.output.open('x') as out:json.dump(result,out,indent=2);out.write('\n')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('repo','snapshot','bsp','jieli','studio','wasm-clang','audio-receipt','loader-receipt'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('output',type=Path);a=p.parse_args()
    try:r=build(a)
    except (ValueError,OSError,KeyError,TypeError,subprocess.CalledProcessError) as e:p.exit(1,str(e)+'\n')
    print(json.dumps({'sourceCommit':r['source']['commit'],'artifacts':r['artifacts'],'resources':r['resources']},indent=2))


if __name__=='__main__':main()
