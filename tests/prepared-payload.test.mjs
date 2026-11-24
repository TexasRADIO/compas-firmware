import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import test from 'node:test';
import {preparedPayload} from '../web/set-payload.mjs';

const data = name => JSON.parse(readFileSync(new URL(name, import.meta.url), 'utf8'));
const studies = data('../src/score.json').studies;
const scores = data('../repertoire/board-scores.json');
const catalog = studies.concat(scores.map((score, index) =>
  ({...score, piano:index+1, guide:Boolean(score.guide)})));

function nativePayload(set) {
  const result = spawnSync('python3', ['-B','-c',`
import json, runpy, sys
board_set = runpy.run_path("tools/board-set.py")
records = board_set["compile_set"](json.load(sys.stdin))
flash = board_set["virtual_flash"](records)
sys.stdout.buffer.write(flash[0x90100:0x90100 + 1376])
`], {input:JSON.stringify(set)});
  assert.equal(result.status, 0, result.stderr.toString());
  return result.stdout;
}

test('prepared browser payload matches the native NOR compiler', () => {
  const full = data('../board/set.json');
  const live = {title:'ÑANDÚ ELÉCTRICA',study:'salsa',bpm:103,feel:28,root:9,clave:1,
    kit:true,section:'break',phrases:4,levels:[73,24,12,68,44,99],mute:2};
  const partial = {format:1,songs:[
    {title:'PALOMA GUIADA',study:'la-paloma',guide:true,bpm:97}, live,
    {title:'EL MANISERO',study:'el-manisero-kempton-bass',guide:true},
    {title:'LA PALOMA PIANO',study:'la-paloma-piano',guide:true},
    {title:'GOLONDRINA PIANO',study:'la-golondrina-piano',guide:true},
    {title:'PAMBICHE',study:'pambiche',kit:true,section:'lift'},
    {title:'TRIBAL 132',study:'tribal',feel:0,section:'lift'}]};
  const filled = {format:1,songs:full.songs.concat(Array.from({length:16-full.songs.length},()=>live))};
  const golondrina = data('../repertoire/golondrina-rehearsal-set.json');
  assert.equal(catalog.find(s=>s.id==='la-golondrina-piano-bolero').piano,19);
  const teodora = data('../repertoire/ma-teodora-rehearsal-set.json');
  assert.equal(catalog.find(s=>s.id==='ma-teodora-1929').piano,20);
  for (const set of [full,partial,filled,golondrina,teodora])
    assert.deepEqual(Buffer.from(preparedPayload(set,catalog)),nativePayload(set));
});

test('malformed set fields and score changes are refused', () => {
  assert.throws(()=>preparedPayload({format:1,songs:[{title:'BAD',study:'missing'}]},catalog));
  assert.throws(()=>preparedPayload({format:1,songs:[
    {title:'BAD',study:'la-golondrina',section:'lift'}]},catalog),/score’s form/);
  assert.throws(()=>preparedPayload({format:1,songs:[
    {title:'BAD',study:'coast',levels:[1,1,1,1,1,101]}]},catalog),/mix/);
});

test('Cielito prepared set keeps its appended score ID across native and browser',()=>{
  assert.equal(catalog.find(s=>s.id==='cielito-lindo-piano').piano,7);
  const set=data('../repertoire/cielito-rehearsal-set.json');
  assert.deepEqual(Buffer.from(preparedPayload(set,catalog)),nativePayload(set));
});


test('all twelve score transpositions compile identically; groove offsets are refused',()=>{
  for(let transpose=-6;transpose<=5;++transpose) {
    const set={format:1,songs:[{title:'SINGER KEY',study:'la-paloma',guide:true,transpose}]};
    const bytes=preparedPayload(set,catalog);
    assert.equal(bytes[19+57],(transpose+12)%12);
    assert.deepEqual(Buffer.from(bytes),nativePayload(set));
  }
  for(const transpose of [-7,6,0.5,true]) {
    const set={format:1,songs:[{title:'BAD',study:'la-paloma',transpose}]};
    assert.throws(()=>preparedPayload(set,catalog),/transpose/);
  }
  assert.throws(()=>preparedPayload({format:1,songs:[{title:'BAD',study:'mx',transpose:1}]},catalog),/transpose/);
});

test('groove count-in shares the score guide byte without changing old set records', () => {
  const set={format:1,songs:[{title:'WITH COUNT',study:'mx',countIn:true},
    {title:'OLD SET',study:'mx'},{title:'WRITTEN SCORE',study:'la-paloma-piano'}]};
  const packed=preparedPayload(set,catalog);
  assert.deepEqual(Buffer.from(packed),nativePayload(set));
  assert.deepEqual([packed[85]>>7,packed[171]>>7,packed[257]>>7],[1,0,0]);
  set.songs[2].countIn=true;
  assert.throws(()=>preparedPayload(set,catalog),/count-in/);
});
