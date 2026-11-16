// Golden frames: drive the compiled board through every page and key state and
// record a SHA-256 per framebuffer. A behaviour-preserving restructure must
// reproduce this file exactly (`--check`). Optional PNGs for review (`--png DIR`).
import {createHash} from 'node:crypto';
import {writeFileSync,readFileSync,mkdirSync} from 'node:fs';
import {deflateSync} from 'node:zlib';
import assert from 'node:assert/strict';
import {bench,packTrace,BUTTON as B,ENC as E,VIEW as V} from '../tests/lib/bench.mjs';

const groove={title:'CUMBIA',study:'mx',bpm:120};
const salsa={title:'SALSA',study:'salsa',bpm:108,clave:1};
const score={title:'GOLONDRINA',study:'la-golondrina-piano-bolero',bpm:120};
const beat=v=>v[V.position]>>2;
// More BoardView offsets (checked against board.h with offsetof).
const R={result:13,next_song:29,score_ending:35,rack_live:36,rack_sequence:68,speed:72};
const page=s=>s.snapshot()[0];
const quiet=s=>{ s.master(80); s.wait(1100); };
const start=s=>{ s.input(B.PLAY); s.until(v=>v[V.playing]&&beat(v)===1); };
const scenarios={
  'band-ready':        s=>{},
  'band-playing':      s=>{s.input(B.PLAY); s.until(v=>v[V.playing]&&beat(v)===2); s.wait(60);},
  'band-cue-waiting':  s=>{s.input(B.PLAY); s.until(v=>beat(v)===1); s.input(0,1<<12); s.until(v=>v[V.next_part]===3&&beat(v)===3); s.wait(60);},
  'band-cue-landed':   s=>{s.input(B.PLAY); s.until(v=>beat(v)===1); s.input(0,1<<12); s.until(v=>v[V.part]===3); s.wait(500);},
  'band-energy-queued':s=>{s.input(B.PLAY); s.until(v=>beat(v)===1); s.turn(E.ALGORITHM,1); s.until(v=>v[V.pending]===1&&beat(v)===2); s.wait(60);},
  'band-break':        s=>{s.input(B.PLAY); s.until(v=>beat(v)===1); s.turn(E.ALGORITHM,2); s.until(v=>v[V.section]===2); s.wait(600);},
  'band-pad-left':     s=>{s.input(0,1<<3); s.wait(20);},
  'band-pad-right':    s=>{s.input(0,1<<18); s.wait(20);},
  'bass':              s=>{s.turn(E.SELECT,1); s.wait(60);},
  'bass-tone-low':     s=>{s.turn(E.SELECT,1); s.turn(E.K1,-40); s.wait(60);},
  'bass-acid':         s=>{s.turn(E.SELECT,1); s.turn(E.K4,1); s.wait(500);},
  'perc':              s=>{s.turn(E.SELECT,2); s.wait(60);},
  'perc-conga-in':     s=>{s.turn(E.SELECT,2); s.input(0,1<<8); s.wait(300);},
  '727':               s=>{s.input(B.REC); s.wait(60);},
  '727-hit':           s=>{s.input(B.REC); s.input(0,1<<3); s.wait(20);},
  'glo':               s=>{s.input(B.GLO); s.wait(60);},
  'mix':               s=>{s.input(B.FX); s.wait(60);},
};
// R1 states. Each one asserts that it reached its state before the frame is kept.
const extra={
  // Transport and conductor moments on Band.
  'band-countin':      {songs:[{...groove,countIn:true},salsa], run:s=>{s.input(B.PLAY); s.wait(300);
                          assert(s.snapshot()[20+V.playing],'count-in running');}},
  'band-lift-landing': {run:s=>{start(s); s.turn(E.ALGORITHM,1); s.until(v=>v[V.section]===1); s.wait(100);}},
  'band-back-in':      {run:s=>{start(s); s.turn(E.ALGORITHM,2); s.until(v=>v[V.section]===2);
                          s.turn(E.ALGORITHM,-2); s.until(v=>v[V.section]===0); s.wait(100);}},
  'song-queued':       {run:s=>{start(s); s.turn(E.PRESETS,1); s.until(v=>v[R.next_song]===1); s.wait(60);}},
  'song-landing':      {songs:[{...groove,phrases:1},salsa], run:s=>{start(s); s.turn(E.PRESETS,1);
                          s.until(v=>v[V.song]===1); s.wait(200);}},
  'ending-signoff':    {run:s=>{start(s); s.input(0,1<<16); s.until(v=>!v[V.playing],13000); s.wait(200);}},
  'ending-ready':      {run:s=>{start(s); s.input(0,1<<16); s.until(v=>!v[V.playing],13000); s.wait(4000);
                          assert.equal(s.audio()[V.song],1,'ending readies the next song');}},
  // Notices in the band above the footer.
  // Notices run at a working MASTER (80) and wait out the 1 s MASTER readout,
  // because MASTER 000 on Band outranks every lower notice.
  'notice-rebajada':   {run:s=>{quiet(s); s.input(B.FX); s.turn(E.K4,-20); s.input(B.HOME); s.wait(60);
                          const v=s.audio(); assert(v[R.speed]<100,'speed lowered');}},
  'notice-master':     {run:s=>{s.master(60); s.wait(60);}},
  'notice-saved':      {run:s=>{quiet(s); s.input(B.GLO); s.input(B.SAVE); s.wait(60);}},
  'notice-drums-only': {run:s=>{quiet(s); s.input(B.REC); s.input(B.SEQ); s.input(B.FX); s.input(B.FX); s.input(B.REC); s.input(B.HOME); s.wait(300);
                          assert.equal(s.audio()[R.rack_sequence],2,'drums only');}},
  // KNOWN DEFECT (R7): leaving the step editor with HOME keeps rack_edit set,
  // which hides the DRUMS ONLY notice that CONTROL-DESIGN says stays on other pages.
  'drums-only-home-exit':{run:s=>{quiet(s); s.input(B.REC); s.input(B.SEQ); s.input(B.FX); s.input(B.FX); s.input(B.HOME); s.wait(300);
                          assert.equal(s.audio()[R.rack_sequence],2,'drums only'); assert.equal(s.snapshot()[18],1,'edit flag kept');}},
  // Mix and the 727.
  'mix-speed':         {run:s=>{s.input(B.FX); s.turn(E.K4,-20); s.wait(60);}},
  '727-roll':          {run:s=>{start(s); s.input(B.REC); s.input(B.ARP); s.wait(60);}},
  '727-rec-armed':     {run:s=>{start(s); s.input(B.REC); s.input(0,1<<3); s.input(B.REC); s.wait(60);}},
  '727-steps':         {run:s=>{s.input(B.REC); s.input(B.SEQ); s.input(0,1<<0); s.input(0,1<<4); s.input(0,1<<10); s.wait(60);
                          assert.equal(s.snapshot()[18],1,'step editor open');}},
  '727-edit':          {run:s=>{s.input(B.REC); s.input(0,1<<5); s.input(B.EDIT); s.wait(60);}},
};
const extraScore={
  'score-coda-queued': {run:s=>{start(s); s.input(B.SEQ); s.wait(60);}},
  'score-countin':     {run:s=>{s.input(B.PLAY); s.wait(150);}},
};
const sets={groove:[groove,salsa,score],score:[score,groove]};
const results={}, traces=[];
const argv=process.argv.slice(2);
const pngDir=argv.includes('--png')?argv[argv.indexOf('--png')+1]:null;
// --lang es: the same states with the screen in Spanish (tests/golden-frames-es.json).
const lang=argv.includes('--lang')?{es:1}[argv[argv.indexOf('--lang')+1]]:0;
if(argv.includes('--lang') && !lang) throw new Error('--lang takes es');
if(pngDir) mkdirSync(pngDir,{recursive:true});
for(const [setName,songs] of Object.entries(sets))
  for(const [name,run] of Object.entries(scenarios)) {
    // Scores have no Energy or part-cue grooves; they keep the page and transport states.
    if(setName==='score' && !['band-ready','band-playing','bass','perc','727','glo','mix'].includes(name)) continue;
    const trace=[]; const s=bench(songs,{live:true,trace,lang}); run(s);
    const frame=s.frame();
    const key=`${setName}/${name}`;
    traces.push(`${key} ${packTrace(trace)}`);
    results[key]=createHash('sha256').update(Buffer.from(frame.buffer)).digest('hex');
    if(pngDir) writeFileSync(`${pngDir}/${setName}-${name}.png`,png(frame));
  }
