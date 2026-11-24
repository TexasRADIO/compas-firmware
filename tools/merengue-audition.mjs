import {dacScale} from '../web/dac-format.mjs';
// Complete backing: literal source-stroke tests live in tests/merengue.test.mjs.
// This renders the real board WASM, and checks show recall and audible variants.
import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';
const folder=process.argv[2];assert(folder,'Pass a new output directory');
const studies=JSON.parse(await readFile(new URL('../src/score.json',import.meta.url))).studies;
const set=JSON.parse(await readFile(new URL('../repertoire/merengue-audition-set.json',import.meta.url)));
const page=await readFile(new URL('../build/Compas-Merengue.html',import.meta.url),'utf8');
assert.deepEqual(JSON.parse(page.match(/const startupSet=(.*);\n/)[1]),set);
const module=await WebAssembly.compile(await readFile(new URL('../build/board/browser-board.wasm',import.meta.url)));
function boot(bytes){
  const b=new WebAssembly.Instance(module).exports;
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),bytes.length).set(bytes);
  assert.equal(b.bench_boot(1),1);b.bench_audio();b.bench_master(100);b.bench_audio();
  return b;
}
const samples=[],clips=[];
for(const section of ['base','lift','break']){
  const b=boot(preparedPayload({format:1,songs:[{title:'MERENGUE CONGAS',study:'merengue-congas',bpm:120,feel:0,kit:true,section}]},studies));
  b.bench_input(1<<12,0,0);
  const frames=4*44100,start=samples.length,hash=createHash('sha256');let peak=0;
  for(let offset=0;offset<frames;offset+=128){
    const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    hash.update(Buffer.from(pcm.buffer,pcm.byteOffset,pcm.byteLength));
    for(let i=0;i<Math.min(128,frames-offset);i++){
      assert.equal(pcm[i*2],pcm[i*2+1]);const value=pcm[i*2]/dacScale(b);
      peak=Math.max(peak,Math.abs(value));samples.push(value);
    }
  }
  assert(peak>100 && peak<32767);
  b.bench_input(1<<12,0,0);b.bench_audio();assert.equal(b.bench_playing(),0);
  assert.equal(b.bench_show_export(),0);
  const bytes=new Uint8Array(b.memory.buffer,b.bench_set_buffer(),1376).slice();
  const rhythms=new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),2416).slice();
  const show=serializeShow(bytes,rhythms),again=boot(showPayload(show).payload);
  again.bench_read_view();const view=new Uint8Array(again.memory.buffer,again.bench_snapshot()+20,76);
  assert.equal(view[1],15);assert.equal(view[2],['base','lift','break'].indexOf(section));
  await writeFile(join(folder,'Merengue-'+section+'.cpshow'),JSON.stringify(show)+'\n',{flag:'wx'});
  clips.push({section,frames,peak,sha256:hash.digest('hex'),startSeconds:start/44100,durationSeconds:4});
  if(section!=='break')for(let i=0;i<22050;i++)samples.push(0);
}
assert.equal(new Set(clips.map(c=>c.sha256)).size,3);
const wav=Buffer.alloc(44+samples.length*2);
wav.write('RIFF',0);wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);
wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);
wav.writeUInt32LE(44100,24);wav.writeUInt32LE(88200,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);
wav.write('data',36);wav.writeUInt32LE(samples.length*2,40);
for(let i=0;i<samples.length;i++)wav.writeInt16LE(samples[i],44+i*2);
await writeFile(join(folder,'Merengue-Conga-Backing.wav'),wav,{flag:'wx'});
const result={sampleRate:44100,channels:1,showRecall:true,clips};
await writeFile(join(folder,'Merengue-Conga-Backing.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify(result));
