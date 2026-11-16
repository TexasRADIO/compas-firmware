// Export the regional prepared set through the native show owner for rehearsal.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow} from '../web/show-format.mjs';
const output=process.argv[2];assert(output);
const studies=JSON.parse(await readFile(new URL('../src/score.json',import.meta.url),'utf8')).studies;
const set=JSON.parse(await readFile(new URL('../repertoire/regional-audition-set.json',import.meta.url),'utf8'));
const {instance:{exports:board}}=await WebAssembly.instantiate(await readFile(new URL('../build/board/browser-board.wasm',import.meta.url)));
const payload=preparedPayload(set,studies);
new Uint8Array(board.memory.buffer,board.bench_set_buffer(),payload.length).set(payload);
assert.equal(board.bench_boot(1),1);board.bench_audio();assert.equal(board.bench_show_export(),0);
const songs=new Uint8Array(board.memory.buffer,board.bench_set_buffer(),board.bench_set_size()).slice();
const rhythms=new Uint8Array(board.memory.buffer,board.bench_rhythm_slots_buffer(),board.bench_rhythm_slots_size()).slice();
await writeFile(output,JSON.stringify(serializeShow(songs,rhythms)),{flag:'wx'});
