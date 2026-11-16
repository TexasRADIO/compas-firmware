// Visual audit 2 on the compiled board: contrast tokens, the 2x cue countdown,
// visible upcoming cones, the burst holding still while a cue waits, the
// cartel sign card, and the colour rule "pink text only when something waits".
import assert from 'node:assert/strict';
import {bench,color} from './lib/bench.mjs';

const pink=color(255,46,136), dim2=color(138,122,156), ring=color(75,60,95);
const paper=color(243,230,200), inkblue=color(29,42,92), orange=color(255,122,26);
const PLAY=1<<12;

const beatOf=v=>v[5]>>2;
const groove={title:'CUMBIA',study:'mx',bpm:120};

// The colour rule: when nothing waits, the cue line, horn, Energy gauge and
// part keys hold no pink text. (Sign extrusion and knob 2's ink are ornament.)
function noPinkWhenIdle(board,label) {
  for(const [x,y,w,h,where] of [[5,94,132,9,'cue line'],[189,107,50,22,'horn'],
                                [118,72,68,22,'energy'],[32,143,174,31,'part keys']])
    assert(!board.pixels(x,y,w,h).includes(pink), `${label}: pink in ${where} with nothing queued`);
}

const band=bench([groove]);
// Stopped: inactive tabs and horn label use DIM2; stopped cones show RING outlines.
assert(band.pixels(77,10,30,9).includes(dim2), 'inactive tab text is DIM2');
assert(band.pixels(189,104,50,25).includes(dim2), 'idle horn label is DIM2');
assert(band.pixels(5,107,181,21).includes(ring), 'stopped cones keep a visible ring');
noPinkWhenIdle(band,'stopped');

// Cartel sign: paper card, ink-blue headliner, mixed-ink edge band (yellow left, blue right).
const sign=band.pixels(0,27,240,28);
assert(sign.includes(paper) && sign.includes(inkblue), 'paper card with ink-blue headliner');
const band24=band.pixels(0,24,240,1);
const r=c=>c>>11, b5=c=>c&31;
assert(r(band24[0])>r(band24[239]) && b5(band24[239])>b5(band24[0]), 'ink runs yellow to blue');

// Playing, nothing queued: upcoming cones are RING, still no pink text.
band.input(PLAY);
band.until(v=>v[0] && beatOf(v)<4);
band.wait(30);
assert(band.pixels(98,107,88,21).includes(ring), 'upcoming cones are visible');
noPinkWhenIdle(band,'playing');

// Burst sticker steps with the beat when idle...
const sticker=()=>band.pixels(200,65,27,29);
band.until(v=>beatOf(v)===1); band.wait(30); const s1=sticker();
band.until(v=>beatOf(v)===2); band.wait(30); const s2=sticker();
assert.notDeepEqual(s1,s2,'the burst marker steps on the beat');
// ...and holds still while a cue waits; the countdown is a 2x pink digit.
band.input(0,1<<12); // Chorus
band.until(v=>v[45]===3 && beatOf(v)<6); band.wait(30);
const held=sticker();
assert(!held.includes(orange), 'no step marker while a cue waits');
band.until(v=>v[45]===3 && beatOf(v)===6); band.wait(30);
assert.deepEqual(sticker(),held,'the burst stays still while a cue waits');
const digit=band.pixels(193,108,11,14).filter(p=>p===pink).length;
assert(digit>20, `2x countdown digit (${digit} pink px)`);
assert(band.pixels(204,113,32,9).includes(pink), 'BEATS unit beside the digit');
// After landing, the rule holds again.
band.until(v=>v[45]!==3 && v[44]===3); band.wait(400);
noPinkWhenIdle(band,'after landing');
// 727 header: the name's bottom rows survive the subtitle line (was "HT RANGA").
const rack=bench([groove]);
rack.input(1<<13); // REC opens the 727 page
const plum=color(23,16,31);
const nameBottom=rack.pixels(29,40,120,2).filter(p=>p!==plum).length;
assert(nameBottom>10, `727 name bottom rows intact (${nameBottom} px)`);
console.log('VISUAL-2 pass on the compiled board: DIM2/RING, 2x countdown, still burst, cartel sign, no idle pink text.');