for(const [setName,table] of [['groove',extra],['score',extraScore]])
  for(const [name,{songs,run}] of Object.entries(table)) {
    const trace=[]; const s=bench(songs??sets[setName],{live:true,trace,lang}); run(s);
    const frame=s.frame(), key=`${setName}/${name}`;
    traces.push(`${key} ${packTrace(trace)}`);
    results[key]=createHash('sha256').update(Buffer.from(frame.buffer)).digest('hex');
    if(pngDir) writeFileSync(`${pngDir}/${setName}-${name}.png`,png(frame));
  }
const out=new URL(lang?'../tests/golden-frames-es.json':'../tests/golden-frames.json',import.meta.url);
// The native replay input (generated; tools/golden-host.mjs consumes it). English only.
mkdirSync(new URL('../build/',import.meta.url),{recursive:true});
if(!lang) writeFileSync(new URL('../build/golden-trace.txt',import.meta.url),traces.join('\n')+'\n');
if(argv.includes('--check')) {
  const want=JSON.parse(readFileSync(out));
  const diff=Object.keys({...want,...results}).filter(k=>want[k]!==results[k]);
  if(diff.length) { console.error('Golden frames differ:',diff.join(', ')); process.exit(1); }
  console.log(`Golden frames identical: ${Object.keys(results).length} states.`);
} else {
  writeFileSync(out,JSON.stringify(results,null,1)+'\n');
  console.log(`Recorded ${Object.keys(results).length} golden frames.`);
}

function png(frame) { // RGB565 -> 8-bit RGB PNG, no dependencies
  const raw=Buffer.alloc(240*(1+240*3));
  for(let y=0;y<240;++y) { raw[y*721]=0; for(let x=0;x<240;++x) {
    const p=frame[y*240+x], o=y*721+1+x*3;
    raw[o]=(p>>11)<<3|(p>>13); raw[o+1]=((p>>5)&63)<<2|((p>>9)&3); raw[o+2]=(p&31)<<3|((p>>2)&7); } }
  const chunk=(type,data)=>{ const len=Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td=Buffer.concat([Buffer.from(type),data]); const crc=Buffer.alloc(4); crc.writeUInt32BE(crc32(td)>>>0);
    return Buffer.concat([len,td,crc]); };
  const ihdr=Buffer.alloc(13); ihdr.writeUInt32BE(240,0); ihdr.writeUInt32BE(240,4); ihdr[8]=8; ihdr[9]=2;
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',deflateSync(raw)),chunk('IEND',Buffer.alloc(0))]);
}
function crc32(buf){ let c=~0; for(const byte of buf){ c^=byte; for(let k=0;k<8;++k) c=(c>>>1)^(0xedb88320&-(c&1)); } return ~c; }
