import {dacScale} from '../web/dac-format.mjs';
// Prepared singer backings with the source conga replacing the authored conga role.
import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import assert from 'node:assert/strict';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';
const folder=process.argv[2];assert(folder,'Pass an existing empty output directory');
const studies=JSON.parse(await readFile(new URL('../src/score.json',import.meta.url))).studies;
const module=await WebAssembly.compile(await readFile(new URL('../build/board/browser-board.wasm',import.meta.url)));
const set={format:1,songs:['BOLERO CONGA','BOLERO SPACE'].map(title=>
  ({title,study:'bolero',bpm:76,feel:0,kit:true,mute:16,section:'base',phrases:0}))};
function boot(payload,rhythms){
  const b=new WebAssembly.Instance(module).exports;
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
  if(rhythms)new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),rhythms.length).set(rhythms);
  assert.equal(b.bench_boot(rhythms?3:1),1);b.bench_audio();b.bench_master(85);b.bench_audio();return b;
}
const b=boot(preparedPayload(set,studies));
const enc=new Int32Array(b.memory.buffer,b.bench_encoders(),7);
const input=(buttons,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();};
for(let slot=0;slot<2;slot++){
  input(1<<7,1<<slot);input(0); // GLO, song key, release.
  input(1<<13);input(1<<11);
  for(let i=0;i<17+slot;i++){enc[6]=1;input(0);}
  input(1<<7);input(1<<9); // Capture source part and backing together.
}
input(1<<7,1);input(0);
assert.equal(b.bench_show_export(),0);
const payload=new Uint8Array(b.memory.buffer,b.bench_set_buffer(),1376).slice();
const rhythms=new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),2416).slice();
assert.equal(rhythms[140],16);assert.equal(rhythms[151+140],17);
assert.equal(rhythms[10],1);assert.equal(rhythms[151+10],1);
const show=serializeShow(payload,rhythms);await writeFile(join(folder,'Bolero-Conga-Show.cpshow'),JSON.stringify(show)+'\n',{flag:'wx'});
const decoded=showPayload(show),again=boot(decoded.payload,decoded.rhythms);
const samples=[],clips=[];
for(let slot=0;slot<2;slot++){
  again.bench_input(1<<7,1<<slot,0);again.bench_audio();again.bench_input(1<<8,0,0);again.bench_audio();
  assert.equal(again.bench_rhythm_export(),0);
  const bytes=new Uint8Array(again.memory.buffer,again.bench_rhythm_buffer(),140).slice();
  assert.deepEqual(bytes,rhythms.slice(slot*151,slot*151+140));
  again.bench_read_view();const v=new Uint8Array(again.memory.buffer,again.bench_snapshot()+20,76);
  assert.equal(v[1],5);assert.equal(v[8],16);assert.equal(v[6],76);assert.equal(v[7],0);
  again.bench_input(1<<12,0,0);const frames=Math.ceil(8*60*44100/76),start=samples.length;let peak=0;
  for(let n=0;n<frames;n+=128){const pcm=new Int32Array(again.memory.buffer,again.bench_audio(),256);
    for(let i=0;i<Math.min(128,frames-n);i++){assert.equal(pcm[i*2],pcm[i*2+1]);const value=pcm[i*2]/dacScale(b);peak=Math.max(peak,Math.abs(value));samples.push(value);}}
  assert(peak>100&&peak<32767);again.bench_input(1<<12,0,0);again.bench_audio();
  clips.push({name:set.songs[slot].title,sourceIndex:16+slot,startSeconds:start/44100,durationSeconds:frames/44100,peak});
  if(!slot)for(let i=0;i<22050;i++)samples.push(0);
}
const wav=Buffer.alloc(44+samples.length*2);wav.write('RIFF');wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);
wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);wav.writeUInt32LE(44100,24);wav.writeUInt32LE(88200,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);wav.write('data',36);wav.writeUInt32LE(samples.length*2,40);
for(let i=0;i<samples.length;i++)wav.writeInt16LE(samples[i],44+i*2);
await writeFile(join(folder,'Bolero-Backing.wav'),wav,{flag:'wx'});
const result={sampleRate:44100,channels:1,bpm:76,feel:0,mutedOriginalConga:true,sourceOriginsRecalled:true,clips};
await writeFile(join(folder,'Bolero-Backing.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));
