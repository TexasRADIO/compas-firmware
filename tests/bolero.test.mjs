import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const ledger=JSON.parse(readFileSync(new URL('../repertoire/bolero-727-cells.json',import.meta.url)));

test('bolero preserves the printed one-bar low-high-low conversation',()=>{
  // First image: eight beamed eighths, with the author's H T S T H O O O labels.
  const bar=[[0,'high','H'],[2,'high','T'],[4,'high','S'],[6,'high','T'],
    [8,'high','H'],[10,'low','O'],[12,'high','O'],[14,'low','O']];
  assert.deepEqual(ledger.patterns[0].events,[...bar,...bar.map(([s,r,t])=>[s+16,r,t])]);
  assert.equal(ledger.patterns[0].sourceBars,1);
  assert.deepEqual(ledger.adaptation['low:O'],[4,true]);
  assert.deepEqual(ledger.adaptation['high:O'],[3,true]);
});

test('bolero spacious phrase keeps two printed bars, without inventing slash-note stickings',()=>{
  const events=ledger.patterns[1].events;
  assert.equal(ledger.patterns[1].sourceBars,2);
  assert.deepEqual(events.map(e=>e[0]),Array.from({length:16},(_,i)=>i*2));
  assert.deepEqual(events.filter(e=>e[2]==='O'),
    [[12,'high','O'],[14,'high','O'],[26,'low','O'],[28,'high','O'],[30,'low','O']]);
  assert.deepEqual(events.filter(e=>e[2]==='S'),[[4,'high','S'],[20,'high','S']]);
  assert.equal(events.filter(e=>e[2]==='HT').length,9);
  assert.deepEqual(ledger.adaptation['high:HT'],[2,false]);
});
