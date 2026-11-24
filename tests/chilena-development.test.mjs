import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';

const read=p=>JSON.parse(readFileSync(new URL(p,import.meta.url)));
const studies=read('../src/score.json').studies, ledger=read('../repertoire/chilena-source-ledger.json');
const song=read('../repertoire/chilena-development-set.json').songs[0];
const s=studies.find(s=>s.id===ledger.study);
const events=t=>t.split(/\s+/).filter(Boolean).map(x=>Number(x.match(/^\d+/)[0]));
const onsets=bars=>bars.flatMap((durations,bar)=>{
  let at=bar*12;return durations.map(d=>{const before=at;at+=d*2;return before;});
});

test('Costa Chica accompaniment retains both hands of the named 6/8 source cell',()=>{
  assert.deepEqual(s.meter,[6,8]);assert.equal(s.bpm,ledger.authored.pulseBpm);assert.equal(s.feel,0);
  assert.deepEqual(onsets(ledger.sourceRhythm.rightHandDurationsInEighths),ledger.sourceRhythm.rightHandOnsets);
  assert.deepEqual(onsets(ledger.sourceRhythm.leftHandDurationsInEighths),ledger.sourceRhythm.leftHandOnsets);
  for(const phase of [2,3]) {
    assert.deepEqual(events(s.parts[1][phase]),ledger.sourceRhythm.rightHandOnsets);
    assert.deepEqual(events(s.parts[5][phase]),ledger.sourceRhythm.leftHandOnsets);
  }
  assert.deepEqual(s.parts[5][2].split(' ').map(t=>Number(t.split(':')[1])),ledger.authored.relativeBassPitches);
  assert.equal(s.bpm*1.5,ledger.authored.midiQuarterBpm);
  assert.equal(4*60/s.bpm,ledger.authored.phraseSeconds);
  assert.equal(s.pulseGroups,undefined,'simultaneous cross rhythm does not change the visible 6/8 pulse');
  assert.equal(s.board.assignMask,0);assert.equal(song.kit,false);
  assert.deepEqual(s.break,['Strum cue','Bass']);
  assert.match(ledger.source.context,/not field notation/);
});

test('New compound entry leaves all 36 previously measured studies byte-identical',()=>{
  const publicRows=studies.filter(s=>!s.testOnly);
  assert.ok(publicRows.length>=37);assert.equal(publicRows[36].id,s.id);
  assert.equal(createHash('sha256').update(JSON.stringify(publicRows.slice(0,36))).digest('hex'),
    '9eba5a6054a26158d3a7b1cede7942e764d5bb9371dd8327eb8776fd4ef64413');
  assert.ok(studies.slice(publicRows.length).every(s=>s.testOnly));
  const payload=preparedPayload({format:1,songs:[song]},studies);
  assert.equal(payload[25],36);assert.deepEqual([...payload.slice(34,40)],song.levels);
});
