// Compás 1.3 FRENO: REBAJADA turned past 60 while playing brakes the band to
// a stop; the REBAJADA setting comes back for the next PLAY.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {boardWasm} from './lib/bench.mjs';
const module=await WebAssembly.compile(await readFile(boardWasm));
const PLAY=1<<12, FX=1<<2, HOME=1<<8;
const b=new WebAssembly.Instance(module).exports;
assert.equal(b.bench_boot(0),1);b.bench_audio();b.bench_master(100);b.bench_audio();
const enc=new Int32Array(b.memory.buffer,b.bench_encoders(),7);
const input=(buttons,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();b.bench_input(0,0,0);b.bench_audio();};
input(1<<7,1<<10);input(HOME);
const state=()=>{const v=b.bench_speed();return {speed:v&255,playing:!!(v&256)};};
input(FX);                                     // mixer: K6 is REBAJADA
enc[5]=-20;input(0);enc[5]=0;                  // 100 -> 80
assert.equal(state().speed,80);
input(PLAY);for(let i=0;i<100;i++)b.bench_audio();
assert.ok(state().playing);
enc[5]=-40;input(0);enc[5]=0;                  // to 60
assert.equal(state().speed,60);
enc[5]=-5;input(0);enc[5]=0;                   // past 60: FRENO
let blocks=0,last=60,falling=true;
while(state().playing && blocks<2000){b.bench_audio();const s=state().speed;if(state().playing&&s>last)falling=false;last=s;blocks++;}
assert.ok(!state().playing,'FRENO ends in a stop');
assert.ok(falling,'speed only falls while braking');
assert.ok(blocks>250 && blocks<500,`brake takes about a second (${blocks} blocks)`);
assert.equal(state().speed,60,'REBAJADA returns after the stop');
// Turning REBAJADA back up during a brake cancels it and keeps playing.
input(PLAY);for(let i=0;i<50;i++)b.bench_audio();
enc[5]=-5;input(0);enc[5]=0;for(let i=0;i<60;i++)b.bench_audio();
assert.ok(state().speed<60 && state().playing);
enc[5]=10;input(0);enc[5]=0;for(let i=0;i<500;i++)b.bench_audio();
assert.ok(state().playing,'cancelled brake keeps the band playing');
assert.equal(state().speed,60,'cancelling lands on the slowest REBAJADA');
// Stopped: past 60 does nothing.
input(PLAY);for(let i=0;i<5;i++)b.bench_audio();
enc[5]=-30;input(0);enc[5]=0;enc[5]=-5;input(0);enc[5]=0;
assert.ok(!state().playing);assert.equal(state().speed,60);
console.log(`FRENO: stop after ${blocks} blocks (${(blocks*128/44100).toFixed(2)} s) from REBAJADA 60`);
