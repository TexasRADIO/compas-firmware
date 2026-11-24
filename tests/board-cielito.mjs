import {dacScale} from '../web/dac-format.mjs';
// Production score ID 7: performer controls on the three-beat printed score.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {preparedPayload} from '../web/set-payload.mjs';
import {boardWasm} from './lib/bench.mjs';
const json=async p=>JSON.parse(await readFile(new URL(p,import.meta.url),'utf8'));
const studies=(await json('../src/score.json')).studies;
const scores=await json('../repertoire/board-scores.json');
const catalog=studies.concat(scores.map((s,i)=>({...s,piano:i+1,guide:!!s.guide})));
const module=await WebAssembly.compile(await readFile(boardWasm));
const b=new WebAssembly.Instance(module).exports;
const payload=preparedPayload(await json('../repertoire/cielito-rehearsal-set.json'),catalog);
new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
assert.equal(b.bench_boot(1),1);b.bench_audio();b.bench_master(80);b.bench_audio();
const view=()=>{b.bench_read_view();return new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,76).slice();};
const input=(buttons=0,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();};
const key=k=>{input(0,1<<k);input();};
const turn=(k,n)=>{new Int32Array(b.memory.buffer,b.bench_encoders(),7)[k]=n;input();};
let peak=0;
function blocks(n) {
  for(let i=0;i<n;i++) {
    const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let f=0;f<256;f+=2) {assert.equal(pcm[f],pcm[f+1]);peak=Math.max(peak,Math.abs(pcm[f]/dacScale(b)));}
  }
  assert.equal(view()[14],0);assert(peak<32767);
}
function until(check,limit=2400) {
  for(let i=0;i<limit && !check(view());i++) blocks(1);
  assert(check(view()),JSON.stringify([...view()]));
}
assert.equal(view()[31],7);assert.equal(view()[37],3);assert.equal(view()[73],0);
input(1<<12);assert.equal(view()[32],0); // Three-beat count-in.
until(v=>v[32]===1); // Pickup, then the first full bar.
until(v=>v[32]===2);
key(8);assert.equal(view()[36],0);key(8);assert.equal(view()[36],1);
key(10);assert.equal(view()[34],1);blocks(1800);assert.equal(view()[34],1);
key(10);assert.equal(view()[34],0);
// Live chorus cue lands in the score, then verse can be requested again.
key(12);assert.equal(view()[45],3);until(v=>v[32]===3 && v[45]===6);
assert.equal(view()[33],17); // Board bars are zero-based; printed span 18.
key(9);until(v=>v[32]===2 && v[45]===6);assert.equal(view()[33],1);
turn(5,-3);assert.equal(view()[75]>>4,9);
turn(5,3);assert.equal(view()[75]>>4,12);assert.equal(view()[75]&15,0);
turn(5,-3);until(v=>(v[75]&15)===9 && (v[75]>>4)===12);
// The pickup is a one-beat partial bar, not an invented intro section.
key(7);until(v=>v[32]===1 && v[45]===6);assert.equal(view()[33],0);
key(16);until(v=>v[32]===4);assert.equal(view()[33],31);
until(v=>!v[0]); // Two-beat final bar must finish, not pad to 3/4.
assert(peak>100);
// The ending advances to the next song (review fix 5), here the guide-off copy.
assert.equal(view()[28],1);assert.equal(view()[36],0);
turn(6,1);assert.equal(view()[28],2);assert.equal(view()[75]&15,9);
turn(6,1);assert.equal(view()[31],0);assert.equal(view()[1],4);
turn(6,-1);assert.equal(view()[31],7);assert.equal(view()[37],3);
console.log(JSON.stringify({score:7,meter:'3/4',countIn:true,guide:true,hold:true,
  pickup:true,verse:true,chorus:true,ending:true,transposeCueCancelLand:true,
  songSwitches:true,endingAdvances:true,automaticPercussion:false,peakPCM:peak,clipped:false}));
