import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {BUTTON,ENC} from './lib/bench.mjs';
const module=new WebAssembly.Module(readFileSync('build/board/browser-board.wasm'));
function rig(){
  const b=new WebAssembly.Instance(module).exports;assert.equal(b.bench_boot(4),1);b.bench_master(100);
  let tick=0;
  const audio=()=>{tick+=128*544;return Int32Array.from(new Int32Array(b.memory.buffer,b.bench_audio(),256));};
  const view=()=>{b.bench_read_view();return Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_snapshot(),b.bench_snapshot_size()));};
  const tap=buttons=>{b.bench_input(buttons,0,tick);audio();b.bench_input(0,0,tick);audio();};
  const turn=(encoder,delta)=>{new Int32Array(b.memory.buffer,b.bench_encoders(),7)[encoder]=delta;b.bench_input(0,0,tick);audio();};
  audio();return {b,audio,view,tap,turn};
}
const r=rig();assert.equal(r.view().length,140,'no snapshot layout expansion');assert.equal(r.view()[103],100);
const controls=Array.from(r.view().subarray(99,103));
r.turn(ENC.K4,-5);assert.equal(r.view()[103],95);
r.turn(ENC.K4,5);assert.equal(r.view()[103],100);
r.tap(BUTTON.OCT_DOWN|BUTTON.OCT_UP);assert.equal(r.view()[103],100);
r.turn(ENC.K4,-150);assert.equal(r.view()[103],0);
r.turn(ENC.K4,45);assert.equal(r.view()[103],45);
assert.deepEqual(Array.from(r.view().subarray(99,103)),controls,'level never repurposes a sound knob');
r.tap(BUTTON.OCT_DOWN);assert.equal(r.view()[102],1);assert.equal(r.view()[103],45);
r.tap(BUTTON.OCT_UP);assert.equal(r.view()[102],0);assert.equal(r.view()[103],45);
const mixBefore=Array.from(r.view().subarray(36,42));
r.tap(BUTTON.HOME);r.tap(BUTTON.FX);assert.equal(r.view()[0],1);
r.tap(BUTTON.OCT_UP);assert.equal(r.view()[103],50);
assert.deepEqual(Array.from(r.view().subarray(36,42)),mixBefore,'band levels are independent');
r.tap(BUTTON.FX);assert.equal(r.view()[0],8);r.tap(BUTTON.OCT_UP);assert.equal(r.view()[103],50,'Effects OCT controls GLUE, not Kumbia');
assert.equal(r.b.bench_kit_level(101),0);assert.equal(r.view()[103],50);

function runLayer(level,enabled=true){
  const x=rig();assert.equal(x.b.bench_kit_level(level),1);
  if(enabled)x.tap(BUTTON.FX);else x.tap(BUTTON.HOME);
  x.tap(BUTTON.PLAY);return x;
}
const band=runLayer(100,false),zero=runLayer(0),half=runLayer(50),full=runLayer(100);
let audible=0;
for(let block=0;block<800;++block){
  const [dry,mute,h,f]=[band,zero,half,full].map(x=>x.audio());
  assert.deepEqual(mute,dry,'zero Kumbia leaves every band sample unchanged');
  for(let i=0;i<dry.length;++i){
    assert(Math.abs((h[i]-dry[i])*2-(f[i]-dry[i]))<=full.b.bench_dac_scale(),'half gain scales only Kumbia, allowing one PCM rounding bit');
    if(f[i]!==dry[i])++audible;
  }
}
assert(audible>1000);
const continuous=runLayer(100),moving=runLayer(100);
for(let n=0;n<40;++n){continuous.audio();moving.audio();}
assert.equal(moving.b.bench_kit_level(0),1);
for(let n=0;n<50;++n){continuous.audio();moving.audio();}
assert.equal(moving.b.bench_kit_level(100),1);
for(let n=0;n<50;++n){continuous.audio();moving.audio();}
for(let n=0;n<80;++n)assert.deepEqual(moving.audio(),continuous.audio(),'unmute resumes the same pattern/sample phase');
assert.equal(moving.b.bench_rails(),0);

const {BoardAudio}=await import('data:text/javascript;base64,'+Buffer.from(readFileSync('web/board-audio.mjs','utf8')+'\nexport {BoardAudio};').toString('base64'));
for(const settings of [[0,100,12,0],[0,100,12,0,35],[0,100,12,0,0]]){
  let state;
  const owner=new BoardAudio({module,startupHanan:true,kitSettings:settings},48000,s=>{if(s instanceof Uint8Array)state=s;});
  owner.receive({master:100});owner.render([new Float32Array(128),new Float32Array(128)]);
  assert.equal(state[103],settings[4]??100,'old browser saves default to unity; new saves retain level, including zero');
}
const storagePrelude=readFileSync('web/board-bench.mjs','utf8').split('function copySet(')[0];
const loadStorage=new Function('WebAssembly','module','localStorage',storagePrelude+'\nreturn kitSettings;');
const oldKey='compas-kumbia-kit-v1',newKey='compas-kumbia-kit-v2',oldSettings=[7,63,9,24];
for(const [data,expected] of [
  [{[oldKey]:JSON.stringify(oldSettings)},oldSettings],
  [{[oldKey]:JSON.stringify(oldSettings),[newKey]:'[0,100,12,0,0]'},[0,100,12,0,0]],
  [{[oldKey]:JSON.stringify(oldSettings),[newKey]:'[0,100,12,0,101]'},oldSettings]]){
  assert.deepEqual(loadStorage(WebAssembly,module,{getItem:k=>data[k]??null}),expected,
    'new browser key retains old four-control settings without overwriting them; saved mute is preserved');
}
if(process.argv[2]){
  const x=runLayer(35);x.b.bench_lang(1);x.view();
  const frame=new Uint16Array(x.b.memory.buffer,x.b.bench_draw(2400000),240*240),rgb=Buffer.alloc(frame.length*3);
  for(let i=0;i<frame.length;++i){rgb[i*3]=(frame[i]>>11)*255/31;rgb[i*3+1]=(frame[i]>>5&63)*255/63;rgb[i*3+2]=(frame[i]&31)*255/31;}
  writeFileSync(process.argv[2],Buffer.concat([Buffer.from('P6\n240 240\n255\n'),rgb]));
}
console.log('Kumbia level: independent 0/50/100 audio, 800-block band parity, phase-preserving mute, page controls, old/new browser restore and stable snapshot pass.');
