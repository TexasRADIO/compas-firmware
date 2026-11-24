import {dacScale} from '../web/dac-format.mjs';
// Private source-cell audition; never writes a device or modifies the score bank.
// node tools/peru-source-audition.mjs BOARD.wasm OUT-DIRECTORY
import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {resolve, join} from 'node:path';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';

const [wasmPath, outPath] = process.argv.slice(2);
assert(wasmPath && outPath, 'usage: BOARD.wasm OUT-DIRECTORY');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const wasm = await readFile(wasmPath), module = await WebAssembly.compile(wasm);
const studies = JSON.parse(await readFile(new URL('../src/score.json', import.meta.url))).studies;
const seed = preparedPayload({format:1, songs:[{
  title:'CELL AUDITION',study:'conjunto',kit:false,feel:0,section:'base'
}]}, studies);
const out = resolve(outPath); await mkdir(out, {recursive:true});
const results = [];
for (const name of ['costa-norte-cell','cumbion-cell','open-hat-cell']) {
  const root = new URL('../repertoire/peru-source-review/', import.meta.url);
  const cellBytes = await readFile(new URL(`${name}.json`, root));
  const cell = JSON.parse(cellBytes);
  const rhythm = await readFile(new URL(`${name}.c7r`, root));
  const b = new WebAssembly.Instance(module).exports;
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),seed.length).set(seed);
  assert.equal(b.bench_boot(1),1); b.bench_master(100); b.bench_audio();
  new Uint8Array(b.memory.buffer,b.bench_rhythm_buffer(),140).set(rhythm);
  assert.equal(b.bench_rhythm_import(),0);
  assert.equal(b.bench_rhythm_export(),0);
  assert.deepEqual(Buffer.from(new Uint8Array(b.memory.buffer,b.bench_rhythm_buffer(),140)),rhythm);
  b.bench_input(1<<12,0,0);
  const frames = Math.ceil(rhythm[4]*60*44100/(rhythm[5]*4));
  const pcm = Buffer.alloc(frames*2);
  let peak = 0, energy = 0;
  for(let offset=0; offset<frames; offset+=128) {
    const block = new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let i=0; i<Math.min(128,frames-offset); ++i) {
      assert.equal(block[i*2],block[i*2+1]);
      const value = block[i*2]/dacScale(b);
      assert(value>-32768 && value<32767,'no PCM rail');
      peak=Math.max(peak,Math.abs(value));energy+=value*value;
      pcm.writeInt16LE(Math.trunc(value),(offset+i)*2);
    }
  }
  assert(peak>0,'audible nonzero output');
  b.bench_input(1<<12,0,0);b.bench_audio();assert.equal(b.bench_playing(),0);
  const header=Buffer.alloc(44);header.write('RIFF');header.writeUInt32LE(36+pcm.length,4);
  header.write('WAVEfmt ',8);header.writeUInt32LE(16,16);header.writeUInt16LE(1,20);
  header.writeUInt16LE(1,22);header.writeUInt32LE(44100,24);header.writeUInt32LE(88200,28);
  header.writeUInt16LE(2,32);header.writeUInt16LE(16,34);header.write('data',36);
  header.writeUInt32LE(pcm.length,40);const wav=Buffer.concat([header,pcm]);
  const receipt={name,sourceCellSha256:hash(cellBytes),c7rSha256:hash(rhythm),
    sourcePdfSha256:cell.source.sha256,wasmSha256:hash(wasm),seedSha256:hash(seed),
    wavSha256:hash(wav),sampleRate:44100,frames,durationSeconds:frames/44100,
    bpm:rhythm[5],steps:rhythm[4],peak,rms:Math.sqrt(energy/frames),
    portableRoundTrip:'byte-identical',physicalWrite:false,
    scope:'Private source-cell integrity/audio render; no musician listening approval or full arrangement claim'};
  await writeFile(join(out,`${name}.wav`),wav);
  await writeFile(join(out,`${name}.json`),JSON.stringify(receipt,null,2)+'\n');
  results.push(receipt);
}
await writeFile(join(out,'receipts.json'),JSON.stringify(results,null,2)+'\n');
console.log(JSON.stringify(results,null,2));
