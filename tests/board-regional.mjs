import {dacScale} from '../web/dac-format.mjs';
// Run after board-browser.py: the focused set uses the real board and 727 bank.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';
import {boardWasm} from './lib/bench.mjs';
const json=async p=>JSON.parse(await readFile(new URL(p,import.meta.url),'utf8'));
const studies=(await json('../src/score.json')).studies;
const profiles={
  regional:['regional-audition-set.json','Compas-Regional-Cumbia.html',
    ['cdmx','mty-dub','tribal','villera','lima','chile','puebla','wepa','tecno-peru','ba-digital']],
  para:['para-development-set.json','Compas-Para-Review.html',
    ['carimbo-marapanim','tecnobrega-belem','lambada-para','guitarrada-para']],
  sonidero:['mexican-sonidero-family-set.json','Compas-Sonidero-Families.html',null]
};
const selected=profiles[process.argv[2]??'regional'];
assert(selected,'choose regional, para or sonidero');
const set=await json('../repertoire/'+selected[0]);
const module=await WebAssembly.compile(await readFile(boardWasm));
const ids=selected[2]??set.songs.map(song=>song.study);
assert.deepEqual(set.songs.map(s=>s.study),ids);
for(const [filename,expected] of [[selected[1],set],['Compas-Sonidero-Board.html',null]]) {
  const page=await readFile(new URL('../build/'+filename,import.meta.url),'utf8');
  assert.deepEqual(JSON.parse(page.match(/const startupSet=(.*);\n/)[1]),expected);
}
const payload=preparedPayload(set,studies);
function boot(bytes=payload) {
  const b=new WebAssembly.Instance(module).exports;
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),bytes.length).set(bytes);
  assert.equal(b.bench_boot(1),1); b.bench_audio();
  // Kumbia appends eight bytes to BoardView; the shared offsets stay fixed.
  assert.equal(b.bench_snapshot_size(),b.bench_kit_restore?140:132);
  b.bench_master(80); b.bench_audio();
  return b;
}
function view(b) {
  b.bench_read_view();
  return new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,80).slice();
}
function input(b,buttons=0,keys=0) {b.bench_input(buttons,keys,0);b.bench_audio();}
function turn(b,index,delta) {
  new Int32Array(b.memory.buffer,b.bench_encoders(),7)[index]=delta;input(b);
}
function render(b,blocks) {
  const hash=createHash('sha256');let peak=0;
  for(let n=0;n<blocks;n++) {
    const pcm=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let i=0;i<256;i+=2) {
      assert.equal(pcm[i],pcm[i+1]);peak=Math.max(peak,Math.abs(pcm[i]/dacScale(b)));
    }
    hash.update(Buffer.from(pcm.buffer,pcm.byteOffset,pcm.byteLength));
  }
  assert(peak>100 && peak<32767);
  assert.equal(view(b)[14],0);
  return {peak,sha256:hash.digest('hex')};
}
const results=[];
for(const song of set.songs) {
  const variants=[];
  for(const section of ['base','lift','break']) {
    const b=boot(preparedPayload({format:1,songs:[{...song,section}]},studies));
    input(b,1<<12);
    variants.push({section,...render(b,2400)});
    assert.equal(view(b)[2],['base','lift','break'].indexOf(section));
    input(b,1<<12);assert.equal(b.bench_playing(),0);
    assert.equal(b.bench_show_export(),0);
    const bytes=new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).slice();
    const rhythms=new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),2416);
    const reopened=boot(showPayload(serializeShow(bytes,rhythms)).payload);
    assert.equal(view(reopened)[1],studies.findIndex(s=>s.id===song.study));
    assert.equal(view(reopened)[2],view(b)[2]);
  }
  assert.equal(new Set(variants.map(v=>v.sha256)).size,3,song.study+' needs distinct energy states');
  results.push({study:song.study,variants});
}
// The focused set loops until the player cues; PRESETS keeps audio running.
const b=boot();input(b,1<<12);render(b,2400);
assert.equal(view(b)[28],0);
for(let i=1;i<ids.length;i++) {
  turn(b,6,1);
  assert.equal(view(b)[29],i);
  assert.equal(b.bench_playing(),1);
  render(b,2400);
  assert.equal(view(b)[28],i);
  assert.equal(view(b)[1],studies.findIndex(s=>s.id===ids[i]));
}
input(b,1<<12);assert.equal(b.bench_playing(),0);
console.log(JSON.stringify({studies:results,energyCases:3*ids.length,liveCues:ids.length-1,showRecall:true,stereo:true,clipped:false},null,2));
