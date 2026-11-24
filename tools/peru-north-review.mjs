import {dacScale} from '../web/dac-format.mjs';
// Real-bank development rehearsal. No device transport or package creation.
// node tools/peru-north-review.mjs [BOARD.wasm] [OUTPUT-DIRECTORY]
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {preparedPayload} from '../web/set-payload.mjs';
import {serializeShow,showPayload} from '../web/show-format.mjs';

const root=new URL('../',import.meta.url), read=async p=>JSON.parse(await readFile(new URL(p,root)));
const studies=(await read('src/score.json')).studies;
const set=await read('repertoire/peru-north-development-set.json');
const wasm=await readFile(process.argv[2]??new URL('build/board/browser-board.wasm',root));
const module=await WebAssembly.compile(wasm),sha=b=>createHash('sha256').update(b).digest('hex');
const out=resolve(process.argv[3]??fileURLToPath(new URL('build/peru-north-review',root)));
await mkdir(out,{recursive:true});
const view=b=>{b.bench_read_view();return Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,80));};
const keys=[7,9,11,12,14,16],partNames=['intro','verso','coro','puente','break','final'];
function boot(songs,lang=0) {
  const b=new WebAssembly.Instance(module).exports;
  const payload=preparedPayload({format:1,songs},studies);
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
  assert.equal(b.bench_boot(1),1);b.bench_lang(lang);b.bench_master(90);b.bench_audio();
  return b;
}
function input(b,buttons=0,key=0) {
  b.bench_input(buttons,key,0);b.bench_audio();b.bench_input(0,0,0);b.bench_audio();return view(b);
}
function render(b,frames) {
  const pcm=Buffer.alloc(frames*2);let peak=0;
  for(let offset=0;offset<frames;offset+=128) {
    const block=new Int32Array(b.memory.buffer,b.bench_audio(),256);
    for(let i=0;i<Math.min(128,frames-offset);++i) {
      assert.equal(block[i*2],block[i*2+1]);const value=block[i*2]/dacScale(b);
      assert(value>-32768 && value<32767);peak=Math.max(peak,Math.abs(value));
      pcm.writeInt16LE(Math.trunc(value),(offset+i)*2);
    }
  }
  assert(peak>0);assert.equal(b.bench_rails(),0);assert.equal(b.bench_fx_shed(),0);
  return {pcm,peak,sha256:sha(pcm)};
}
function wav(pcm) {
  const h=Buffer.alloc(44);h.write('RIFF');h.writeUInt32LE(pcm.length+36,4);
  h.write('WAVEfmt ',8);h.writeUInt32LE(16,16);h.writeUInt16LE(1,20);h.writeUInt16LE(1,22);
  h.writeUInt32LE(44100,24);h.writeUInt32LE(88200,28);h.writeUInt16LE(2,32);
  h.writeUInt16LE(16,34);h.write('data',36);h.writeUInt32LE(pcm.length,40);return Buffer.concat([h,pcm]);
}
const receipts=[];
for(const song of set.songs) {
  const frames=Math.ceil(16*60*44100/(song.bpm??studies.find(s=>s.id===song.study).bpm)/4);
  const hashes=new Map();
  for(const lang of [0,1])for(const kit of [false,true])for(let part=0;part<6;++part) {
    const b=boot([{...song,kit}],lang);input(b,0,1<<keys[part]);
    assert.equal(view(b)[44],part);input(b,1<<12);
    const result=render(b,frames+256),name=`${song.study}-${partNames[part]}-${kit?'727':'synth'}-${lang?'es':'en'}`;
    hashes.set(`${kit}-${part}`,result.sha256);
    const bytes=wav(result.pcm);
    if(lang===0 && kit)await writeFile(join(out,name+'.wav'),bytes);
    receipts.push({name,study:song.study,part:partNames[part],kit,language:lang?'es':'en',frames:frames+256,
      peak:result.peak,pcmSha256:result.sha256,wavSha256:sha(bytes),missedDeadlineClaim:false});
    if(part===0)assert.equal(view(b)[44],1,'Intro opens Verso');
    if(part===5)assert.equal(b.bench_playing(),0,'Final stops');
    else input(b,1<<12);
    assert.equal(b.bench_playing(),0);
  }
  for(const kit of [false,true])assert(new Set(partNames.map((_,i)=>hashes.get(`${kit}-${i}`))).size>=4,'audibly distinct live textures');
  for(const section of ['base','lift','break']) {
    const b=boot([{...song,section}]);input(b,1<<12);const result=render(b,frames);
    receipts.push({study:song.study,energy:section,peak:result.peak,pcmSha256:result.sha256});input(b,1<<12);
  }
  // A rapid newer part supersedes the waiting one; it cannot land mid-phrase.
  const b=boot([song]);input(b,0,1<<keys[1]);input(b,1<<12);render(b,2048);
  input(b,0,1<<keys[2]);assert.equal(view(b)[45],2);
  input(b,0,1<<keys[3]);assert.equal(view(b)[45],3);assert.equal(view(b)[44],1);
  let landed=false;
  for(let n=0;n<2000;++n){b.bench_audio();const v=view(b);if(v[44]===3){assert.equal(v[5],0);landed=true;break;}}
  assert(landed,'Puente lands at phrase');input(b,1<<12);
  assert.equal(b.bench_show_export(),0);
  const payload=Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_set_buffer(),b.bench_set_size()));
  const rhythms=Uint8Array.from(new Uint8Array(b.memory.buffer,b.bench_rhythm_slots_buffer(),b.bench_rhythm_slots_size()));
  const file=serializeShow(payload,rhythms),reopened=showPayload(file);
  assert.deepEqual(reopened.payload,payload);assert.deepEqual(reopened.rhythms,rhythms);
  receipts.push({study:song.study,newestCueWins:true,landingStep:0,showRoundTrip:'byte-identical'});
}
for(const en of receipts.filter(r=>r.language==='en')) {
  const es=receipts.find(r=>r.study===en.study&&r.part===en.part&&r.kit===en.kit&&r.language==='es');
  assert.equal(en.pcmSha256,es.pcmSha256,'language cannot change audio');
}
const demonstrations=[];
for(const song of set.songs) {
  const b=boot([song]),chunks=[],trace=[],fills=new Set();
  let lastPart=-1,lastPosition=-1,stage=0,peak=0,blocks=0,stopped=false;
  const tap=(buttons,key,tick)=>{b.bench_input(buttons,key,tick);b.bench_input(0,0,tick);};
  tap(0,1<<keys[0],0);b.bench_audio();tap(1<<12,0,128*544);
  for(;blocks<30000;++blocks) {
    const source=new Int32Array(b.memory.buffer,b.bench_audio(),256),pcm=Buffer.alloc(256);
    for(let i=0;i<128;++i){assert.equal(source[i*2],source[i*2+1]);const value=source[i*2]/dacScale(b);
      peak=Math.max(peak,Math.abs(value));assert(value>-32768&&value<32767);pcm.writeInt16LE(Math.trunc(value),i*2);}
    chunks.push(pcm);
    const v=view(b),part=v[44],position=v[5],tick=((blocks+2)*128*544)>>>0;
    fills.add(v[4]);
    if(part!==lastPart&&v[0])trace.push({part:partNames[part],frame:blocks*128});
    const wrap=part===lastPart&&position<lastPosition;
    if(stage===0&&part===1)stage=1;
    else if(stage===1&&part===1&&wrap){tap(0,1<<keys[2],tick);stage=2;}
    else if(stage===2&&part===2&&wrap){tap(0,1<<keys[3],tick);stage=3;}
    else if(stage===3&&part===3&&wrap){tap(0,1<<keys[4],tick);stage=4;}
    else if(stage===4&&part===4&&part!==lastPart){tap(0,1<<keys[3],tick);stage=5;}
    else if(stage===5&&part===3&&part!==lastPart){tap(1<<10,1<<keys[5],tick);stage=6;}
    lastPart=part;lastPosition=position;
    if(!v[0]&&stage===6){stopped=true;break;}
  }
  assert(stopped&&fills.has(1)&&fills.has(2),'fill arms, plays and lands into Final before stopping');
  assert.deepEqual(trace.map(t=>t.part),['intro','verso','coro','puente','break','puente','final']);
  assert.equal(b.bench_rails(),0);assert.equal(b.bench_fx_shed(),0);
  const bytes=wav(Buffer.concat(chunks)),filename=song.study+'-conductor-demo.wav';
  await writeFile(join(out,filename),bytes);
  demonstrations.push({study:song.study,filename,wavSha256:sha(bytes),frames:chunks.length*128,
    durationSeconds:chunks.length*128/44100,peak,trace,fillArmedAndPlayed:true,stopped:true,readyPart:partNames[view(b)[44]],
    scope:'Scripted Compás conductor performance, not the source recording or its full song form'});
}
const report={status:'pass',wasmSha256:sha(wasm),sourceLedgerSha256:sha(await readFile(new URL('repertoire/peru-north-development-ledger.json',root))),
  physicalWrite:false,scope:'48 part/language/palette auditions, six Energy auditions, two cue/show journeys and two complete conductor/fill performances; host functional evidence, not target timing or musician approval',receipts,demonstrations};
await writeFile(join(out,'receipt.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({status:report.status,runs:receipts.length,out,wasmSha256:report.wasmSha256}));
