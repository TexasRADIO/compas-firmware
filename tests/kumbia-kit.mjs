import assert from 'node:assert/strict';
import {readFileSync, writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {BUTTON, ENC, catalog} from './lib/bench.mjs';
import {keyPresentation} from '../web/key-presentation.mjs';

const module=new WebAssembly.Module(readFileSync('build/board/browser-board.wasm'));
const source=process.env.COMP_HANAN;
assert.ok(source,'Set COMP_HANAN to the pinned Hanan extraction.');
const framesPerBar=117600, blockSize=128, noCue=4;
function bench() {
  const board=new WebAssembly.Instance(module).exports;
  let ticks=0;
  assert.equal(board.bench_boot(4),1);
  const audio=()=>{
    ticks+=blockSize*544;
    const frame=new Int32Array(board.memory.buffer,board.bench_audio(),blockSize*2);
    return Array.from({length:blockSize},(_,i)=>frame[i*2]/128);
  };
  const read=()=>{
    board.bench_read_view();
    return new Uint8Array(board.memory.buffer,board.bench_snapshot(),board.bench_snapshot_size()).slice();
  };
  const press=(button=0,keys=0)=>board.bench_input(button,keys,ticks);
  const turn=(index,delta)=>{
    new Int32Array(board.memory.buffer,board.bench_encoders(),7)[index]=delta;
    press();
    audio();
  };
  const tap=key=>{
    press(0,1<<key);
    audio();
    press();
    audio();
  };
  const start=()=>press(BUTTON.PLAY);
  const run=n=>Array.from({length:n},audio).flat();
  const master=level=>{ board.bench_master(level); audio(); };
  audio();
  master(100);
  return {board,audio,read,press,turn,tap,start,run};
}
const settings=bytes=>Array.from(bytes.subarray(99,103));
const factoryNames=['1-white','2-yellow','3-red','4-green'];
const home=[0,100,12,0];
for(let i=0;i<4;i++) {
  const rig=bench();
  assert.equal(rig.read()[0],7);
  assert.deepEqual(settings(rig.read()),home);
  rig.tap(i+4);
  rig.start();
  const actual=rig.run(Math.ceil(framesPerBar/blockSize)).slice(0,framesPerBar);
  const wav=readFileSync(join(source,'previews','bank-1-white_slot-'+factoryNames[i]+'.wav'));
  const expected=Array.from({length:framesPerBar},(_,f)=>(wav[44+Math.floor(f*9804/44100)]-127)*128);
  assert.deepEqual(actual,expected,'Home settings reproduce factory preview, centred for Compás');
}

function pad(controls) {
  const rig=bench();
  assert.equal(rig.board.bench_kit_restore(...controls),1);
  rig.press(0,1<<2); // Campana has a stable factory pitch and a long tail.
  return rig.run(240);
}
const lastSound=pcm=>pcm.findLastIndex(sample=>sample!==0)+1;
const dry=pad(home), decay=pad([0,5,12,0]), pitch=pad([0,100,24,0]), clock=pad([0,100,12,54]);
assert.ok(lastSound(decay)<lastSound(dry)/10,'5% DECAY gives a short tail');
assert.ok(Math.abs(lastSound(pitch)*2-lastSound(dry))<12,'+12 semitones doubles playback pitch');
assert.ok(Math.abs(lastSound(clock)*4400/9804-lastSound(dry))<12,'CLOCK lowers the sample clock');
assert.notDeepEqual(pad([100,100,12,0]),dry,'DRIVE changes the factory signal');

function accentRun(mode,drive) {
  const rig=bench();
  assert.equal(rig.board.bench_kit_restore(drive,100,12,0),1);
  for(let i=0;i<mode;i++) {
    rig.press(BUTTON.FX);
    rig.audio();
  }
  if(mode===0) { rig.press(BUTTON.HOME); rig.audio(); }
  rig.start();
  return rig.run(920);
}
const band=accentRun(0,0);
for(const drive of [0,100]) {
  const layer=accentRun(1,drive), solo=accentRun(2,drive);
  const accentChanges=layer.reduce((n,x,i)=>n+(x-band[i]!==solo[i]),0);
  if(drive===0) assert.equal(accentChanges,0,'No accent effect at zero drive');
  else assert.ok(accentChanges>100,'The groove accents change drive on the kit bus');
}

const rig=bench();
rig.start();
rig.run(40);
assert.equal(rig.read()[98],2,'PLAY starts Drums Only');
rig.tap(5);
assert.deepEqual(Array.from(rig.read().subarray(96,98)),[0,1]);
rig.tap(6);
assert.deepEqual(Array.from(rig.read().subarray(96,98)),[0,2]);
const presentation=keyPresentation(rig.read(),catalog);
assert.equal(presentation.keys[6].name,'Red pattern');
assert.equal(presentation.keys[6].next,true);
rig.tap(4);
assert.equal(rig.read()[97],noCue);
rig.tap(7);
for(let blocks=48;blocks<917;blocks++) {
  rig.audio();
  assert.equal(rig.read()[96],0,'Queue waits for the next bar');
}
for(let i=0;i<6 && rig.read()[96]===0;i++) rig.audio();
assert.deepEqual(Array.from(rig.read().subarray(96,98)),[3,noCue]);
rig.turn(ENC.K1,100);
rig.turn(ENC.K2,-95);
rig.turn(ENC.K3,12);
rig.turn(ENC.K4,54);
const limits=[100,5,24,54];
assert.deepEqual(settings(rig.read()),limits);
assert.equal(rig.read()[26],90,'CLOCK leaves BPM unchanged');
rig.press(0,1); // A fresh hit while the knobs are at their limits.
const effects=rig.run(40);
rig.press();
assert.ok(effects.some(sample=>sample!==0));
assert.ok(effects.every(sample=>sample>=-32768 && sample<=32767));
rig.press(BUTTON.PLAY);
assert.ok(rig.audio().every(sample=>sample===0),'STOP cancels kit voices');
rig.tap(5);
assert.deepEqual(settings(rig.read()),limits,'Pattern selection leaves kit settings intact');
const fresh=bench();
assert.equal(fresh.board.bench_kit_restore(255,5,24,54),0);
assert.equal(fresh.board.bench_kit_restore(...limits),1);
fresh.audio();
assert.deepEqual(settings(fresh.read()),limits);
fresh.press(BUTTON.HOME);
fresh.audio();
fresh.press(BUTTON.PLAY);
fresh.run(12);
fresh.turn(ENC.SELECT,-1);
fresh.press(BUTTON.FX);
fresh.audio();
assert.equal(fresh.read()[98],1,'FX adds the kit as a layer over the band');
assert.equal(fresh.read()[20],1,'Layering leaves the transport running');
assert.deepEqual(settings(fresh.read()),limits);
assert.ok(fresh.run(96).some(sample=>sample!==0));

if(process.argv[2]) {
  rig.board.bench_lang(1);
  rig.read();
  const pixels=new Uint16Array(rig.board.memory.buffer,rig.board.bench_draw(2400000),240*240);
  const rgb=Buffer.alloc(pixels.length*3);
  for(let i=0;i<pixels.length;i++) {
    rgb[i*3]=Math.round((pixels[i]>>11)*255/31);
    rgb[i*3+1]=Math.round(((pixels[i]>>5)&63)*255/63);
    rgb[i*3+2]=Math.round((pixels[i]&31)*255/31);
  }
  writeFileSync(process.argv[2],Buffer.concat([Buffer.from('P6\n240 240\n255\n'),rgb]));
}
// Exercise the browser's audio owner and its 48 kHz output path without a device.
const ownerSource=readFileSync('web/board-audio.mjs','utf8')+'\nexport {BoardAudio};';
const {BoardAudio}=await import('data:text/javascript;base64,'+Buffer.from(ownerSource).toString('base64'));
let lastState;
const owner=new BoardAudio({module,startupHanan:true,kitSettings:limits},48000,state=>{
  if(state instanceof Uint8Array) lastState=state;
});
const stereo=[new Float32Array(128),new Float32Array(128)];
owner.receive({master:100});
owner.render(stereo);
assert.deepEqual(settings(lastState),limits);
assert.equal(lastState[0],7);
owner.receive({buttons:BUTTON.PLAY,keys:0,time:0});
let audioPeak=0;
for(let i=0;i<48;++i) {
  owner.render(stereo);
  assert.deepEqual(stereo[0],stereo[1]);
  for(const sample of stereo[0]) audioPeak=Math.max(audioPeak,Math.abs(sample));
}
assert.ok(audioPeak>0.01 && audioPeak<1,'Browser audio owner renders the kit at 48 kHz');
owner.receive({buttons:BUTTON.PLAY,keys:0,time:1000000});
owner.render(stereo);
owner.render(stereo);
assert.ok(stereo[0].every(sample=>sample===0),'Browser audio owner stops playback');

console.log('Kumbia checks passed: all four factory loops, exact home audio, queued/cancelled cues, four control limits, kit restore, tempo isolation and STOP.');
