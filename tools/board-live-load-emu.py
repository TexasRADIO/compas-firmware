"""Real-panel maximum FX/Kumbia/727 load, brake and USB/clock contention.

The endpoints prove the accepted values at slow/home sample-clock corners,
then the restored speed and stopped transport after braking. Virtual USB/NOR only.
"""
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import struct
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1];board=root/'build/board'
studies=[s for s in json.loads((root/'src/score.json').read_text())['studies'] if not s.get('testOnly')]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('emulator',type=Path)
parser.add_argument('--study',type=int,default=1,choices=range(len(studies)))
parser.add_argument('--layers',action='store_true',help='enable all four conductor percussion layers before opening the pads')
parser.add_argument('--master-raw',type=int,default=760,choices=range(16,1024),help='physical VOLUME ADC input (1023 = full master)')
parser.add_argument('--kumbia-level',type=int,default=100,choices=range(0,101,5),help='exercise the independent post-drive fader via real K4 detents')
parser.add_argument('--saved-study',action='store_true',help='boot the exact study from an ordinary saved show rather than measuring browser-detent travel')
parser.add_argument('--case',action='append',choices=('maximum','fastClock','brake'),help='run selected cases; default is all three')
args=parser.parse_args();emulator=args.emulator.resolve();selected=args.study
assert hashlib.sha256(emulator.read_bytes()).hexdigest()=='8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a'
subprocess.run([sys.executable,str(root/'tools/board-emu.py'),str(board),'store','prepare'],check=True,capture_output=True)
image=board/'emu-store.xip';sections=(board/'compas-store.sections').read_text()
def symbol(name):return int(re.search(r'^([0-9a-f]+).*\s'+name+r'$',sections,re.M)[1],16)-0x1c00000
wire=runpy.run_path(str(root/'tools/board-usb-emu.py'))
fx_size=int(re.search(r'^[0-9a-f]+.*\s([0-9a-f]+)\s+fx$',sections,re.M)[1],16)
def tap(t,k):return [f'{t}:{k}',f'{t+70}:{k}^']
seed=b'\xff'*(1<<20)
if args.saved_study:
    compiler=runpy.run_path(str(root/'tools/board-set.py'))
    seed=compiler['virtual_flash'](compiler['compile_set']({'format':1,'songs':[
        {'title':studies[selected]['short'],'study':studies[selected]['id'],'feel':0}]}),unit_store=True)
    selection=[]
elif selected==1:
    selection=['800:e6:+1']
else:
    # Enter after boot initialization, then exercise the ordinary rack
    # browser with bounded, spaced detents. A RAM probe verifies the target;
    # an early REC previously left the scanner on the song browser instead.
    remaining=selected;detents=[]
    while remaining:
        step=min(10,remaining);detents.append(step);remaining-=step
    selection=tap(1500,13)+[f'{1700+i*200}:e6:+{step}' for i,step in enumerate(detents)]+tap(2500,8)
