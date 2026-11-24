import {dacScale} from '../web/dac-format.mjs';
// Private listening reel from the same board WASM used by the browser.
import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import assert from 'node:assert/strict';
const folder=process.argv[2];assert(folder,'Pass a new output directory');
const module=await WebAssembly.compile(await readFile(new URL('../build/board/browser-board.wasm',import.meta.url)));
const samples=[],clips=[];
for(const rate of [100,75,78,60]) {
  const b=new WebAssembly.Instance(module).exports;
  assert.equal(b.bench_boot(0),1);b.bench_audio();b.bench_master(100);b.bench_audio();
  const enc=new Int32Array(b.memory.buffer,b.bench_encoders(),7);
  const input=(buttons,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();};
  input(1<<7,1<<10);input(1<<8);input(1<<2);enc[5]=rate-100;input(0);
  b.bench_input(1<<12,0,0);
  const frames=Math.ceil(8*60*44100/rate),start=samples.length;
  for(let offset=0;offset<frames;offset+=128) {
    const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let i=0;i<Math.min(128,frames-offset);i++)samples.push(pcm[2*i]/dacScale(b));
  }
  clips.push({study:'CDMX Sonidera',speedPercent:rate,originalBpm:100,effectiveBpm:rate,
    pitchSemitones:12*Math.log2(rate/100),startSeconds:start/44100,durationSeconds:frames/44100});
  if(rate!==60)for(let i=0;i<22050;i++)samples.push(0);
}
const wav=Buffer.alloc(44+samples.length*2);
wav.write('RIFF',0);wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);
wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);
wav.writeUInt32LE(44100,24);wav.writeUInt32LE(88200,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);
wav.write('data',36);wav.writeUInt32LE(samples.length*2,40);
for(let i=0;i<samples.length;i++)wav.writeInt16LE(samples[i],44+i*2);
await writeFile(join(folder,'Rebajada-Audition.wav'),wav,{flag:'wx'});
await writeFile(join(folder,'Rebajada-Audition.json'),JSON.stringify({sampleRate:44100,channels:1,clips},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({frames:samples.length,clips}));
