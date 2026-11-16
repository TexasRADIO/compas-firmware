// Run after board-browser.py. Exercise the exact worklet owner without a browser.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';
import {boardWasm} from './lib/bench.mjs';
const read=async p=>JSON.parse(await readFile(new URL(p,import.meta.url),'utf8'));
const studies=(await read('../src/score.json')).studies;
const scores=await read('../repertoire/board-scores.json');
const catalog=studies.concat(scores.map((s,i)=>({...s,piano:i+1,guide:!!s.guide || "guide_part" in s})));
const module=await WebAssembly.compile(await readFile(boardWasm));
let cases=0;
for(const score of scores) {
 const hashes=new Set();
 for(let transpose=-6;transpose<=5;++transpose) {
  const b=new WebAssembly.Instance(module).exports;
  const payload=preparedPayload({format:1,songs:[{title:'SINGER KEY',study:score.id,bpm:180,guide:!!score.guide || "guide_part" in score,transpose}]},catalog);
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),1360).set(payload);
  assert.equal(b.bench_boot(1),1);b.bench_audio();b.bench_master(80);b.bench_audio();
  const input=(buttons,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();};
  input(1<<8);input(0,1<<9);input(0); // Band, Verse, release: no count-in.
  b.bench_input(1<<12,0,0);
  const hash=createHash('sha256');let peak=0;
  for(let block=0;block<700;++block) {
   const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
   for(let i=0;i<256;i+=2){assert.equal(pcm[i],pcm[i+1]);peak=Math.max(peak,Math.abs(pcm[i]/128));}
   hash.update(Buffer.from(pcm.buffer,pcm.byteOffset,pcm.byteLength));
  }
  assert(peak>100 && peak<32767);hashes.add(hash.digest('hex'));
  input(1<<12);assert.equal(b.bench_playing(),0);
  assert.equal(b.bench_show_export(),0);
  const set=new Uint8Array(b.memory.buffer,b.bench_set_buffer(),1360);
  const rhythm=new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),2416);
  assert.equal(set[64],(transpose+12)%12);
  const file=serializeShow(set,rhythm), decoded=showPayload(JSON.parse(JSON.stringify(file)));
  assert.deepEqual(decoded.payload,set);
  const reopen=new WebAssembly.Instance(module).exports;
  new Uint8Array(reopen.memory.buffer,reopen.bench_set_buffer(),1360).set(decoded.payload);
  new Uint8Array(reopen.memory.buffer,reopen.bench_rhythm_slots_buffer(),2416).set(decoded.rhythms);
  assert.equal(reopen.bench_boot(3),1);reopen.bench_audio();
  assert.equal(reopen.bench_show_export(),0);
  assert.deepEqual(new Uint8Array(reopen.memory.buffer,reopen.bench_set_buffer(),1360),set);
  ++cases;
 }
 assert.equal(hashes.size,12,score.id+' must produce twelve different pitched streams');
}
// Both slots play the same Galveston notation. The original bolero layer
// adds signal during Verse, while count-in, Intro and Coda match exactly.
function golondrinaStream(study, key) {
  const b = new WebAssembly.Instance(module).exports;
  const set = preparedPayload({format:1,songs:[{title:'GOLONDRINA',study,bpm:180,guide:true}]},catalog);
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),set.length).set(set);
  assert.equal(b.bench_boot(1),1);
  b.bench_audio();
  b.bench_master(80);
  b.bench_audio();
  if (key) {
    b.bench_input(0,1<<key,0);
    b.bench_audio();
    b.bench_input(0,0,0);
    b.bench_audio();
  }
  b.bench_input(1<<12,0,0);
  const hash = createHash('sha256');
  let peak = 0;
  for (let block=0; block<700; ++block) {
    const pcm = new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for (const sample of pcm) peak = Math.max(peak,Math.abs(sample/128));
    hash.update(Buffer.from(pcm.buffer,pcm.byteOffset,pcm.byteLength));
  }
  assert(peak>100 && peak<32767);
  return hash.digest('hex');
}
const piano='la-golondrina-piano', bolero='la-golondrina-piano-bolero';
assert.equal(golondrinaStream(piano,0),golondrinaStream(bolero,0));
assert.notEqual(golondrinaStream(piano,9),golondrinaStream(bolero,9));
assert.equal(golondrinaStream(piano,16),golondrinaStream(bolero,16));
console.log(JSON.stringify({scores:scores.length,keysPerScore:12,cases,framesPerCase:89600,
  showReopen:true,stereo:true,clipped:false,boleroVersePCM:true,unlayeredIntroAndCoda:true}));
