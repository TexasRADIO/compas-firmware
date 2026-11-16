// Exercise the six review fixes through the real board and incremental stage renderer.
import assert from 'node:assert/strict';
import {bench,color} from './lib/bench.mjs';

const pink=color(255,46,136), cyan=color(34,224,255), lilac=color(180,107,255);
const dim=color(107,92,122), cream=color(255,248,236), gold=color(255,230,0);
const dim2=color(138,122,156); // VISUAL-2: secondary text

const groove={title:'CUMBIA',study:'mx',bpm:180};
const score={title:'GOLONDRINA',study:'la-golondrina-piano-bolero',bpm:180};
const band=bench([groove]);
assert.equal(band.pixels(5,10,1,1)[0],gold);
band.turn(0,1); // Bass
assert.equal(band.pixels(65,10,1,1)[0],gold);
assert.notEqual(band.pixels(5,10,1,1)[0],gold);
band.turn(0,-1);
const ready=band.pixels(5,94,132,9);
assert(ready.includes(lilac) && !ready.includes(pink));
const dial=band.pixels(64,197,13,13);
band.turn(3,35);
const turned=band.pixels(64,197,13,13);
assert(dial.includes(cream) && turned.includes(cream));
assert.notDeepEqual(turned,dial);
band.input(1<<12);
band.input(0,1<<12); // Chorus
assert(band.pixels(189,113,48,9).includes(pink));
band.until(v=>v[44]===3 && v[45]===6);
const landed=band.pixels(189,113,48,9);
assert(landed.includes(dim2) && !landed.includes(pink));

const bass=bench([groove]);
bass.turn(0,1);
const waveform=bass.pixels(104,125,128,20);
bass.turn(2,-45);
assert.notDeepEqual(bass.pixels(104,125,128,20),waveform);
bass.until(v=>v[1]===1);
const quiet=bass.pixels(22,82,61,61);
assert(bass.input(1<<12)[59]);
assert.notDeepEqual(bass.pixels(22,82,61,61),quiet);
bass.turn(5,1);
assert.notDeepEqual(bass.pixels(104,125,128,20),waveform);

const percussion=bench([groove]);
percussion.turn(0,2);
const mutedConga=percussion.pixels(9,105,42,33);
percussion.input(0,1<<8);
assert.notDeepEqual(percussion.pixels(9,105,42,33),mutedConga);
const beforeHit=percussion.pixels(9,105,42,33);
percussion.input(1<<12);
percussion.until(v=>v[35]&1);
assert.notDeepEqual(percussion.pixels(9,105,42,33),beforeHit);

const pads=bench([groove]);
pads.input(1<<13);
const open=3, muted=2;
const readyPad=pads.pixels(178,49,55,28);
pads.input(0,1<<open);
assert.equal(pads.rack().getUint16(0,true),1<<open);
assert.notDeepEqual(pads.pixels(178,49,55,28),readyPad);
pads.input(0,1<<muted);
assert.equal(pads.rack().getUint16(2,true),1<<open);
assert(pads.pixels(192,59,27,10).includes(pink));

const lead=bench([{...groove,countIn:true,phrases:1},{...groove,title:'NEXT'}]);
assert.equal(lead.input(1<<12)[33],3);
assert(lead.pixels(189,113,48,9).includes(pink));
lead.input(0,1<<12);
const first=lead.until(v=>v[33]===1);
assert.equal(first[44],3); // The queued part starts on the first downbeat.
assert.equal(first[58],1);
assert(lead.pixels(189,113,48,9).includes(cyan));

const count=bench([{...groove,phrases:12},{...groove,title:'NEXT'}]);
count.input(1<<12);
assert(count.pixels(225,113,12,9).includes(cyan)); // Seven characters in “11 MORE”.
const last=bench([{...groove,phrases:1},{...groove,title:'NEXT'}]);
last.input(1<<12);
assert(last.pixels(189,113,48,9).includes(cyan));

const show=bench([score,groove],{sparse:true});
const hold=show.pixels(127,213,49,18);
assert(hold.includes(dim) && !hold.includes(cream));
show.turn(0,1); // Bass
assert(show.pixels(29,82,101,101).includes(dim));
show.turn(0,1); // Percussion
assert(show.pixels(19,118,20,7).includes(pink));
show.input(0,1<<13); // Third upper key disables the bolero.
assert(!show.pixels(19,118,20,7).includes(pink));
assert(show.pixels(19,143,20,7).includes(dim));
show.input(1<<8); // Home
show.input(1<<12); // Play
assert(show.pixels(127,213,49,18).includes(cream));
show.input(1<<11); // Written coda
const next=show.until(v=>!v[0]);
assert.equal(next[28],15); // Skip the empty slots.
show.input(1<<12);
show.input(1<<12);
assert.equal(show.until(v=>!v[0])[28],15); // Manual Stop holds the slot.
show.input(1<<12);
show.input(0,1<<16); // Groove ending on the last slot.
assert.equal(show.until(v=>!v[0])[28],15);
console.log('Review fixes pass on the compiled board: horn, repeat, Ready, Hold, knobs, score pages and endings.');
