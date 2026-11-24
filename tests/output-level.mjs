// Real-bank converter comparison. This does not measure analogue voltage.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {dacScale} from '../web/dac-format.mjs';
const [baselinePath,receipt='build/output-level.json']=process.argv.slice(2);
assert(baselinePath,'pass the retained, qualified 38-study baseline WASM');
const bytes=[baselinePath,'build/board/browser-board.wasm'].map(p=>readFileSync(p));
const sha=b=>createHash('sha256').update(b).digest('hex');
assert.equal(sha(bytes[0]),'a20726b0ef4ceb6fbd4adbccafec80f574c9cfbdb2b8eaa3dcc63ec303b74191');
const modules=bytes.map(b=>new WebAssembly.Module(b));
function boot(module,pattern,settings,master=100){
  const b=new WebAssembly.Instance(module).exports;
  assert.equal(b.bench_boot(4),1);b.bench_master(master);
  assert.equal(b.bench_kit_restore(...settings),1);
  b.bench_audio();b.bench_input(0,1<<(4+pattern),0);b.bench_audio();b.bench_input(0,0,0);b.bench_audio();
  b.bench_input(1<<12,0,0);return b;
}
const rows=[];
for(const [name,settings] of [['factory',[0,100,12,0]],['maximum-fast',[100,100,24,0]],['maximum-slow',[100,100,24,54]]])
for(let pattern=0;pattern<4;++pattern){
  const boards=modules.map(m=>boot(m,pattern,settings));
  assert.deepEqual(boards.map(dacScale),[128,256]);
  let oldPeak=0,newPeak=0,squared=0,samples=0;
  for(let n=0;n<2400;++n){
    const pcm=boards.map(b=>new Int32Array(b.memory.buffer,b.bench_audio(),256));
    for(let i=0;i<256;++i){
      assert.equal(pcm[1][i],pcm[0][i]*2,'same musical samples, doubled final DAC word');
      assert(pcm[1][i]>=-8388608 && pcm[1][i]<=8388607);
      oldPeak=Math.max(oldPeak,Math.abs(pcm[0][i]));newPeak=Math.max(newPeak,Math.abs(pcm[1][i]));
      squared+=(pcm[1][i]/8388608)**2;++samples;
    }
  }
  assert(newPeak>0);assert.equal(boards[1].bench_rails(),0);
  rows.push({name,pattern,stereoFrames:samples/2,oldPeakDbfs:20*Math.log10(oldPeak/8388608),newPeakDbfs:20*Math.log10(newPeak/8388608),newRmsDbfs:10*Math.log10(squared/samples)});
}
const {BoardAudio}=await import('data:text/javascript;base64,'+Buffer.from(readFileSync('web/board-audio.mjs','utf8')+'\nexport {BoardAudio};').toString('base64'));
for(const rate of [44100,48000]){
  const owners=modules.map(module=>new BoardAudio({module,startupHanan:true},rate,()=>{}));
  owners.forEach(o=>{o.receive({master:100});o.receive({buttons:1<<12,keys:0,time:0});});
  for(let n=0;n<800;++n){
    const rendered=owners.map(o=>{const out=[new Float32Array(128),new Float32Array(128)];o.render(out);return out;});
    assert.deepEqual(rendered[0],rendered[1],'browser amplitude stays unchanged without clipping or a second gain');
  }
}
const result={status:'pass',baselineSha256:sha(bytes[0]),candidateSha256:sha(bytes[1]),dacScales:[128,256],gainDb:20*Math.log10(2),cases:rows,browserSampleRates:[44100,48000],analogueVoltageMeasured:false,physicalUnitTouched:false};
writeFileSync(receipt,JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));
