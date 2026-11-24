import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';

const read = path => JSON.parse(readFileSync(new URL(path,import.meta.url)));
const studies = read('../src/score.json').studies;
const ledger = read('../repertoire/peru-north-development-ledger.json');
const set = read('../repertoire/peru-north-development-set.json');
const events = text => text.split(/\s+/).filter(Boolean).map(token=>Number(token.match(/^\d+/)[0]));
const repeat = ticks => [...ticks.map(t=>t/2),...ticks.map(t=>t/2+8)];

test('North-coast arrangements preserve adopted percussion cells without importing a lead or invented source bass pitches', () => {
  assert.deepEqual(set.songs.map(s=>s.study),ledger.entries.map(e=>e.study));
  for(const entry of ledger.entries) {
    const s=studies.find(study=>study.id===entry.study);
    assert.equal(s.meter,2); assert.equal(s.bpm,entry.pulse);assert.equal(s.feel,0);
    assert.equal(entry.sourceQuarterBpm,s.bpm*2);
    assert.equal(s.parts.length,6);
    const source=[ledger.commonSourceBar.kick,ledger.commonSourceBar.upperPercussion,
      ledger.commonSourceBar.bongo,ledger.commonSourceBar.conga,entry.sourceShellBar];
    source.forEach((ticks,role)=>assert.deepEqual(events(s.parts[role][2]),repeat(ticks)));
    assert.deepEqual(events(s.parts[5][2]),entry.authoredBass.machineOnsets);
    assert.deepEqual(s.parts[5][2].split(' ').map(token=>Number(token.split(':')[1])),entry.authoredBass.relativePitches);
    assert.equal(s.board.kit[5],0);assert.equal(s.board.assignMask&32,0);
    assert.match(entry.authored,/Source bass pitches|recorded bass/i);
    assert.ok(entry.notClaimed.length>=5);
    assert.deepEqual(s.break,['Hat','Bass']);
  }
  const [motor,cumbion]=ledger.entries.map(e=>studies.find(s=>s.id===e.study));
  assert.notDeepEqual(events(motor.parts[4][2]),events(cumbion.parts[4][2]));
  assert.deepEqual(events(motor.parts[1][2]),events(cumbion.parts[1][2]));
});

test('Development append preserves the 34 qualified studies and keeps fixtures outside the normal catalogue', () => {
  const publicRows=studies.filter(s=>!s.testOnly);
  assert.ok(publicRows.length>=36);
  assert.deepEqual(publicRows.slice(34,36).map(s=>s.id),ledger.entries.map(e=>e.study));
  const digest=createHash('sha256').update(JSON.stringify(publicRows.slice(0,34))).digest('hex');
  assert.equal(digest,'e95e54acc43bef7f17f6bf8dbe96d3b51028e25cafe9474b2a806489e1d3e60e');
  assert.ok(studies.slice(publicRows.length).every(s=>s.testOnly));
});

test('North-coast review set keeps the authored mix and all three Energy choices in ordinary portable records', () => {
  for(const song of set.songs) for(const section of ['base','lift','break']) {
    const payload=preparedPayload({format:1,songs:[{...song,section}]},studies);
    assert.equal(payload.length,1376);
    assert.equal(payload[25],studies.findIndex(s=>s.id===song.study));
    assert.deepEqual([...payload.slice(34,40)],song.levels);
    assert.equal((payload[84]>>1)&3,['base','lift','break'].indexOf(section));
  }
});
