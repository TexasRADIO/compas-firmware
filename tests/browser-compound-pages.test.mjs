import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {stepEditorPage} from '../web/step-pages.mjs';

test('browser step editor keeps 16, 24 and 32-step studies whole and pages 48 steps', () => {
  assert.deepEqual(stepEditorPage(16), {page:0,pages:1,first:0,count:16});
  assert.deepEqual(stepEditorPage(24), {page:0,pages:1,first:0,count:24});
  assert.deepEqual(stepEditorPage(32), {page:0,pages:1,first:0,count:32});
  assert.deepEqual(stepEditorPage(48), {page:0,pages:3,first:0,count:16});
  assert.deepEqual(stepEditorPage(48,1), {page:1,pages:3,first:16,count:16});
  assert.deepEqual(stepEditorPage(48,2), {page:2,pages:3,first:32,count:16});
  assert.deepEqual(stepEditorPage(48,99), {page:2,pages:3,first:32,count:16});
});

test('browser panel labels and addresses every 12/8 page', () => {
  const html=readFileSync(new URL('../web/index.html', import.meta.url),'utf8');
  const panel=readFileSync(new URL('../web/panel.mjs', import.meta.url),'utf8');
  assert.match(html,/id="step-pages"/);
  assert.match(panel,/Steps '\+first\+'–'\+last/);
  assert.match(panel,/const index=page\.first\+bar\*16\+j/);
  assert.match(panel,/scores\[selected\]\?\.\[index\]/);
});
