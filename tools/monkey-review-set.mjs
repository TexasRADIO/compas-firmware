import {dacScale} from '../web/dac-format.mjs';
// Build and play the focused set through the ordinary 1.3 board/show interface.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';
const root=new URL('../',import.meta.url),sha=b=>createHash('sha256').update(b).digest('hex');
const read=async p=>JSON.parse(await readFile(new URL(p,root)));
const set=await read('repertoire/monkey-test-pattern-review-set.json'),studies=(await read('src/score.json')).studies;
const bytes=await readFile(new URL('build/board/browser-board.wasm',root)),module=await WebAssembly.compile(bytes);
const out=new URL('build/monkey-test-review-20261121/Player/',root);await mkdir(out,{recursive:true});
const payload=preparedPayload(set,studies),results=[];let show;
for(const lang of [0,1]) {
  const b=new WebAssembly.Instance(module).exports;
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
  assert.equal(b.bench_boot(1),1);b.bench_lang(lang);b.bench_master(80);b.bench_audio();
  assert.equal(b.bench_show_export(),0);
  const saved=Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_set_buffer(),b.bench_set_size()));
  const rhythms=Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),b.bench_rhythm_slots_size()));
  const file=serializeShow(saved,rhythms),reopened=showPayload(file);
  assert.deepEqual(reopened.payload,saved);assert.deepEqual(reopened.rhythms,rhythms);
  if(show)assert.deepEqual(file,show);else show=file;
  for(let slot=0;slot<set.songs.length;++slot) {
    const song=set.songs[slot],s=studies.find(s=>s.id===song.study),id=studies.indexOf(s);
    assert.equal(b.bench_cue(slot),0);b.bench_audio();b.bench_read_view();
    const view=new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,88);assert.equal(view[1],id);assert.equal(view[28],slot);
    const compound=Array.isArray(s.meter),steps=compound?s.meter[0]*4:s.meter*8;
    const blocks=Math.ceil(steps*60*44100/(s.bpm*(compound?6:4)*128));
    b.bench_input(1<<12,0,0);b.bench_input(0,0,0);let peak=0;const hash=createHash('sha256');
    for(let n=0;n<blocks;++n) {
      const pcm=Buffer.from(b.memory.buffer,b.bench_audio(),256*4);hash.update(pcm);
      for(const sample of new Int32Array(pcm.buffer,pcm.byteOffset,256))peak=Math.max(peak,Math.abs(sample/dacScale(b)));
    }
    assert(peak>0&&peak<32767);assert.equal(b.bench_rails(),0);assert.equal(b.bench_fx_shed(),0);
    b.bench_input(1<<12,0,0);b.bench_input(0,0,0);b.bench_audio();assert.equal(b.bench_playing(),0);
    results.push({language:lang?'es':'en',slot:slot+1,study:s.id,id,steps,peak,blocks,pcmSha256:hash.digest('hex')});
  }
}
for(const r of results.filter(r=>r.language==='en'))assert.equal(r.pcmSha256,results.find(e=>e.language==='es'&&e.slot===r.slot).pcmSha256);
const cueJourneys=[];
for(const lang of [0,1]) {
  const b=new WebAssembly.Instance(module).exports;
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
  assert.equal(b.bench_boot(1),1);b.bench_lang(lang);b.bench_master(80);b.bench_audio();
  const view=()=>{b.bench_read_view();return Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,88));};
  b.bench_input(1<<12,0,0);b.bench_input(0,0,0);b.bench_audio();
  for(let i=0;view()[5]<1&&i<200;++i)b.bench_audio();
  assert.equal(b.bench_cue(7),0);b.bench_audio();assert.equal(view()[29],7);
  assert.equal(b.bench_cue(5),0);b.bench_audio();assert.equal(view()[29],5);
  const trace=[];
  for(const target of [5,7,0]) {
    if(target!==5){assert.equal(b.bench_cue(target),0);b.bench_audio();}
    let landed=false;
    for(let n=0;n<5000;++n){b.bench_audio();const v=view();
      assert(v[0]&&!v[14],'meter changes preserve running transport');
      if(v[28]===target){assert.equal(v[5],0);assert.equal(v[1],studies.findIndex(s=>s.id===set.songs[target].study));
        trace.push({slot:target+1,study:set.songs[target].study,landingStep:0});landed=true;break;}}
    assert(landed,'cross-meter cue lands');
    for(let i=0;view()[5]<1&&i<200;++i)b.bench_audio();
  }
  b.bench_input(1<<12,0,0);b.bench_audio();assert.equal(b.bench_playing(),0);assert.equal(b.bench_rails(),0);
  cueJourneys.push({language:lang?'es':'en',newestSongCueWins:true,meters:['4/4','2/4','6/8','4/4'],trace});
}
const showText=JSON.stringify(show,null,2)+'\n';
await writeFile(new URL('Compas-Pattern-Review.cpshow',out),showText);
const report={status:'pass',wasmSha256:sha(bytes),showSha256:sha(showText),showRoundTrip:'byte-identical',physicalWrite:false,results,cueJourneys};
await writeFile(new URL('focused-set-receipt.json',out),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({status:report.status,songs:set.songs.length,languages:2,wasmSha256:report.wasmSha256,showSha256:report.showSha256}));
