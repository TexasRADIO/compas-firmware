import {dacScale} from '../web/dac-format.mjs';
// Audition the panel's complete bank and compare every saved row with Set Builder.
import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import assert from 'node:assert/strict';
const folder=process.argv[2];assert(folder,'Pass a prepared output directory with Complete-Rhythms.seed.bin');
const seed=await readFile(join(folder,'Complete-Rhythms.seed.bin'));
const entries=JSON.parse(await readFile(new URL('../repertoire/rhythm-bank-set.json',import.meta.url),'utf8')).songs;
const module=await WebAssembly.compile(await readFile(new URL('../build/board/browser-board.wasm',import.meta.url)));
const samples=[],clips=[];
for(const [index,entry] of entries.entries()) {
 const b=new WebAssembly.Instance(module).exports;
 assert.equal(b.bench_boot(0),1);b.bench_audio();b.bench_master(80);b.bench_audio();
 const enc=new Int32Array(b.memory.buffer,b.bench_encoders(),7);
 const input=buttons=>{b.bench_input(buttons,0,0);b.bench_audio();};
 input(1<<13);enc[6]=index||-1;input(0);
 input((1<<7)|(1<<9)); // Explicitly capture the unslotted rhythm into slot 1.
 assert.equal(b.bench_show_export(),0);
 const row=new Uint8Array(b.memory.buffer,b.bench_set_buffer(),86);
 assert.deepEqual(Buffer.from(row),seed.subarray(0x90100+index*86,0x90100+(index+1)*86));
 assert.equal(b.bench_rhythm_export(),0);
 const rhythm=new Uint8Array(b.memory.buffer,b.bench_rhythm_buffer(),140);
 const bpm=rhythm[5],frames=Math.ceil(8*60*44100/bpm),start=samples.length;
 input(1<<8);b.bench_input(1<<12,0,0);
 let peak=0,sum=0;
 for(let offset=0;offset<frames;offset+=128) {
  const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
  for(let i=0;i<Math.min(128,frames-offset);i++) {
   assert.equal(pcm[2*i],pcm[2*i+1]);const value=pcm[2*i]/dacScale(b);
   assert(value>-32768&&value<32767);peak=Math.max(peak,Math.abs(value));sum+=value*value;samples.push(value);
  }
 }
 assert(peak>100);
 clips.push({index,title:entry.title,study:entry.study,bpm,master:80,startSeconds:start/44100,durationSeconds:frames/44100,peak,rms:Math.sqrt(sum/frames)});
 if(index<entries.length-1)for(let i=0;i<22050;i++)samples.push(0);
}
const wav=Buffer.alloc(44+samples.length*2);
wav.write('RIFF',0);wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);
wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);
wav.writeUInt32LE(44100,24);wav.writeUInt32LE(88200,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);
wav.write('data',36);wav.writeUInt32LE(samples.length*2,40);
for(let i=0;i<samples.length;i++)wav.writeInt16LE(samples[i],44+i*2);
await writeFile(join(folder,'Complete-Rhythms-Audition.wav'),wav,{flag:'wx'});
await writeFile(join(folder,'Complete-Rhythms-Audition.json'),JSON.stringify({sampleRate:44100,channels:1,source:'Compas complete bank; original arrangements; not artist recordings',clips},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({presets:clips.length,allRowsMatchPreparedSet:true,frames:samples.length,clips}));
