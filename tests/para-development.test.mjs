import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {preparedPayload} from '../web/set-payload.mjs';

const read = path => JSON.parse(readFileSync(new URL(path, import.meta.url)));
const studies = read('../src/score.json').studies;
const set = read('../repertoire/para-development-set.json');
const ledger = read('../repertoire/para-development-ledger.json');
const ids = ['carimbo-marapanim','tecnobrega-belem','lambada-para','guitarrada-para'];

test('Pará development arrangements are append-only, distinct and explicitly bass-free', () => {
  assert.deepEqual(studies.filter(study => ids.includes(study.id)).map(study => study.id), ids);
  assert.ok(ids.every(id => studies.findIndex(study => study.id === id) >= 29));
  assert.deepEqual(set.songs.map(song => song.study), ids);
  assert.deepEqual(ledger.entries.map(entry => entry.study), ids);
  for (const id of ids) {
    const study = studies.find(candidate => candidate.id === id);
    const entry = ledger.entries.find(candidate => candidate.study === id);
    assert.ok(study.evidence.includes(id === 'lambada-para' || id === 'guitarrada-para' ? 'Authored' : 'Source'));
    assert.match(entry.status, /SOURCE CELL|AUTHORED STUDY/);
    assert.match(entry.source.url, /^https:\/\//);
    assert.ok(entry.notClaimed.length >= 3);
    assert.equal(study.parts[5][1], 'none');
    assert.match(study.note, /No (automatic )?bass|No bass/);
    assert.notEqual(study.parts.map(part => part.slice(2)), studies[0].parts.map(part => part.slice(2)));
  }
});

test('Pará set serializes through the existing show format with all three Energy choices', () => {
  for (const song of set.songs) {
    for (const section of ['base', 'lift', 'break']) {
      const bytes = preparedPayload({format: 1, songs: [{...song, section}]}, studies);
      assert.equal(bytes.length, 1376);
      assert.equal(bytes[19 + 6], studies.findIndex(study => study.id === song.study));
      assert.equal((bytes[84]>>1)&3, ['base','lift','break'].indexOf(section));
    }
  }
});
