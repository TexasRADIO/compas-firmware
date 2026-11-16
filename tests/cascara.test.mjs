// The salsa cáscara is an eighth-note figure: every base hit sits on an even
// sixteenth, and it reads x.xx.x.x | x.x.xx.x against the study's 3-2 son clave.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const score=JSON.parse(readFileSync(new URL('../src/score.json',import.meta.url)));
const salsa=score.studies.find(s=>s.id==='salsa');
const steps=text=>text.split(/\s+/).filter(Boolean).map(t=>parseInt(t,10));
const part=name=>salsa.parts.find(p=>p[0].normalize('NFD').replace(/\p{M}/gu,'')===name);
test('cáscara base sits on the eighth-note grid', ()=>{
  const hits=steps(part('Cascara')[2]);
  assert(hits.length && hits.every(h=>h%2===0), `odd sixteenths in ${hits}`);
});
test('cáscara is the standard figure against 3-2 son clave', ()=>{
  const eighths=steps(part('Cascara')[2]).map(h=>h/2);
  const figure=Array.from({length:16},(_,i)=>eighths.includes(i)?'x':'.').join('');
  assert.equal(figure,'x.xx.x.xx.x.xx.x');
  assert.deepEqual(steps(part('Clave')[2]),[0,6,12,20,24]); // 3-2: the three side first
});
