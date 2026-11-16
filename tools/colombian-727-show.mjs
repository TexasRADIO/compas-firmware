// Assemble a playable show through the same panel and exporter used live.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';

const read=path=>JSON.parse(readFileSync(new URL(path,import.meta.url)));
const {studies}=read('../src/score.json');
const set=read('../repertoire/colombian-cumbia-audition-set.json');
const {patterns,rack727}=read('../repertoire/colombian-cumbia-cells.json');
const origins=[19,20,21];
const buttons={set:7,home:8,save:9,seq:11,rec:13,fx:2};
const wasm=new WebAssembly.Module(readFileSync(new URL('../build/board/browser-board.wasm',import.meta.url)));

function boot(setBytes,rhythmBytes) {
  const board=new WebAssembly.Instance(wasm).exports;
  new Uint8Array(board.memory.buffer,board.bench_set_buffer(),board.bench_set_size()).set(setBytes);
  if(rhythmBytes) new Uint8Array(board.memory.buffer,board.bench_rhythm_slots_buffer(),board.bench_rhythm_slots_size()).set(rhythmBytes);
  assert.equal(board.bench_boot(rhythmBytes?3:1),1);
  board.bench_audio();
  return board;
}
function press(board,button,keys=0) {
  board.bench_input(1<<buttons[button],keys,0);
  board.bench_audio();
  if(keys) {
    board.bench_input(0,0,0);
    board.bench_audio();
  }
}
function recall(board,slot) {
  press(board,'set',1<<slot);
  press(board,'home');
}
function turnPresets(board,detents) {
  new Int32Array(board.memory.buffer,board.bench_encoders(),7)[6]=detents;
  board.bench_input(0,0,0);
  board.bench_audio();
}
function verify(board,slot) {
  board.bench_read_view();
  const state=new Uint8Array(board.memory.buffer,board.bench_snapshot()+20,76);
  const song=set.songs[slot];
  assert.equal(state[28],slot);
  assert.equal(studies[state[1]].id,song.study);
  assert.equal(state[6],song.bpm);
  assert.equal(state[7],0);
  assert.equal(state[68],2); // Drums Only.
  assert.equal(state[70],origins[slot]);
}

assert.deepEqual(set.songs.map(s=>s.study),patterns.map(p=>p.study));
set.songs.forEach((song,i)=>{song.title=rack727.titles[i];});
const board=boot(preparedPayload(set,studies));
for(const [slot,origin] of origins.entries()) {
  recall(board,slot);
  press(board,'rec');
  press(board,'seq');
  turnPresets(board,1); // USER to the first source preset.
  turnPresets(board,origin);
  press(board,'fx'); // Layer to Drums Only.
  verify(board,slot);
  press(board,'set');
  press(board,'save');
}
recall(board,0);
assert.equal(board.bench_show_export(),0);
const payload=new Uint8Array(board.memory.buffer,board.bench_set_buffer(),board.bench_set_size());
const rhythms=new Uint8Array(board.memory.buffer,board.bench_rhythm_slots_buffer(),board.bench_rhythm_slots_size());
const show=serializeShow(payload,rhythms);
const decoded=showPayload(show);
const reopened=boot(decoded.payload,decoded.rhythms);
for(const slot of origins.keys()) {
  recall(reopened,slot);
  verify(reopened,slot);
}
process.stdout.write(JSON.stringify(show)+'\n');
