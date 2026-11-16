import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {execFileSync} from 'node:child_process';
import {save,recall} from '../web/store.mjs';

const {instance:{exports:e}}=await WebAssembly.instantiate(readFileSync('build/engine.wasm'));
const pcm=new Int16Array(e.memory.buffer,e.cp_pcm(),256);
const studies=JSON.parse(readFileSync(new URL("../src/score.json",import.meta.url))).studies;
for(let study=0;study<studies.length;study++) test('WASM/native PCM and headroom, study '+study,()=>{
  const dir=mkdtempSync(join(tmpdir(),'compas-'));
  try{
    const output=join(dir,'a.wav');
    const stats=JSON.parse(execFileSync('./build/compas',[study,output],{encoding:'utf8'}));
    const file=readFileSync(output);
    assert.equal(file.subarray(0,4).toString(),'RIFF');
    assert.equal(stats.railSamples,0);assert.ok(stats.peakPCM>2000);
    assert.equal(e.cp_init(44100),0);assert.equal(e.cp_command(3,study,0),0);assert.equal(e.cp_command(1,0,0),0);
    const rate=44100,frames=stats.frames;
    const second=frame=>Math.floor(frame/rate);
    for(let done=0;done<frames;){
      const n=Math.min(frames-done,128);
      if(second(done)===6 && e.cp_status(1)===0 && e.cp_status(2)===3)e.cp_command(4,1,0);
      if(second(done)===13 && second(done-n)===12)e.cp_command(5,0,0);
      if(second(done)===19 && second(done-n)===18)e.cp_command(4,2,0);
      if(second(done)===25 && second(done-n)===24)e.cp_command(4,0,0);
      assert.equal(e.cp_render(n),0);
      const start=44+done*2;
      const bytes=Buffer.from(pcm.buffer,pcm.byteOffset,n*2);
      if(!bytes.equals(file.subarray(start,start+n*2))) assert.fail('PCM differs at block '+done);
      done+=n;
    }
  }finally{rmSync(dir,{recursive:true,force:true});}
});
test('failed verification and interrupted browser save keep active project',()=>{
  const map=new Map();
  let failure='';
  const storage={getItem:k=>map.get(k)??null,setItem(k,v){
    if(failure==='commit' && k==='compas-private-v1') throw Error('injected commit fault');
    if(failure==='slot' && k!=='compas-private-v1') return;
    map.set(k,v);
  }};
  assert.throws(()=>recall(storage),/No project/);
  const first=new Uint8Array(55);const next=first.slice();next[0]=1;
  save(storage,first);assert.deepEqual(recall(storage),first);
  for(const mode of ['slot','commit']){
    failure=mode;assert.throws(()=>save(storage,next));assert.deepEqual(recall(storage),first);
  }
  failure='';save(storage,next);assert.deepEqual(recall(storage),next);
});
