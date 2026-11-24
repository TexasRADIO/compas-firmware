#!/usr/bin/env python3
"""Exercise the real Compás editor/transport. Never flashes or raises MASTER.

Requires the separately qualified package to be installed first. Bind the
CoreMIDI endpoint to its physical USB location with the retained ports helper.
This tests USB/engine state, not physical knobs, analogue output or IRQ timing.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import time
import zlib
import mido

PREFIX=bytes.fromhex('f00022243505')
def u14(n):return [n>>7&127,n&127]
def u28(n):return [(n>>s)&127 for s in (21,14,7,0)]
def read32(b):
    n=0
    for v in b:n=n<<7|v
    return n
def unpack7(b):
    out=bytearray()
    for i in range(0,len(b),8):
        chunk=b[i:i+8]
        for j,v in enumerate(chunk[1:]):out.append(v|((chunk[0]>>j&1)<<7))
    return bytes(out)
def snapshot_fields(raw):
    fields={};at=4
    while at<len(raw):
        scope=raw[at];n=int.from_bytes(raw[at+1:at+3],'big');at+=3
        assert at+n<=len(raw);fields[scope]=list(raw[at:at+n]);at+=n
    assert at==len(raw)
    return {'revision':int.from_bytes(raw[:4],'big'),'sections':fields}

class Client:
    def __init__(self,ports,updater,location):
        self.ports=ports;self.location=location;self.rows=[];self.commands=[];self.seq=0
        spec=importlib.util.spec_from_file_location('ota_wire',updater)
        self.ota=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.ota)
        self.name=self.endpoint();self.start=time.monotonic()
        self.input=mido.open_input(self.name);self.output=mido.open_output(self.name)
    def endpoint(self):
        rows=[r.split('\t') for r in subprocess.check_output([str(self.ports)],text=True).splitlines() if r.startswith(self.location+'\t')]
        assert len(rows)==1 and rows[0][2]==rows[0][3],'ambiguous physical endpoint'
        return rows[0][2]
    def close(self):self.input.close();self.output.close()
    def send(self,b):
        assert self.endpoint()==self.name,'physical endpoint changed'
        self.commands.append((time.monotonic()-self.start,list(b)))
        self.output.send(mido.Message.from_bytes(b))
    def poll(self):
        msg=self.input.poll()
        if msg is None:return None
        b=bytes(msg.bytes());self.rows.append((time.monotonic()-self.start,list(b)));return b
    def pump(self,seconds):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            if self.poll() is None:time.sleep(.001)
    def request(self,kind,payload=(),want=None):
        self.seq+=1;seq=self.seq
        self.send(PREFIX+bytes([kind,*u14(seq),*payload,247]))
        until=time.monotonic()+3
        while time.monotonic()<until:
            b=self.poll()
            if b and b.startswith(PREFIX) and len(b)>=10 and (b[7]*128+b[8])==seq:
                assert want is None or b[6]==want,(kind,b.hex())
                return b[6],b[9:-1]
            if b is None:time.sleep(.001)
        raise TimeoutError(f'No editor reply to {kind:#x}')
    def snapshot(self):
        _,body=self.request(0x10,want=0x11)
        assert body[:4]==bytes([0,0,0,1]),'expected one snapshot chunk'
        raw=unpack7(body[4:-5]);assert zlib.crc32(raw)==read32(body[-5:]),'snapshot CRC'
        return snapshot_fields(raw)
    def set(self,scope,target,field,value):
        tx=self.seq+1
        self.request(0x20,[*u28(0),scope,*u14(tx)],0x30)
        try:
            self.request(0x21,[scope,target,*u14(field),*u14(value)],0x34)
            self.request(0x23,u14(tx),0x30)
        except BaseException:
            self.request(0x24,want=0x30);raise
        self.pump(.06)
    def identity(self):
        self.send(self.ota.HS_FULL);until=time.monotonic()+3
        while time.monotonic()<until:
            b=self.poll();ident=self.ota.parse_identity(b) if b else None
            if ident:return ident.text
            if b is None:time.sleep(.001)
        raise TimeoutError('No full updater identity')

def run(args):
    args.out.mkdir(parents=True,exist_ok=True)
    c=Client(args.ports_helper.resolve(),args.updater.resolve(),args.location)
    result={'scope':'physical USB/editor/transport only','physicalKnobsTested':False,'analogueOutputMeasured':False,'hardwareIrqCountersAvailable':False}
    initial=None;body_passed=False
    try:
        assert c.identity()=='FM-1_92210003'
        _,hello=c.request(1,[0],2)
        assert hello[1]==2 and hello[2]*128+hello[3]==13
        assert hello[11]*128+hello[12]==38
        initial=c.snapshot();result['initial']=initial
        c.request(0x41,want=0x30);c.pump(.1)
        grooves=[]
        for i in range(38):
            c.set(0x10,0,0x1100,i);state=c.snapshot()
            assert state['sections'][0x12][0]==i,(i,state)
            c.send(bytes([250]));c.pump(.2)
            assert c.snapshot()['sections'][9][0]==1
            c.send(bytes([252]));c.pump(.15)
            assert c.snapshot()['sections'][9][0]==0
            grooves.append({'groove':i,'revision':state['revision'],'startStop':True})
        result['grooves']=grooves
        # Maximum effect values use the same public fields as the browser.
        for i,v in enumerate((100,100,5,100,90)):c.set(5,i,0x1300+i,v)
        rates=[]
        for groove,bpm in ((0,60),(0,94),(0,180),(36,60),(36,180),(37,60),(37,180)):
            c.set(0x10,0,0x1100,groove);c.set(7,0,0x1001,bpm*10)
            before=time.monotonic()-c.start;c.send(bytes([250]));c.pump(3);assert c.identity()=='FM-1_92210003'
            c.pump(1);stop=time.monotonic()-c.start;c.send(bytes([252]));c.pump(.6)
            ticks=[t for t,b in c.rows if b==[248] and before+.2<t<stop-.2]
            expected=bpm*.4*(1.5 if groove>=36 else 1)
            hz=(len(ticks)-1)/(ticks[-1]-ticks[0]);assert abs(hz-expected)<expected*.03,(groove,bpm,hz,expected)
            assert not any(b==[248] and stop+.15<t<stop+.55 for t,b in c.rows)
            rates.append({'groove':groove,'bpm':bpm,'expectedHz':expected,'observedHz':hz,'identityWhilePlaying':True,'quietAfterStop':True})
        result['clock']=rates
        c.set(0x10,0,0x1100,37);c.set(7,0,0x1001,1800)
        c.send(bytes([250]));before=time.monotonic()-c.start
        for _ in range(args.soak_seconds//10):
            c.pump(10);assert c.identity()=='FM-1_92210003';state=c.snapshot()
            assert state['sections'][9][0]==1
        c.send(bytes([252]));c.pump(.5)
        result['soakSeconds']=time.monotonic()-c.start-before
        # Also exercise Continue after Stop, then leave the instrument stopped.
        c.send(bytes([251]));c.pump(2);assert c.snapshot()['sections'][9][0]==1
        c.send(bytes([252]));c.pump(.5)
        result['continue']=True;body_passed=True
    finally:
        try:
            c.send(bytes([252]));c.pump(.15)
            if initial:
                s=initial['sections'];c.set(0x10,0,0x1100,s[0x12][0]);c.set(7,0,0x1001,int.from_bytes(bytes(s[1][:2]),'big'))
                for i,v in enumerate(s[5]):c.set(5,i,0x1300+i,v)
            result['final']=c.snapshot();result['identity']=c.identity()
            states=[b for _,b in c.rows if len(b)==19 and b[:7]==[240,0,34,36,53,1,2]]
            assert states and all(b[9]==args.language for b in states),'language telemetry mismatch'
            assert result['final']['sections'][9][0]==0
            result['language']=args.language
            result['passed']=body_passed
        finally:
            c.close()
            (args.out/'wire.json').write_text(json.dumps({'sent':c.commands,'received':c.rows})+'\n')
            (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ports-helper',type=Path,required=True);p.add_argument('--updater',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--language',type=int,choices=(0,1),required=True)
    p.add_argument('--location',default='02120000');p.add_argument('--soak-seconds',type=int,default=120)
    args=p.parse_args();assert 10<=args.soak_seconds<=1800
    print(json.dumps(run(args),indent=2))