setup=selection+[f'900:pot:{args.master_raw}']+[f'{1000+i*80+enc*4}:e{enc}:+1' for i in range(130) for enc in (2,3)]
setup+=tap(11700,2)+tap(12000,2)
setup += [f'{12300+i*80+enc*4}:e{enc}:+1' for i in range(130) for enc in (2,3,4,5)]
for i in range(10):setup+=tap(23000+i*200,1)
setup+=tap(25200,5)+tap(25500,5)+tap(25800,2)
setup += [f'{26100+i*80+enc*4}:e{enc}:+1' for i in range(130) for enc in (2,3,4)]
for i in range(54):setup+=tap(26100+i*160,0)
for i in range((100-args.kumbia_level)//5):setup.append(f'{35000+i*80}:e5:-5')
setup+=tap(37000,8)+tap(37300,13)+tap(37600,12)
if args.layers:
    for key in (8,10,13,15):setup+=tap(37100,14+key)
for at in range(38000,44500,160):
    for sound in (0,2,4,5,6,7,9,11,13,14):setup+=tap(at,14+sound)
brake=tap(45000,8)+tap(45200,2)+[f'{45500+i*100}:e5:-1' for i in range(40)]
# Test FRENO with a separate detent after the speed sweep has settled. An
# instantaneous scripted sweep can coalesce an edge during the page transfer;
# reaching the floor and asking for the brake are two distinct gestures.
brake += [f'{t}:e5:-1' for t in (50000,51000,52000,53000,54000)]
out=Path(tempfile.mkdtemp(prefix='live-load-',dir=board));print(out,flush=True)
def run(case):
    name,duration,extra=case;folder=out/name;folder.mkdir();flash=folder/'flash.bin';flash.write_bytes(seed)
    keys=[a.replace(':0',':1',1) if name=='fastClock' and 26100<=int(a.split(':',1)[0])<34800 and a.split(':')[1] in ('0','0^') else a for a in setup]
    sends=[(40000,wire['wire']['frame'](0x11,b'')),(42000,wire['wire']['frame'](0x11,b'\1')),(42500,wire['wire']['UPGRADE'])]
    script=folder/'usb.txt';script.write_text(';'.join(f'{t}:{b.hex()}' for t,b in sends))
    env={k:v for k,v in os.environ.items() if not k.startswith('FM1_')}
    env.update(FM1_STRICT='1',FM1_NO_PLAY='1',FM1_BENCH_KEYS=','.join(keys+extra),FM1_USB_SEND='@'+str(script),FM1_DUMPRAM=str(folder/'ram.bin'))
    with (folder/'bench.log').open('w') as log:
        subprocess.run([str(emulator),'--bin',str(image),'--entry','0x02000120','--midi','none','--flash',str(flash),'--usb','--shot',str(duration)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
    ram=(folder/'ram.bin').read_bytes();v=ram[symbol('view'):symbol('view')+88];stats=struct.unpack_from('<11I',ram,symbol('board_stats'))
    params=list(ram[symbol('fx')+fx_size-12:symbol('fx')+fx_size-7]);log=(folder/'bench.log').read_text()
    messages=[(int(t),bytes.fromhex(h)) for t,h in re.findall(r'\[\s*(\d+) ms\] usb in: +([0-9a-f ]+)',log)]
    identities=[];side=[]
    for t,msg in messages:
        decoded=wire['decode_app_message'](msg)
        if decoded is not None:identities.append(decoded[1].rstrip(b'\0').decode())
        elif len(msg)==19:side.append({'ms':t,'bpm':msg[11]*128+msg[12],'playing':msg[14]})
    clocks=[t for t,m in messages if m==b'\xf8'];starts=[t for t,m in messages if m==b'\xfa'];stops=[t for t,m in messages if m==b'\xfc']
    window=[t for t in clocks if 38800<=t<=41500]
    quarter_bpm=270 if isinstance(studies[selected]['meter'],list) and studies[selected]['meter'][1]==8 else 180
    expected_hz=quarter_bpm*24/60
    assert len(window)>50
    observed_hz=1000*(len(window)-1)/(window[-1]-window[0])
    assert abs(observed_hz-expected_hz)<expected_hz*.02
    result={'panelScriptSha256':hashlib.sha256(','.join(keys+extra).encode()).hexdigest(),
        'playing':bool(v[0]),'study':v[1],'bpm':v[6],'feel':v[7],'master':v[12],'speed':v[72],'kumbiaMode':v[78],
        'kumbiaSettings':list(v[79:83]),'kumbiaLevel':v[83],'fxParams':params,'peakIrqUs':stats[9]/24,'misses':stats[2],
        'overlays':v[49],
        'late':stats[3],'clips':stats[4],'shed':stats[10],'rackPeak':stats[6],'totalPcmPeak':stats[8],
        'identities':identities,'clockPackets':len(clocks),'startPackets':len(starts),'stopPackets':len(stops),
        'sidemanReports':len(side),'playingAt180Reports':sum(s['playing'] and s['bpm']==180 for s in side),
        'clockWindow':{'fromMs':38800,'toMs':41500,'quarterBpm':quarter_bpm,'expectedHz':expected_hz,'observedHz':observed_hz,'packets':len(window)},
        'reportedBrakeTempos':sorted(set(s['bpm'] for s in side if s['ms']>=45500 and s['playing'])),
        'virtualFlashUnchanged':flash.read_bytes()==seed}
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    assert v[1]==selected and v[6]==180 and v[7]==100 and params==[100,100,5,100,90]
    assert v[12]==(100 if args.master_raw>=1008 else (args.master_raw*100+511)//1023)
    assert v[78]==1 and list(v[79:83])==[100,100,24,0 if name=='fastClock' else 54]
    assert v[83]==args.kumbia_level
    assert v[49]==(15 if args.layers else 0)
    assert not any(stats[2:5]) and not stats[10] and stats[9]/24<=2000
    assert identities==['FM-1_922','FM-1_92210003'] and result['playingAt180Reports'] and len(clocks)>200
    assert result['virtualFlashUnchanged'] and len(starts)==1
    assert bool(v[0])==(name!='brake') and v[72]==(60 if name=='brake' else 100)
    assert len(stops)==(1 if name=='brake' else 0) and stats[6]==10 and stats[8]==10
    return result
cases=[c for c in [('maximum',44800,[]),('fastClock',44800,[]),('brake',57000,brake)] if not args.case or c[0] in args.case]
with ThreadPoolExecutor(max_workers=2) as pool:
    results=dict(zip((c[0] for c in cases),pool.map(run,cases)))
report={'imageSHA256':hashlib.sha256(image.read_bytes()).hexdigest(),'study':studies[selected]['id'],'allFourLayers':args.layers,
        'studySelection':'ordinary saved show' if args.saved_study else 'panel preset browser',
        'scope':studies[selected]['short']+' plus Kumbia, ten manual 727 pads and maximum effects; slow and home sample-clock corners; not every study','results':results,'physicalUnitTouched':False}
(out/'load.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
