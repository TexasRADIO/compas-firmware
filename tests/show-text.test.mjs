import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync, readFileSync} from 'node:fs';
import {serializeShow, showPayload} from '../web/show-format.mjs';
import {showToText, textToShow} from '../web/show-text.mjs';

const roundTrip = file => {
  const original = showPayload(file);
  const text = showToText(original);
  const back = showPayload(textToShow(text));
  assert.deepEqual(back.payload, original.payload);
  assert.deepEqual(back.rhythms, original.rhythms);
  return text;
};

test('real shows round-trip byte for byte through compas-song/2', () => {
  for (const name of ['build/Son-Salsa-Source-Show.cpshow', 'build/Colombian-Cumbia-727.cpshow']) {
    if (!existsSync(name)) continue;
    const text = roundTrip(JSON.parse(readFileSync(name, 'utf8')));
    assert.match(text, /^compas-song\/2\n/);
    assert.doesNotMatch(text, /^slot \d+ raw$/m, `${name} should be fully readable`);
  }
});

test('hidden, accented and unexpressible slots all survive', () => {
  const set = new Uint8Array(1376), rhythms = new Uint8Array(2416);
  for (let s = 0; s < 16; ++s) { set[s * 86] = 127; rhythms[s * 151 + 140] = 255; }
  set.set([...'CUMBIA ELeCTRICA'].map(c => c.charCodeAt(0)), 0);   // a real-looking song needs a valid project…
  set.fill(9, 19, 86);                                            // …this one isn't, so it must fall back to raw
  const text = roundTrip(serializeShow(set, rhythms));
  assert.match(text, /^slot 1 raw$/m);
  assert.match(text, /^slot 2 hidden$/m);
});

test('a hand edit to a 727 lane lands in the bytes with a fresh CRC', () => {
  const name = 'build/Colombian-Cumbia-727.cpshow';
  if (!existsSync(name)) return;
  const text = showToText(showPayload(JSON.parse(readFileSync(name, 'utf8'))));
  const lane = text.match(/^    maracas\s+([xX.]+)$/m);
  assert.ok(lane, 'expects a 727 lane');
  const flipped = lane[1][0] === '.' ? 'x' + lane[1].slice(1) : '.' + lane[1].slice(1);
  const edited = text.replace(lane[0], lane[0].replace(lane[1], flipped));
  const back = showPayload(textToShow(edited));     // CRCs are checked by showPayload
  assert.notDeepEqual(back.rhythms, showPayload(textToShow(text)).rhythms);
});

test('bad text is refused with a plain reason', () => {
  assert.throws(() => textToShow('hello'), /compas-song\/1 or \/2/);
  assert.throws(() => textToShow('compas-song/1\nslot 1 hidden\n  band-raw 00'), /16 slots|bytes/);
});
