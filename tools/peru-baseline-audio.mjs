// Compare every original arrangement sample for sample with qualified RC3.
// node tools/peru-baseline-audio.mjs BASELINE.wasm [CANDIDATE.wasm] [RECEIPT.json] [34|36|37|38]
import {readFile,writeFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {dacScale} from '../web/dac-format.mjs';
const root=new URL('../',import.meta.url),sha=b=>createHash('sha256').update(b).digest('hex');
const [oldPath,newPath=new URL('build/board/browser-board.wasm',root),out=new URL('build/peru-baseline-audio.json',root),countText='34']=process.argv.slice(2);
const count=Number(countText),pins={34:'75a97ffaf8d7b9fb7e3ce4da4e558b447913ab3747e38a1f19dbcee5afa4d3dc',
  36:'f8e5f261f1c05324eed1bc4cd310f63495d6d6a26bbe3304c29ac644f8abf251',
  37:'47dd3e23e7086434aecb07af3b639b023044790f50dd9270d11e2bf2d41ece7d',
  38:'a20726b0ef4ceb6fbd4adbccafec80f574c9cfbdb2b8eaa3dcc63ec303b74191'};
assert(pins[count],'only measured baselines are supported');
assert(oldPath,'baseline WASM required');
const oldBytes=await readFile(oldPath),newBytes=await readFile(newPath);
assert.equal(sha(oldBytes),pins[count],'measured baseline');
const modules=await Promise.all([oldBytes,newBytes].map(b=>WebAssembly.compile(b)));
const studies=JSON.parse(await readFile(new URL('src/score.json',root))).studies.filter(s=>!s.testOnly);
const receipts=[];let samples=0;
assert(studies.length>=count);
for(const study of studies.slice(0,count))for(const section of ['base','lift','break'])for(const kit of [false,true]) {
  const payload=preparedPayload({format:1,songs:[{study:study.id,title:'BASELINE',section,kit,feel:0}]},studies);
  const boards=modules.map(module=>{
    const b=new WebAssembly.Instance(module).exports;
    new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
    assert.equal(b.bench_boot(1),1);b.bench_master(90);b.bench_audio();b.bench_input(1<<12,0,0);
    return b;
  });
  const [numerator,denominator]=Array.isArray(study.meter)?study.meter:[study.meter,4];
  const steps=numerator*32/denominator,stepsPerPulse=denominator===8?6:4;
  const blocks=Math.ceil(steps*60*44100/(study.bpm*stepsPerPulse*128));const hash=createHash('sha256');
  for(let n=0;n<blocks;++n) {
    const pcm=boards.map(b=>{
      const words=new Int32Array(b.memory.buffer,b.bench_audio(),256),scale=dacScale(b);
      const normalized=Buffer.alloc(256*2);
      for(let i=0;i<words.length;++i){
        assert(words[i]%scale===0,'exact integer DAC packing');
        normalized.writeInt16LE(words[i]/scale,i*2);
      }
      return normalized;
    });
    assert(pcm[0].equals(pcm[1]),`${study.id} ${section} kit=${kit} block ${n}`);
    hash.update(pcm[1]);samples+=256;
  }
  receipts.push({study:study.id,section,kit,blocks,sha256:hash.digest('hex')});
}
const scales=modules.map(module=>dacScale(new WebAssembly.Instance(module).exports));
const result={status:'pass',scope:`${count} original studies, three Energy states, both palettes, complete two-bar phrases; exact stereo Q15 PCM before DAC packing`,
  baselineDacScale:scales[0],candidateDacScale:scales[1],dacAmplitudeRatio:scales[1]/scales[0],dacGainDb:20*Math.log10(scales[1]/scales[0]),
  baselineSha256:sha(oldBytes),candidateSha256:sha(newBytes),runs:receipts.length,pcmChannelSamplesCompared:samples,stereoFramesCompared:samples/2,physicalWrite:false,receipts};
await writeFile(out,JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,runs:result.runs,pcmChannelSamplesCompared:samples,stereoFramesCompared:samples/2}));
