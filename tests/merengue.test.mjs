import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {preparedPayload} from '../web/set-payload.mjs';
const read=p=>JSON.parse(readFileSync(new URL(p,import.meta.url)));
const ledger=read('../repertoire/merengue-727-cells.json');
const studies=read('../src/score.json').studies;

test('merengue conga source retains two complete bars and the one-note variation',()=>{
  // Independently read from the notation/key hashes recorded in the ledger.
  const source=[[0,'high','S'],[6,'high','B'],[8,'high','S'],[12,'low','O'],
    [16,'high','O'],[18,'high','O'],[22,'high','B'],[24,'high','S'],[28,'high','O']];
  assert.deepEqual(ledger.patterns[0].events,source);
  assert.deepEqual(ledger.patterns[1].events,[...source.slice(0,6),[20,'high','O'],...source.slice(6)]);
  assert.deepEqual(ledger.adaptation['high:B'],[2,false]);
  assert.deepEqual(ledger.adaptation['high:S'],[2,true]);
});

test('complete conga backing preserves source strokes through Hybrid and Lift',()=>{
  const s=studies[15];assert.equal(s.id,'merengue-congas');assert.equal(s.meter,4);
  assert.deepEqual(s.parts.slice(0,4).map(p=>p.slice(2,4)),
    [['6 22',''],['0! 8! 24!',''],['16! 18! 28!','20!'],['12!','']]);
  assert.deepEqual(s.board.kit,[3,3,4,5,0,0]);
  assert.equal(s.board.assignMask,15);assert.equal(s.parts[5][1],'bass');
  assert(!s.break.includes('Bass'));
  assert.deepEqual(studies.slice(0,15).map(s=>s.id),
    ['coast','mx','salsa','conjunto','tejano','bolero','son','merengue','pambiche','tribal','cdmx','mty-dub','villera','lima','chile']);
});

test('derecho and maco preserve the repeated body, hand and stick strokes',()=>{
  const {patterns,strokes}=read('../repertoire/merengue-derecho-maco.json');
  const set=read('../repertoire/merengue-audition-set.json');
  const packed=preparedPayload({format:set.format,songs:set.songs.slice(0,6)},studies);
  assert.deepEqual(patterns.map(p=>p.study),['derecho-primera','derecho-segunda','merengue-maco']);
  assert.deepEqual(patterns.map(p=>p.tambora['left-open']),[[],[15],[]]);
  assert.deepEqual(patterns.map(p=>p.tambora['right-rim']),
    [[3,6,7,10,11],[2,3,6,7,10,11],[0,4,8,12]]);
  const paired=(hits,accents=[])=>[0,16].flatMap(bar=>hits.map(hit=>
    String(bar+hit)+(accents.includes(hit)?'!':''))).join(' ');
  patterns.forEach((pattern,index)=>{
    const study=studies[20+index];
    assert.equal(study.id,pattern.study);
    assert.equal(set.songs[3+index].study,study.id);
    assert.equal(packed[(3+index)*86+25],20+index);
    assert.deepEqual(study.parts.slice(0,4),Object.entries(strokes).map(([stroke,[name,voice]])=>
      [name,voice,paired(pattern.tambora[stroke]),'','']));
    assert.deepEqual(study.parts[4],['Güira','scrape',paired(pattern.guira,pattern.guiraAccents),'','']);
    assert.equal(study.board.assignMask,0);
    assert.deepEqual(study.break,['Stick rim','Güira']);
  });
});

test('merenhouse keeps the maco bodies and adds a separate quarter-note kick',()=>{
  const study=studies[27], maco=studies[22];
  assert.equal(study.id,'merenhouse');
  for(const lane of [0,2,3,4]) assert.deepEqual(study.parts[lane].slice(0,3),maco.parts[lane].slice(0,3));
  assert.deepEqual(study.parts[1].slice(0,3),['Club kick','kick','0! 4 8 12 16! 20 24 28']);
  assert.equal(study.parts[5][1],'bass');
  assert.equal(study.board.assignMask,0);
  const set=read('../repertoire/merenhouse-rehearsal-set.json');
  assert.deepEqual(set.songs.map(song=>song.mute??0),[34,32,0]);
  const payload=preparedPayload(set,studies);
  for(const [slot,mask] of [34,32,0].entries()) {
    const offset=slot*86;
    assert.equal(payload[offset+25],27);
    assert.equal(payload[offset+28],mask);
  }
});
