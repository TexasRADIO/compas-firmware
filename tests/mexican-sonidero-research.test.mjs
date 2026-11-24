import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = path => JSON.parse(readFileSync(new URL(path, import.meta.url)));
const studies = read('../src/score.json').studies;
const ledger = read('../repertoire/mexican-sonidero-research-ledger.json');
const audition = read('../repertoire/mexican-sonidero-family-set.json');

test('Mexican sonidero ledger keeps practice, repertoire, arrangement and transform claims separate', () => {
  assert.equal(ledger.schema, 'compas-sonidero-family-ledger/1');
  assert.match(ledger.scope, /not a claim that all entries share one rhythm/);
  for (const [id, source] of Object.entries(ledger.sources)) {
    assert.ok(id);
    assert.match(source.url, /^https:\/\//);
    assert.ok(source.claim.length > 30);
    assert.ok(source.doesNotEstablish.length > 20);
  }
  for (const lane of ledger.lanes) {
    assert.ok(lane.nextGate.length > 10);
    assert.ok(['authored-accompaniment-study', 'performance-control', 'repertoire-family', 'adjacent-distinct-scene', 'research-only'].includes(lane.kind));
    for (const ref of lane.sourceRefs) assert.ok(ledger.sources[ref], `${lane.id} cites known source ${ref}`);
    if (lane.existingStudy) assert.ok(studies.some(study => study.id === lane.existingStudy), `${lane.id} maps to a current study`);
  }
  const mixteca = ledger.lanes.find(lane => lane.id === 'mixteca-tecladistas-and-yaa-sii');
  assert.equal(mixteca.kind, 'research-only');
  assert.equal(mixteca.meterRequirement, 'compound-6-8-or-12-8');
  assert.match(ledger.rules.compoundMeter, /Do not flatten/);

  const circuit = ledger.lanes.find(lane => lane.id === 'central-sonidero-circuit');
  assert.equal(circuit.kind, 'research-only');
  assert.match(circuit.claim, /not evidence/i);
  assert.match(circuit.nextGate, /rather than multiplying presets/i);

  const hybrid = ledger.lanes.find(lane => lane.id === 'hybrid-digital-live-cumbia');
  assert.equal(hybrid.kind, 'research-only');
  assert.match(hybrid.nextGate, /open lead space/i);

  const editada = ledger.lanes.find(lane => lane.id === 'leon-cumbia-editada');
  assert.equal(editada.kind, 'research-only');
  assert.match(editada.nextGate, /do not add an EDITADA beat/i);

  const pueblaCategories = ledger.lanes.find(lane => lane.id === 'puebla-local-listening-categories');
  assert.equal(pueblaCategories.kind, 'research-only');
  assert.match(pueblaCategories.claim, /not yet three eligible Compás rhythm identities/i);
  assert.match(pueblaCategories.nextGate, /player-controlled phrase\/tempo transition/i);

  const wepa = ledger.lanes.find(lane => lane.id === 'san-luis-potosi-wepa');
  assert.deepEqual(wepa.sourceRefs, ['felini-regional-roles', 'colmex-wepa']);
  assert.match(wepa.claim, /deep-voice\/saludo layer remains live/i);
  assert.match(wepa.nextGate, /Potosí operator/i);
});

test('Mexican sonidero family audition keeps the documented distinctions audible', () => {
  const ids = ['cdmx', 'puebla', 'mty-dub', 'salsa', 'son', 'wepa', 'cumbia-sureste', 'tribal'];
  assert.equal(audition.format, 1);
  assert.deepEqual(audition.songs.map(song => song.study), ids);
  assert.equal(new Set(ids).size, ids.length);
  for (const song of audition.songs) {
    assert.ok(studies.some(study => study.id === song.study));
    assert.equal(typeof song.kit, 'boolean');
  }
  assert.ok(ledger.lanes.some(lane => lane.id === 'rebajada-transform' && lane.kind === 'performance-control'));
  assert.ok(!ids.some(id => id.includes('rebajada')));
  const sureste = studies.find(study => study.id === 'cumbia-sureste');
  assert.match(sureste.evidence, /Role-based Compás arrangement/);
  assert.match(sureste.note, /No named recording/);
  assert.equal(sureste.parts[5][1], 'bass');
});
