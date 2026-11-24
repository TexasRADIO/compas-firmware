import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const root = new URL('../repertoire/peru-source-review/', import.meta.url);
const files = ['costa-norte-cell', 'cumbion-cell', 'open-hat-cell'];
const cells = files.map(name => JSON.parse(readFileSync(new URL(`${name}.json`, root))));

test('Peru cut-time auditions preserve source event spacing and two-bar duration without rounding', () => {
  for (const cell of cells) {
    const s = cell.sourceGrid, a = cell.adaptation;
    assert.equal(s.meter, '2/2');
    assert.equal(s.bars, 2);
    assert.equal(s.ticks, 32);
    assert.equal(s.tempoUnit, 'half note');
    assert.equal(s.sourceQuarterBpm, s.tempoBpm * 2);
    assert.equal(s.ticks * s.tempoBpm * 4,
      a.c7r.steps * s.sourceQuarterBpm * s.ticksPerQuarter,
      'source and machine duration in rational tempo units');
    for (const role of a.roles) {
      const source = s.roles.find(item => item.name === role.source);
      assert.ok(source, role.source);
      assert.ok(source.onsets.every(tick => tick % 2 === 0), 'off-grid source must not be rounded');
      assert.deepEqual(role.onsets, source.onsets.map(tick => tick / 2));
      assert.deepEqual(role.accented, [], 'no source dynamics are invented');
    }
  }
});

test('Peru cell files encode only the declared roles, with no bass, lead or invisible accompaniment', () => {
  for (let i = 0; i < files.length; ++i) {
    const cell = cells[i], bytes = readFileSync(new URL(`${files[i]}.c7r`, root));
    assert.equal(bytes.length, 140);
    assert.equal(bytes.toString('ascii', 0, 4), 'C7R1');
    assert.equal(bytes[4], 16);
    assert.equal(bytes[5], cell.sourceGrid.tempoBpm);
    assert.equal(bytes[6], 0, 'straight timing only');
    assert.equal(bytes[10], 2, 'Drums Only');
    const expected = new Array(15).fill(0);
    for (const role of cell.adaptation.roles) {
      const voice = Number(role.compas727.match(/voice (\d+)/)[1]);
      expected[voice] = role.onsets.reduce((word, step) => word | 1 << step, 0) >>> 0;
    }
    for (let voice = 0; voice < 15; ++voice) {
      assert.equal(bytes.readUInt32LE(16 + voice * 8), expected[voice]);
      assert.equal(bytes.readUInt32LE(20 + voice * 8), 0);
    }
  }
});

test('Cumbion keeps its sparse shell separate from the continuing upper role', () => {
  const [base, cumbion, hats] = cells;
  assert.deepEqual(cumbion.adaptation.roles[1].onsets, base.adaptation.roles[0].onsets);
  assert.equal(cumbion.adaptation.roles[0].onsets.length, 8);
  assert.equal(cumbion.adaptation.roles[1].onsets.length, 12);
  assert.match(hats.sourceGrid.tempoStatus, /NOT.*measured tempo/);
  assert.match(hats.adaptation.roles[1].honesty, /does not recreate an open hi-hat/);
});
