// Convert a browser show to a NEW emulator-only NOR fixture. No device access.
import {readFile,writeFile,stat} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {showPayload} from '../web/show-format.mjs';

const [input,output,validator,validatorSHA]=process.argv.slice(2);
if(!input || !output || ![4,6].includes(process.argv.length))
  throw Error('Usage: node tools/board-show-flash.mjs show.cpshow new-virtual.bin [validator.wasm sha256]');
if((await stat(input)).size>65536) throw Error('Show file is too large.');
const {payload,rhythms}=showPayload(JSON.parse(await readFile(input,'utf8')));
const wasm=await readFile(validator ?? fileURLToPath(new URL('../build/board/browser-board.wasm',import.meta.url)));
if(validator !== undefined && createHash('sha256').update(wasm).digest('hex')!==validatorSHA)
  throw Error('Show validator differs from the paired browser build.');
const {instance:{exports:board}}=await WebAssembly.instantiate(wasm);
if(payload.length!==board.bench_set_size() || rhythms.length!==board.bench_rhythm_slots_size())
  throw Error('Build the matching browser board before converting this show.');
new Uint8Array(board.memory.buffer,board.bench_set_buffer(),payload.length).set(payload);
new Uint8Array(board.memory.buffer,board.bench_rhythm_slots_buffer(),rhythms.length).set(rhythms);
if(!board.bench_boot(3)) throw Error('Native validation rejected this show.');

const flash=new Uint8Array(0x100000).fill(255),base=0x90000;
flash.set(payload,base+256);flash.set(rhythms,base+256+payload.length);
flash.set(new TextEncoder().encode('CPS2'),base);
const view=new DataView(flash.buffer);view.setUint32(base+4,1,true);
let crc=0xffffffff;
for(const byte of [...flash.subarray(base+4,base+8),...payload,...rhythms]) {
  crc^=byte;
  for(let bit=0;bit<8;++bit)crc=(crc>>>1)^(crc&1?0xedb88320:0);
}
view.setUint32(base+8,(crc^0xffffffff)>>>0,true);
flash.set(new TextEncoder().encode('CMPASIM1'),0x92000);
await writeFile(output,flash,{flag:'wx'});
console.log(JSON.stringify({virtualOnly:true,showBytes:payload.length+rhythms.length,output}));
