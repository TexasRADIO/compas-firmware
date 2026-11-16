// Assemble the documented cells with the existing accompaniment through panel controls.
// Literal source masks below independently check the compiled bank and saved show.
import {writeFileSync} from 'node:fs';
import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';
const output=process.argv[2];
assert(output,'Pass --show-only or an existing empty output directory');
const showOnly=output==='--show-only';
const read=async p=>JSON.parse(await readFile(new URL(p,import.meta.url)));
const recipe=await read('../repertoire/son-salsa-source-set.json');
const ledger=await read('../repertoire/afro-cuban-727-cells.json');
const studies=(await read('../src/score.json')).studies;
const prefix=(await read('../repertoire/cr8000-conga-excerpts.json')).patterns.length;
assert.equal(ledger.source.sha256,recipe.sourceSHA256);
const module=await WebAssembly.compile(await readFile(new URL('../build/board/browser-board.wasm',import.meta.url)));
const literal=[
 [0x45554555,0x10001000,0x05550415,0x50005000,0x00000140],
 [0x45554555,0x10001000,0x04150555,0x50005000,0x01400000],
 [0,0,0x05550415,0x50005000,0x00000140],
 [0,0,0x04150555,0x50005000,0x01400000]
];
const sources=recipe.arrangements.map(a=>{
 const index=ledger.presets.findIndex(p=>p.id===a.rhythm);assert(index>=0);
 const s=studies.find(s=>s.id===a.song.study);
 const muted=s.parts.flatMap((p,i)=>a.song.mute&(1<<i)?[p[0]]:[]);
 assert.deepEqual(muted,a.mutedRoles);assert(!(a.song.mute&32));
 assert.equal(a.song.clave,a.rhythm.endsWith('23')?1:0);
 return prefix+index;
});
function boot(payload,rhythms){
 const b=new WebAssembly.Instance(module).exports;
 new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
 if(rhythms)new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),rhythms.length).set(rhythms);
 assert.equal(b.bench_boot(rhythms?3:1),1);b.bench_audio();b.bench_master(80);b.bench_audio();return b;
}
function input(b,buttons=0,keys=0){b.bench_input(buttons,keys,0);b.bench_audio();}
function turn(b,encoder,delta){new Int32Array(b.memory.buffer,b.bench_encoders(),7)[encoder]=delta;input(b);}
function view(b){b.bench_read_view();return new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,76).slice();}
function rhythm(b){assert.equal(b.bench_rhythm_export(),0);return new Uint8Array(b.memory.buffer,b.bench_rhythm_buffer(),140).slice();}
function verify(b,slot){
 const a=recipe.arrangements[slot], v=view(b), r=rhythm(b), d=new DataView(r.buffer);
 assert.equal(v[1],studies.findIndex(s=>s.id===a.song.study));assert.equal(v[8],a.song.mute);
 assert.equal(v[39]&1,a.song.clave);assert.equal(v[7],0);assert.equal(v[6],a.song.bpm);
 assert.equal(v[70],sources[slot]);assert.equal(r[10],1); // Layer, not Drums Only.
 const hits=Array.from({length:15},(_,i)=>d.getUint32(16+i*8,true));
 assert.deepEqual(hits,[...literal[slot],...Array(10).fill(0)]);
 assert.deepEqual(Array.from({length:15},(_,i)=>d.getUint32(20+i*8,true)),hits.map((h,i)=>i===2?0x00100010:[1,3,4].includes(i)?h:0));
 // The accompaniment timeline matches the attached cell direction.
 const clave32=0x01101041, clave23=((clave32<<16)|(clave32>>>16))>>>0;
 assert.equal(new DataView(v.buffer).getUint32(40,true),a.song.clave?clave23:clave32);
 return r;
}
const b=boot(preparedPayload({format:1,songs:recipe.arrangements.map(a=>a.song)},studies));
for(let slot=0;slot<4;slot++){
 input(b,1<<7,1<<slot);input(b);input(b,1<<13);input(b,1<<11);
 for(let n=0;n<=sources[slot];n++)turn(b,6,1);
 input(b,1<<8);assert.equal(view(b)[70],sources[slot]);verify(b,slot);input(b,1<<7);input(b,1<<9);
}
input(b,1<<7,1);input(b);
assert.equal(b.bench_show_export(),0);
const set=new Uint8Array(b.memory.buffer,b.bench_set_buffer(),1360).slice();
const rhythms=new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),2416).slice();
sources.forEach((id,i)=>assert.equal(rhythms[i*151+140],id));
const show=serializeShow(set,rhythms);
const decoded=showPayload(show),again=boot(decoded.payload,decoded.rhythms);
function recall(slot) {
 input(again,1<<7,1<<slot);input(again);input(again,1<<8);
 assert.deepEqual(verify(again,slot),rhythms.slice(slot*151,slot*151+140));
}
if(showOnly) {
 for(let slot=0;slot<recipe.arrangements.length;++slot) recall(slot);
 writeFileSync(1,JSON.stringify(show)+'\n');
 process.exit(0);
}
await writeFile(join(output,'Son-Salsa-Source-Show.cpshow'),JSON.stringify(show)+'\n',{flag:'wx'});
const samples=[],clips=[];
for(let slot=0;slot<4;slot++){
 recall(slot);
 for(const [energy,name] of [[0,'Base'],[1,'Lift']]){
  turn(again,1,energy-view(again)[2]);
  input(again,1<<12);assert.equal(view(again)[2],energy);
  const frames=Math.ceil(8*60*44100/recipe.arrangements[slot].song.bpm),start=samples.length;let peak=0;
  for(let n=0;n<frames;n+=128){const pcm=new Int32Array(again.memory.buffer,again.bench_audio(),256);
   for(let i=0;i<Math.min(128,frames-n);i++){assert.equal(pcm[i*2],pcm[i*2+1]);const value=pcm[i*2]/128;peak=Math.max(peak,Math.abs(value));samples.push(value);}}
  assert(peak>100&&peak<32767);input(again,1<<12);verify(again,slot);
  const hash=createHash('sha256').update(JSON.stringify(samples.slice(start))).digest('hex');
  if(energy)assert.notEqual(hash,clips.at(-1).sha256);
  clips.push({sha256:hash,name:recipe.arrangements[slot].song.title,energy:name,sourceIndex:sources[slot],startSeconds:start/44100,durationSeconds:frames/44100,peak});
  for(let i=0;i<22050;i++)samples.push(0);
 }
}
// Running PRESETS must change accompaniment orientation and attached source together.
input(again,1<<7,1);input(again);input(again,1<<8);input(again,1<<12);
for(let slot=1;slot<4;slot++){
 const before=view(again)[28];turn(again,6,1);assert.equal(view(again)[29],slot);
 let guard=3000;
 while(view(again)[28]===before&&guard--)again.bench_audio();
 assert(guard>0);assert.equal(view(again)[28],slot);verify(again,slot);assert.equal(view(again)[0],1);
}
input(again,1<<12);
const wav=Buffer.alloc(44+samples.length*2);wav.write('RIFF');wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);wav.writeUInt32LE(44100,24);wav.writeUInt32LE(88200,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);wav.write('data',36);wav.writeUInt32LE(samples.length*2,40);samples.forEach((s,i)=>wav.writeInt16LE(s,44+i*2));
await writeFile(join(output,'Son-Salsa-Backing.wav'),wav,{flag:'wx'});
const result={sampleRate:44100,channels:1,feel:0,sourceOrigins:sources,sourceMasks:true,accents:true,backingClaveAligned:true,replacedRolesNotDoubled:true,showRecall:true,livePresetCues:3,clips};
await writeFile(join(output,'Son-Salsa-Backing.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));
