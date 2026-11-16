import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {preparedPayload} from '../web/set-payload.mjs';
const read=path=>JSON.parse(readFileSync(new URL(path,import.meta.url)));
const studies=read('../src/score.json').studies;
const ids=['cdmx','mty-dub','tribal','villera','lima','chile','puebla','wepa','tecno-peru','ba-digital'];
test('regional expansion preserves legacy study IDs and adds four presets to the regional set without changing the factory order',()=>{
  assert.deepEqual(studies.slice(0,10).map(s=>s.id),['coast','mx','salsa','conjunto','tejano','bolero','son','merengue','pambiche','tribal']);
  const set=read('../board/set.json');
  assert.equal(set.songs.length,16);
  assert.deepEqual(set.songs.slice(10).map(s=>s.study),ids.slice(0,6));
  assert.equal(studies[15].id,'merengue-congas');
  assert.deepEqual(studies.slice(16,20).map(s=>s.id),ids.slice(6));
  assert.deepEqual(read('../repertoire/regional-audition-set.json').songs.map(s=>s.study),ids);
  const patterns=ids.map(id=>studies.find(s=>s.id===id).parts.map(p=>p.slice(1)));
  assert.equal(new Set(patterns.map(p=>JSON.stringify(p))).size,ids.length);
});
test('regional project and routing preserve pitched bass, scraper and saved edits',()=>{
  for(const id of ids){
    const s=studies.find(s=>s.id===id), i=studies.indexOf(s);
    assert.equal(s.parts[5][1],'bass');
    assert.equal(s.board.kit[5],0);
    assert.equal(s.board.assignMask&32,0);
    s.parts.forEach((p,r)=>{if(['scrape','pluck'].includes(p[1])){
      assert.equal(s.board.kit[r],0);
      assert.equal(s.board.assignMask&(1<<r),0);
    }});
    const payload=preparedPayload({format:1,songs:[{title:s.short,study:id,feel:0,root:7,kit:true,section:'break',levels:[89,72,68,61,55,80]}]},studies);
    assert.equal(payload[19+6],i);
    assert.equal(payload[19+45],7);
    assert.equal(payload[81],2);
  }
});
