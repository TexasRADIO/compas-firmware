import {dacScale} from '../web/dac-format.mjs';
// Private 12/8 source-cell rehearsal on actual board WASM; no physical device.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';
const root=new URL('../',import.meta.url),read=async p=>JSON.parse(await readFile(new URL(p,root)));
const studies=(await read('src/score.json')).studies,song=(await read('repertoire/bembe-development-set.json')).songs[0];
const id=studies.findIndex(s=>s.id===song.study);assert.equal(id,37);
const wasm=await readFile(process.argv[2]??new URL('build/board/browser-board.wasm',root));
const module=await WebAssembly.compile(wasm),sha=b=>createHash('sha256').update(b).digest('hex');
const out=resolve(process.argv[3]??'build/bembe-review');await mkdir(out,{recursive:true});
const keys=[7,9,11,12,14,16],names=['intro','verse-a','verse-b','chorus','break','ending'];
const view=b=>{b.bench_read_view();return Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,88));};
function boot(bpm,lang=0){
  const b=new WebAssembly.Instance(module).exports,payload=preparedPayload({format:1,songs:[{...song,bpm}]},studies);
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
  assert.equal(b.bench_boot(1),1);b.bench_lang(lang);b.bench_master(90);b.bench_audio();
  assert.equal(view(b)[1],id);return b;
}
function tap(b,buttons=0,key=0,tick=0){b.bench_input(buttons,key,tick);b.bench_input(0,0,tick);}
function audio(b){const block=new Int32Array(b.memory.buffer,b.bench_audio(),256),pcm=Buffer.alloc(256);let peak=0;
  for(let i=0;i<128;++i){assert.equal(block[i*2],block[i*2+1]);const v=block[i*2]/dacScale(b);
    assert(v>-32768&&v<32767);peak=Math.max(peak,Math.abs(v));pcm.writeInt16LE(Math.trunc(v),i*2);}
  assert.equal(b.bench_rails(),0);assert.equal(b.bench_fx_shed(),0);return {pcm,peak};}
function wav(pcm){const h=Buffer.alloc(44);h.write('RIFF');h.writeUInt32LE(pcm.length+36,4);h.write('WAVEfmt ',8);
  h.writeUInt32LE(16,16);h.writeUInt16LE(1,20);h.writeUInt16LE(1,22);h.writeUInt32LE(44100,24);
  h.writeUInt32LE(88200,28);h.writeUInt16LE(2,32);h.writeUInt16LE(16,34);h.write('data',36);h.writeUInt32LE(pcm.length,40);return Buffer.concat([h,pcm]);}
const renders=[],journeys=[];
for(const bpm of [60,96,180])for(const lang of [0,1])for(let part=0;part<6;++part){
  const b=boot(bpm,lang);tap(b,0,1<<keys[part]);b.bench_audio();assert.equal(view(b)[44],part);tap(b,1<<12);
  const blocks=Math.ceil((8*60*44100/bpm+256)/128),chunks=[],steps=new Set();let peak=0;
  for(let n=0;n<blocks;++n){const r=audio(b);chunks.push(r.pcm);peak=Math.max(peak,r.peak);steps.add(view(b)[5]);}
  assert(peak>0);assert.equal(steps.size,48,'each real 12/8 phrase traverses all 48 steps');
  if(part===0)assert.equal(view(b)[44],1);if(part===5)assert.equal(b.bench_playing(),0);
  const pcm=Buffer.concat(chunks),name=`bembe-${bpm}-${names[part]}-${lang?'es':'en'}`;
  if(bpm===96&&lang===0)await writeFile(join(out,name+'.wav'),wav(pcm));
  renders.push({bpm,language:lang?'es':'en',part:names[part],stepsSeen:steps.size,frames:blocks*128,peak,pcmSha256:sha(pcm)});
}
for(const en of renders.filter(r=>r.language==='en'))assert.equal(en.pcmSha256,
  renders.find(r=>r.bpm===en.bpm&&r.part===en.part&&r.language==='es').pcmSha256);
for(const bpm of [60,96,180]){
  const b=boot(bpm),trace=[],chunks=[],fills=new Set();let previous=-1,stage=0,stopped=false,peak=0;
  tap(b,0,1<<keys[0]);b.bench_audio();tap(b,1<<12);
  for(let n=0;n<25000;++n){const r=audio(b);chunks.push(r.pcm);peak=Math.max(peak,r.peak);const v=view(b),p=v[44],tick=((n+2)*128*544)>>>0;
    fills.add(v[4]);if(p!==previous&&v[0])trace.push({part:names[p],frame:n*128,step:v[5]});
    if(stage===0&&p===1){tap(b,0,1<<keys[2],tick);tap(b,0,1<<keys[3],tick);stage=1;}
    else if(stage===1&&p===3){assert.equal(v[5],0);tap(b,0,1<<keys[4],tick);stage=2;}
    else if(stage===2&&p===4){assert.equal(v[5],0);tap(b,1<<10,1<<keys[5],tick);stage=3;}
    if(!v[0]&&stage===3){stopped=true;break;}previous=p;
  }
  assert(stopped&&fills.has(1)&&fills.has(2),'fill arms and plays into Ending');
  assert.deepEqual(trace.map(t=>t.part),['intro','verse-a','chorus','break','ending']);
  assert(trace.every(t=>t.step===0));
  assert.equal(b.bench_show_export(),0);const payload=Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_set_buffer(),b.bench_set_size()));
  const rhythms=Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),b.bench_rhythm_slots_size()));
  const bytesShow=serializeShow(payload,rhythms),reopened=showPayload(bytesShow);assert.deepEqual(reopened.payload,payload);assert.deepEqual(reopened.rhythms,rhythms);
  if(bpm===96)await writeFile(join(out,'Bembe-12-8-Review.cpshow'),JSON.stringify(bytesShow,null,2)+'\n');
  const bytes=wav(Buffer.concat(chunks)),name=`bembe-conductor-${bpm}.wav`;await writeFile(join(out,name),bytes);
  journeys.push({bpm,trace,peak,frames:chunks.length*128,wavSha256:sha(bytes),newestCueWins:true,fillIntoEnding:true,stopped:true,showRoundTrip:'byte-identical'});
}
const report={status:'pass',wasmSha256:sha(wasm),sourceLedgerSha256:sha(await readFile(new URL('repertoire/bembe-source-ledger.json',root))),
  scope:'36 bilingual part/tempo renders across all 48 steps and three conductor/fill/save journeys; actual board WASM, not PI32 timing or musician approval',physicalWrite:false,renders,journeys};
await writeFile(join(out,'receipt.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({status:report.status,renders:renders.length,journeys:journeys.length,out}));
