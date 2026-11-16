// Integration checks use actual panel snapshots from the compiled audio owner.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {keyPresentation} from '../web/key-presentation.mjs';
import {preparedPayload} from '../web/set-payload.mjs';
import {boardWasm} from './lib/bench.mjs';
const root=new URL('../',import.meta.url), read=p=>JSON.parse(readFileSync(new URL(p,root)));
const ledger=read('src/score.json');
const studies=ledger.studies.map(s=>({...s,conductorParts:ledger.conductorForms[s.conductor??'default']}));
const catalog=[...studies,...read('repertoire/board-scores.json').map((s,i)=>({...s,piano:i+1,guide:Boolean(s.guide)}))];
const module=new WebAssembly.Module(readFileSync(boardWasm));
function boot(study){const b=new WebAssembly.Instance(module).exports;const p=preparedPayload({format:1,songs:[{title:'KEY TEST',study}]},catalog);new Uint8Array(b.memory.buffer,b.bench_set_buffer(),p.length).set(p);assert.equal(b.bench_boot(1),1);b.bench_audio();return b;}
function state(b){b.bench_read_view();return keyPresentation(new Uint8Array(b.memory.buffer,b.bench_snapshot(),b.bench_snapshot_size()),catalog);}
function input(b,buttons=0,keys=0){b.bench_input(buttons,keys,0);b.bench_audio();}
function hit(b,key){input(b,0,1<<key);input(b);}
function turn(b,encoder,delta){new Int32Array(b.memory.buffer,b.bench_encoders(),7)[encoder]=delta;input(b);}
const b=boot('cdmx');
assert.deepEqual(state(b).keys.filter(k=>k.kind==='part').map(k=>k.name),['Intro','Verso','Coro','Puente','Break','Final']);
assert.equal(state(b).keys.filter(k=>k.kind==='drum').length,15);
assert.equal(state(b).keys[7].shortcut,'A');assert.equal(state(b).keys[17].name,'Bass');
assert.equal(state(b).keys[8].on,false);hit(b,8);assert.equal(state(b).keys[8].on,true);
input(b,1<<12);hit(b,12);assert.equal(state(b).keys[12].next,true);
for(let i=0;i<2500;i++)b.bench_audio();assert.equal(state(b).keys[12].current,true);
turn(b,0,1);assert.equal(state(b).keys[12].kind,'part');
input(b,1<<13);assert.equal(state(b).keys[0].name,'High bongo');assert.equal(state(b).keys[15].name,'Accent');assert.equal(state(b).keys[16].kind,'unused');
input(b,1<<11);assert.equal(state(b).keys[0].name,'Step 1');hit(b,0);assert.equal(state(b).keys[0].on,true);
input(b,1<<1);assert.equal(state(b).keys[0].name,'Step 17');
input(b,1<<7);assert.equal(state(b).keys[0].kind,'slot');assert.equal(state(b).keys[0].current,true);
input(b,1<<2);assert.equal(state(b).keys[0].name,'Mute Kick');assert.equal(state(b).keys[6].on,false);hit(b,6);assert.equal(state(b).keys[6].on,true);
const coast=boot('coast');assert.equal(state(coast).keys[17].kind,'unused');
const score=boot('la-paloma');assert.equal(state(score).keys[8].name,'Melody guide');assert.equal(state(score).keys[10].kind,'unused');assert.equal(state(score).keys[16].name,'Coda');assert.equal(state(score).keys[11].kind,'unused');
input(score,1<<12);assert.equal(state(score).keys[10].name,'Hold current phrase');hit(score,10);assert.equal(state(score).keys[10].on,true);
assert.equal(state(score).keys[13].kind,'unused');
const golondrina=boot('la-golondrina-piano-bolero');
assert.equal(state(golondrina).keys[13].name,'Bolero percussion');
assert.equal(state(golondrina).keys[13].on,true);
hit(golondrina,13);
assert.equal(state(golondrina).keys[13].on,false);
turn(golondrina,0,2);
hit(golondrina,13);
assert.equal(state(golondrina).keys[13].on,true);
const cielito=boot('cielito-lindo-piano');assert.equal(state(cielito).keys[7].name,'Pickup');assert.equal(state(cielito).keys[16].name,'Ending');
const teodora=boot('ma-teodora-1929');
assert.deepEqual(state(teodora).keys.filter(k=>k.kind==='part').map(k=>k.name),['Pickup','Call','Echoes','Close']);
assert.equal(state(teodora).keys[8].kind,'unused');
input(teodora,1<<12);
assert.equal(state(teodora).keys[10].name,'Hold current phrase');
console.log('Actual board snapshots: parts/cue/arrival, overlays, bass, 727 pads/steps/bar, slots, mute/solo, score guide/hold and compound-meter labels pass.');
