// Shared compiled-board bench for framebuffer tests and golden frames.
// One copy replaces the bench() helpers duplicated across tests/*.mjs.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {preparedPayload} from '../../web/set-payload.mjs';

const root=new URL('../../',import.meta.url);
export const read=path=>readFileSync(new URL(path,root));
// The browser catalog, built as tools/set_catalog.py builds it: each study carries
// its conductor form's part names, and each score its piano slot and guide flag.
const ledger=JSON.parse(read('src/score.json'));
export const catalog=[
  ...ledger.studies.filter(s=>!s.testOnly).map(s=>({...s,conductorParts:ledger.conductorForms[s.conductor??'default']})),
  ...JSON.parse(read('repertoire/board-scores.json')).map((score,i)=>({...score,piano:i+1,guide:!!score.guide}))];
let compiled;
// COMPAS_BOARD_WASM selects the compiled board (relative to the repo root). The
// default is the real-bank build; tools/standin-check.sh points it at build/standin.
export const boardWasm=new URL(process.env.COMPAS_BOARD_WASM??'build/board/browser-board.wasm',root);
const module=()=>compiled??=new WebAssembly.Module(readFileSync(boardWasm));

export const color=(r,g,b)=>(r>>3)<<11|(g>>2)<<5|(b>>3);
export const BUTTON={OCT_DOWN:1<<0,OCT_UP:1<<1,FX:1<<2,SCL:1<<3,ENV:1<<4,LFO:1<<5,EDIT:1<<6,
  GLO:1<<7,HOME:1<<8,SAVE:1<<9,ARP:1<<10,SEQ:1<<11,PLAY:1<<12,REC:1<<13};
export const ENC={SELECT:0,ALGORITHM:1,K1:2,K2:3,K3:4,K4:5,PRESETS:6};
// BoardView byte offsets used by the tests (80-byte view; the last word is
// compound step bits 33–48, so all historic offsets remain unchanged).
export const VIEW={playing:0,study:1,section:2,pending:3,fill:4,position:5,song:28,part:44,next_part:45};

// trace: an optional array that receives every state-changing bench call, so
// tests/stage-host.c can replay the same session natively (tools/golden-host.mjs).
export function bench(songs,{sparse=false,live=false,trace=null,lang=0}={}) {
  const raw=new WebAssembly.Instance(module()).exports;
  const log=trace?(token=>trace.push(token)):()=>{};
  const b={...raw,
    bench_audio:()=>{ log('A'); return raw.bench_audio(); },
    bench_read_view:()=>{ log('R'); return raw.bench_read_view(); },
    bench_draw:t=>{ log('D'+t); return raw.bench_draw(t); },
    bench_input:(buttons,keys,t)=>{ log(`I${buttons},${keys},${t}`); return raw.bench_input(buttons,keys,t); },
    bench_master:l=>{ log('M'+l); return raw.bench_master(l); },
    bench_boot:p=>{ log('P'+Buffer.from(new Uint8Array(raw.memory.buffer,raw.bench_set_buffer(),raw.bench_set_size())).toString('hex'));
      log('S'+p); return raw.bench_boot(p); }};
  const payload=preparedPayload({format:1,songs},catalog);
  if(sparse) { // hide slot 1 and move it to slot 15
    const slot=b.bench_set_size()/16;
    payload.copyWithin(15*slot,slot,2*slot);
    payload.fill(0,slot,2*slot);
    payload[slot]=127;
  }
  new Uint8Array(b.memory.buffer,b.bench_set_buffer(),payload.length).set(payload);
  assert.equal(b.bench_boot(1),1);
  if(lang) b.bench_lang(lang); // Spanish screens (golden-frames --lang es)
  let tick=0;
  // live: draw like the device does (every ~50 ms and after each input), so the
  // stage sees state changes between frames, as on the FM-1.
  let lastDraw=0;
  const audio=()=>{ tick+=128*544; b.bench_audio(); b.bench_read_view();
    if(live && tick-lastDraw>=1200000) { b.bench_draw(tick); lastDraw=tick; }
    return new Uint8Array(b.memory.buffer,b.bench_snapshot()+20,80); };
  const input=(buttons,keys=0)=>{ b.bench_input(buttons,keys,tick); audio(); b.bench_input(0,0,tick);
    const v=audio(); if(live) { b.bench_draw(tick); lastDraw=tick; } return v; };
  const turn=(index,delta)=>{ log(`E${index},${delta}`); new Int32Array(b.memory.buffer,b.bench_encoders(),7)[index]=delta; return input(0); };
  const until=(predicate,limit=6500)=>{ for(let i=0;i<limit;++i){ const v=audio(); if(predicate(v)) return v; }
    assert.fail('Board did not reach the expected state'); };
  const wait=ms=>{ let v; for(let i=0;i<Math.ceil(ms/2.9);++i) v=audio(); return v; };
  const frame=()=>{ const f=Uint16Array.from(new Uint16Array(b.memory.buffer,b.bench_draw(tick),240*240)); log('F'); return f; };
  const pixels=(x,y,width,height)=>{ const f=new Uint16Array(b.memory.buffer,b.bench_draw(tick),240*240);
    return Array.from({length:height},(_,i)=>Array.from(f.subarray((y+i)*240+x,(y+i)*240+x+width))).flat(); };
  const rack=()=>new DataView(b.memory.buffer,b.bench_snapshot()+b.bench_snapshot_size()-8,8);
  audio(); if(live) { b.bench_draw(tick); lastDraw=tick; }
  const master=level=>{ b.bench_master(level); return audio(); };
  const snapshot=()=>{ b.bench_read_view(); return new Uint8Array(b.memory.buffer,b.bench_snapshot(),b.bench_snapshot_size()).slice(); };
  return {input,turn,until,wait,frame,pixels,rack,audio,master,snapshot};
}

// Compact a trace: runs of the same token become `token*count`.
export function packTrace(trace) {
  const out=[];
  for(let i=0;i<trace.length;) { let j=i; while(j<trace.length && trace[j]===trace[i]) ++j;
    out.push(j-i>1?`${trace[i]}*${j-i}`:trace[i]); i=j; }
  return out.join(' ');
}
