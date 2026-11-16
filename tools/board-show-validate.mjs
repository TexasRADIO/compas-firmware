// Validate a saved show against explicit, hash-pinned WASM bytes. No device I/O.
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {showPayload} from '../web/show-format.mjs';

const args=JSON.parse(readFileSync(0,'utf8'));
const bytes=readFileSync(args.wasm);
if(createHash('sha256').update(bytes).digest('hex')!==args.wasmSHA256)
  throw Error('Show validator WASM differs from the review manifest');
const {payload,rhythms}=showPayload(args.show);
const {instance:{exports:board}}=await WebAssembly.instantiate(bytes);
if(payload.length!==board.bench_set_size() || rhythms.length!==board.bench_rhythm_slots_size())
  throw Error('Show size differs from the native owner');
new Uint8Array(board.memory.buffer,board.bench_set_buffer(),payload.length).set(payload);
new Uint8Array(board.memory.buffer,board.bench_rhythm_slots_buffer(),rhythms.length).set(rhythms);
if(!board.bench_boot(3))throw Error('Native validation rejected the show');
process.stdout.write(JSON.stringify({payload:Buffer.concat([payload,rhythms]).toString('hex')}));
