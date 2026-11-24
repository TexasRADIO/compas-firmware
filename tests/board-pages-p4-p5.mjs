// Pages V3 P4 (Band Energy gauge and finger-pad feedback) and P5 (GLO rows),
// exercised through the real board, panel and incremental stage renderer.
import assert from 'node:assert/strict';
import {bench,color} from './lib/bench.mjs';

const pink=color(255,46,136), cyan=color(34,224,255), lilac=color(180,107,255), gold=color(255,230,0);
const dim=color(107,92,122), line=color(44,34,56), white=0xffff, dim2=color(138,122,156);
const ORDER_TOP=40, ROW=9;
const GLO=1<<7, HOME=1<<8, PLAY=1<<12;

const VIEW={playing:0,pending:3,position:5,song:28};
const beatOf=v=>v[VIEW.position]>>2;

// ---------------------------------------------------------------- P4 Energy
const groove={title:'CUMBIA',study:'mx',bpm:120};
const band=bench([groove]);
band.input(PLAY);
band.until(v=>v[VIEW.playing] && beatOf(v)<6);
const bar=i=>band.pixels(120+i*10,92-(7+4*i),8,7+4*i);
assert(bar(0).includes(cyan) && !bar(1).includes(cyan), 'BASE lights one cyan bar');
assert(!bar(1).includes(pink), 'nothing is queued yet');
band.turn(1,1); // ALGORITHM up: LIFT queued for the phrase
let v=band.until(v=>v[VIEW.pending]===1 && !(beatOf(v)&1));
assert(bar(1).includes(pink), 'queued LIFT bar pulses pink on even beats');
assert(band.pixels(5,94,132,9).includes(pink), 'pending name below NOW');
band.until(v=>v[VIEW.pending]===1 && (beatOf(v)&1));
assert(!bar(1).includes(pink), 'and rests on odd beats');
band.turn(1,-1); // back to the current level cancels both indicators
v=band.until(v=>v[VIEW.pending]!==1 && !(beatOf(v)&1));
assert(!bar(1).includes(pink) && !bar(2).includes(pink));
assert(!band.pixels(5,94,132,9).includes(pink));
// A cue that lands lights the new level.
band.turn(1,1);
band.until(v=>v[VIEW.pending]!==1 && v[2]===1);
band.wait(30);
assert(bar(0).includes(gold) && bar(1).includes(gold), 'LIFT lights two gold bars');

// ---------------------------------------------------------------- P4 finger pads
const pads=bench([groove]);
const strip=sound=>sound<7?pads.pixels(2+sound*4,146,3,8):pads.pixels(209+(sound-7)*4,146,3,8);
assert(pads.pixels(3,177,18,7).includes(dim2) && pads.pixels(212,177,24,7).includes(dim2), 'BCT and ASWQ codes');
pads.input(0,1<<3); // Open conga, left bank
assert(strip(3).includes(white), 'the struck strip flashes white');
assert(pads.pixels(1,132,70,9).filter(p=>p!==color(11,7,16)).length>40, 'its name shows on the left');
pads.wait(200);
assert(!strip(3).includes(white), 'the flash is brief');
assert(pads.pixels(1,132,70,9).filter(p=>p!==color(11,7,16)).length>40, 'the name stays up to 400 ms');
pads.wait(300);
const leftIdle=pads.pixels(1,132,70,9);
assert(!leftIdle.includes(white));
pads.input(0,1<<18); // High agogó, right bank
assert(strip(7).includes(white));
assert(pads.pixels(169,132,70,9).filter(p=>p!==color(11,7,16)).length>40, 'right-bank name on the right');
pads.wait(500);
// A hit on the other bank must replace the old flash and name immediately.
const leftName=()=>pads.pixels(0,132,72,9);
const rightName=()=>pads.pixels(168,132,72,9);
const idleLeft=leftName(), idleRight=rightName();
pads.input(0,1<<3);
assert.notDeepEqual(leftName(),idleLeft);
pads.input(0,1<<18);
assert.deepEqual(leftName(),idleLeft,'right hit clears the previous left label');
assert(!strip(3).includes(white) && strip(7).includes(white));
pads.input(0,1<<3);
assert.deepEqual(rightName(),idleRight,'left hit clears the previous right label');
assert(!strip(7).includes(white) && strip(3).includes(white));
pads.wait(500);
assert.deepEqual(leftName(),idleLeft);
// Automatic overlays and playback never light the finger pads.
pads.input(0,1<<8); // conga layer on
pads.input(PLAY);
for(let i=0;i<300;++i) {
  pads.wait(6);
  for(let s=0;s<15;++s) assert(!strip(s).includes(white), 'no flash without a manual hit');
}

// ---------------------------------------------------------------- P5 GLO
const show=bench([
  {title:'CUMBIA',study:'mx',bpm:101},
  {title:'EMPTY SOON',study:'mx',bpm:100},         // hidden by the sparse layout
  {title:'BANQUENA',study:'cumbia-banquena',bpm:94},
  {title:'SALSA 32',study:'salsa',bpm:108,clave:0},
  {title:'SALSA 23',study:'salsa',bpm:108,clave:1},
  {title:'DERECHO',study:'derecho-primera',bpm:158},
],{sparse:true});
show.input(GLO);
const row=(slot,x,w)=>show.pixels(x,ORDER_TOP+slot*ROW,w,ROW);
assert.equal(row(0,0,1)[0],gold, 'NOW is gold');
assert(row(1,17,40).includes(dim) && !row(1,116,124).some(p=>p===cyan||p===lilac||p===gold), 'an empty slot stays empty');
assert(row(2,189,24).includes(cyan), 'cut-time Colombian cumbia reads IN 2');
assert(row(2,116,48).includes(cyan), 'a source-backed body is cyan');
assert(row(0,189,24).every(p=>p===gold), 'no counting note for MX electronic');
assert(row(3,189,24).includes(cyan) && row(4,189,24).includes(cyan));
assert.notDeepEqual(row(3,189,24),row(4,189,24), '3-2 and 2-3 differ');
assert(row(3,116,48).includes(lilac), 'an authored study is lilac');
assert(row(5,116,48).includes(cyan), 'derecho primera is source-backed');
assert(row(15,17,40).length && row(15,0,1)[0]!==gold, 'the moved song keeps its own row');
const tempoBefore=row(0,168,18);
show.turn(2,5); // live tempo
assert.notDeepEqual(row(0,168,18),tempoBefore, 'NOW follows live tempo');
const savedTempo=row(2,168,18);
show.turn(0,2); // SELECT moves the SAVE cursor (1.3: KNOB3 is the language)
assert([1,2,3].some(slot=>row(slot,0,1)[0]===line), 'the cursor row is LINE');
assert.deepEqual(row(2,168,18).filter(p=>p===gold).length>0, true, 'other rows keep saved tempo');
show.input(HOME); show.input(PLAY); show.input(GLO); // cue slot 4 while playing
show.input(0,1<<3);
show.until(()=>row(3,0,1)[0]===pink, 200);
assert.equal(row(3,0,1)[0],pink, 'NEXT is pink');
console.log('P4/P5 pass on the compiled board: Energy gauge, finger-pad feedback, GLO rows.');
