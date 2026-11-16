import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {preparedPayload} from '../web/set-payload.mjs';
import {keyPresentation} from '../web/key-presentation.mjs';
import {catalog,boardWasm} from './lib/bench.mjs';

const json=async path=>JSON.parse(await readFile(new URL(path,import.meta.url),'utf8'));
const set=await json('../repertoire/cucaracha-rehearsal-set.json');
const payload=preparedPayload(set,catalog);
const {instance}=await WebAssembly.instantiate(await readFile(
  boardWasm));
const b=instance.exports;
new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
assert.equal(b.bench_boot(1),1);
b.bench_audio();
b.bench_master(80);
b.bench_audio();
const snapshot=()=>{
  b.bench_read_view();
  return new Uint8Array(b.memory.buffer,b.bench_snapshot(),b.bench_snapshot_size()).slice();
};
const view=()=>snapshot().subarray(20,96);
const input=(buttons=0,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();};
const key=i=>{input(0,1<<i);input();};
let peak=0;
function until(condition) {
  for(let i=0;i<4000&&!condition(view());i++) {
    const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let frame=0;frame<256;frame+=2) {
      assert.equal(pcm[frame],pcm[frame+1]);
      peak=Math.max(peak,Math.abs(pcm[frame]/128));
    }
  }
  assert(condition(view()),JSON.stringify([...view()]));
}

assert.equal(view()[31],8);
assert.equal(view()[37],3);
assert.equal(keyPresentation(snapshot(),catalog).keys[7].name,'Start');
input(1<<12);
assert.equal(view()[32],0);
until(v=>v[32]===2);
assert.equal(view()[33],0);
key(12);
until(v=>v[32]===3 && v[45]===6);
assert.equal(view()[33],8);
key(7);
until(v=>v[32]===2 && v[45]===6);
assert.equal(view()[33],0);
key(16);
until(v=>v[32]===4);
assert.equal(view()[33],16);
until(v=>!v[0]);
assert.equal(view()[14],0);
assert(peak>100 && peak<32767);
console.log(JSON.stringify({score:8,openingVerse:true,chorusPickup:true,cadence:true,peakPCM:peak}));
