import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const processor = readFileSync(new URL('../web/processor.mjs', import.meta.url), 'utf8');

test('browser worklet obtains its score width from the engine', () => {
  assert.match(processor, /const steps = e\.cp_status\(15\);/);
  assert.doesNotMatch(processor, /study === 3 \|\| study === 7/);
  assert.match(processor, /6\/8 \(24-step\).*12\/8 \(48-step\)/s);
});
