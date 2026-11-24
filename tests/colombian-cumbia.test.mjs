import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {preparedPayload} from '../web/set-payload.mjs';

const read=path=>JSON.parse(readFileSync(new URL(path,import.meta.url)));
const ledger=read('../repertoire/colombian-cumbia-cells.json');
const {studies}=read('../src/score.json');
const set=read('../repertoire/colombian-cumbia-audition-set.json');

test('the three Colombian cells retain distinct strokes and a common tied guache',()=>{
  const packed=preparedPayload(set,studies);
  const expected=[
    [[0,1,3,4,6],[2,6],[0,2,3,4,6,7],[0,2,3,4]],
    [[0,1,2,3,4,5,6],[2,6],[0,2,3,4,6,7],[0,2,3,4]],
    [[0,1,2,4,5,6],[1,2,5,6],[1,2,4,6],[0,2,3,5]],
  ];
  ledger.patterns.forEach((pattern,i)=>{
    const study=studies[23+i];
    assert.equal(study.id,pattern.study);
    assert.equal(set.songs[i].study,pattern.study);
    assert.equal(packed[i*86+25],23+i);
    assert.deepEqual([pattern.alegre,pattern.llamador,pattern.rimOdd,pattern.rimEven],expected[i]);
    assert.deepEqual(study.parts.map(part=>part[1]),['hand','skin','stick','low','scrape','shake']);
    assert.deepEqual(study.parts.map(part=>part[2].split(' ').map(s=>parseInt(s,10))),[
      [0,8,16,24].flatMap(bar=>expected[i][0].map(n=>bar+n)),
      [0,8,16,24].flatMap(bar=>expected[i][1].map(n=>bar+n)),
      [0,8,16,24].flatMap((bar,index)=>expected[i][index%2?3:2].map(n=>bar+n)),
      [14,30],
      [0,2,6,10,14,18,22,26,30],
      [0,8,16,24].flatMap(bar=>[0,2,3,4,6,7].map(n=>bar+n)),
    ]);
    assert(study.parts.every(part=>part[3]==='' && part[4]===''));
    assert.deepEqual(study.break,['Llamador','Guache']);
  });
});
