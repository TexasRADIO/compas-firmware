import {dacScale} from '../web/dac-format.mjs';
// Exact exported lane masks and private listening reel from the board WASM.
import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import assert from 'node:assert/strict';
const folder=process.argv[2];assert(folder,'Pass a new output directory');
const module=await WebAssembly.compile(await readFile(new URL('../build/board/browser-board.wasm',import.meta.url)));
const expected=[
  [0,0,0x05550555,0x50005000,0],
  [0,0,0x04150555,0x50005000,0x01400000],
  [0,0,0x05550415,0x50005000,0x00000140],
  [0x45554555,0x10001000,0,0,0],
  [0x45554555,0x10001000,0x04150555,0x50005000,0x01400000],
  [0x45554555,0x10001000,0x05550415,0x50005000,0x00000140],
  [0,0,0x01400141,0x10050000,0x00001000],
  [0,0,0x01400141,0x10150000,0x00001000],
  [0,0,0x01550155,0x10001000,0x44004400],
  [0,0,0x01550555,0x10005000,0x44000000]
];
const names=['Tumbao-One','Tumbao-Two-23','Tumbao-Two-32','Martillo','Son-Perc-23','Son-Perc-32','Merengue-Conga','Merengue-Conga-Variation','Bolero-Conga','Bolero-Space'];
const samples=[],clips=[];
for(const [n,masks] of expected.entries()) {
  const b=new WebAssembly.Instance(module).exports;
  assert.equal(b.bench_boot(0),1);b.bench_audio();b.bench_master(100);b.bench_audio();
  const enc=new Int32Array(b.memory.buffer,b.bench_encoders(),7);
  const input=(buttons,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();};
  input(1<<7,1<<1);input(1<<8); // Cumbia Electrica, two-bar 4/4.
  enc[3]=-100;input(0); // Band KNOB2 Feel -> 0.
  input(1<<13);input(1<<11);
  for(let i=0;i<9+n;i++) {enc[6]=1;input(0);}
  input(1<<2); // Layer -> Drums Only.
  assert.equal(b.bench_rhythm_export(),0);
  const bytes=new Uint8Array(b.memory.buffer,b.bench_rhythm_buffer(),140).slice();
  const v=new DataView(bytes.buffer);assert.equal(bytes[10],2);assert.equal(bytes[6],0);
  assert.deepEqual(Array.from({length:15},(_,i)=>v.getUint32(16+i*8,true)),[...masks,...Array(10).fill(0)]);
  if(n>=6) assert.deepEqual(Array.from({length:15},(_,i)=>v.getUint32(20+i*8,true)),[0,0,n<8?0x01000101:0x00100010,masks[3],masks[4],...Array(10).fill(0)]);
  await writeFile(join(folder,names[n]+'.c7r'),bytes,{flag:'wx'});
  b.bench_input(1<<12,0,0);
  const frames=Math.ceil(16*60*44100/bytes[5]),start=samples.length;
  let peak=0,sum=0;
  for(let offset=0;offset<frames;offset+=128) {
    const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let i=0;i<Math.min(128,frames-offset);i++) {
      assert.equal(pcm[2*i],pcm[2*i+1]);
      const value=pcm[2*i]/dacScale(b);assert(value>-32768&&value<32767);
      peak=Math.max(peak,Math.abs(value));sum+=value*value;samples.push(value);
    }
  }
  assert(peak>100);
  clips.push({name:names[n],presetIndex:8+n,bpm:bytes[5],feel:bytes[6],startSeconds:start/44100,durationSeconds:frames/44100,peak,rms:Math.sqrt(sum/frames)});
  if(n<expected.length-1)for(let i=0;i<22050;i++)samples.push(0);
}
const wav=Buffer.alloc(44+samples.length*2);
wav.write('RIFF',0);wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);
wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);
wav.writeUInt32LE(44100,24);wav.writeUInt32LE(88200,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);
wav.write('data',36);wav.writeUInt32LE(samples.length*2,40);
for(let i=0;i<samples.length;i++)wav.writeInt16LE(samples[i],44+i*2);
await writeFile(join(folder,'Latin-Percussion-Audition.wav'),wav,{flag:'wx'});
await writeFile(join(folder,'Latin-Percussion-Audition.json'),JSON.stringify({sampleRate:44100,channels:1,clips},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({frames:samples.length,clips}));
