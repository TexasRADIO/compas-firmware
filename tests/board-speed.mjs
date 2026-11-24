// Run after tools/board-browser.py: exact whole-stream varispeed in board WASM.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {boardWasm} from './lib/bench.mjs';
const module=await WebAssembly.compile(await readFile(boardWasm));
function source(rate) {
  const b=new WebAssembly.Instance(module).exports;
  assert.equal(b.bench_boot(0),1);b.bench_audio();b.bench_master(100);b.bench_audio();
  const enc=new Int32Array(b.memory.buffer,b.bench_encoders(),7);
  const input=(buttons,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();};
  input(1<<7,1<<10);input(1<<8); // CDMX slot -> Band
  input(1<<2);enc[5]=rate-100;input(0); // FX -> Mix, SPEED
  b.bench_input(1<<12,0,0);
  return {b,enc};
}
const blocks=800,frames=128*blocks;
const baseline=source(100),reference=new Int16Array(frames+128);
for(let block=0;block<=blocks;block++) {
  const pcm=new Int32Array(baseline.b.memory.buffer,baseline.b.bench_audio(),256);
  for(let i=0;i<128;i++)reference[block*128+i]=pcm[2*i]/baseline.b.bench_dac_scale();
}
for(const rate of [60,75,78,100]) {
  const {b}=source(rate);
  for(let block=0;block<blocks;block++) {
    const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let i=0;i<128;i++) {
      const time=(block*128+i)*rate,index=Math.floor(time/100),fraction=time%100;
      const expected=reference[index]+Math.trunc((reference[index+1]-reference[index])*fraction/100);
      assert.equal(pcm[2*i],expected*b.bench_dac_scale());assert.equal(pcm[2*i+1],pcm[2*i]);
    }
  }
  b.bench_input(1<<12,0,0);
  assert.ok(new Int32Array(b.memory.buffer,b.bench_audio(),256).every(x=>x===0));
}
console.log(JSON.stringify({rates:[60,75,78,100],comparedFramesPerRate:frames,wholeStreamRatio:true,stopClears:true}));
