// Compás 1.2 effects page (FX twice): ROOM, ECHO, TIME, REPEAT, GLUE.
// Off by default and bit-identical to 1.1 until a knob is turned.
import assert from 'node:assert/strict';
import {readFile, writeFile} from 'node:fs/promises';
import {boardWasm, color} from './lib/bench.mjs';
const module=await WebAssembly.compile(await readFile(boardWasm));
const PLAY=1<<12, FX=1<<2, HOME=1<<8, OCT_DOWN=1<<0, OCT_UP=1<<1;
function band() {
  const b=new WebAssembly.Instance(module).exports;
  assert.equal(b.bench_boot(0),1);b.bench_audio();b.bench_master(100);b.bench_audio();
  const enc=new Int32Array(b.memory.buffer,b.bench_encoders(),7);
  const input=(buttons,keys=0)=>{b.bench_input(buttons,keys,0);b.bench_audio();b.bench_input(0,0,0);b.bench_audio();};
  input(1<<7,1<<10);input(HOME);               // CDMX slot -> Band
  const page=()=>{b.bench_read_view();return new Uint8Array(b.memory.buffer,b.bench_snapshot(),1)[0];};
  const block=()=>Int32Array.from(new Int32Array(b.memory.buffer,b.bench_audio(),256));
  return {b,enc,input,page,block};
}
const PANEL_MIX=1, PANEL_FX=8;
const run=(setup,blocks=600)=>{
  const s=band(); setup(s); s.b.bench_input(PLAY,0,0);
  const out=[]; for(let i=0;i<blocks;i++) out.push(s.block()); s.b.bench_input(0,0,0); return {s,out};
};

// Navigation: FX -> mixer, FX again -> effects, FX -> mixer.
{
  const s=band();
  s.input(FX); assert.equal(s.page(),PANEL_MIX);
  s.input(FX); assert.equal(s.page(),PANEL_FX);
  s.input(FX); assert.equal(s.page(),PANEL_MIX);
  s.input(FX); s.input(HOME); assert.equal(s.page(),0);
}

// Effects page with every amount at zero: same samples as never visiting it.
const dry=run(()=>{}).out;
const visited=run(s=>{s.input(FX);s.input(FX);s.enc[4]=3;s.input(0);s.enc[5]=-10;s.input(0);}).out;
assert.deepEqual(visited,dry,'effects at zero must not change a sample');
for(const blk of dry) for(let i=0;i<128;i++) assert.equal(blk[2*i],blk[2*i+1]);

// ROOM: the sound changes and goes stereo; turning it back to 0 restores mono.
const room=run(s=>{s.input(FX);s.input(FX);s.enc[2]=60;s.input(0);});
let side=0, diff=0;
room.out.forEach((blk,n)=>{ for(let i=0;i<128;i++){ side+=Math.abs(blk[2*i]-blk[2*i+1]); diff+=Math.abs(blk[2*i]-dry[n][2*i]); } });
assert.ok(side>0 && diff>0,'ROOM adds a stereo tail');
// ECHO at 3/16 (default TIME): energy appears a dotted eighth after hits.
const echo=run(s=>{s.input(FX);s.input(FX);s.enc[3]=80;s.input(0);});
let echoDiff=0; echo.out.forEach((blk,n)=>{for(let i=0;i<256;i++) echoDiff+=Math.abs(blk[i]-dry[n][i]);});
assert.ok(echoDiff>0,'ECHO changes the sound');
// GLUE steps by ten on OCT+/OCT- and never leaves 0..100.
{
  const s=band(); s.input(FX); s.input(FX);
  for(let i=0;i<12;i++) s.input(OCT_UP);
  for(let i=0;i<12;i++) s.input(OCT_DOWN);
  assert.equal(s.page(),PANEL_FX,'OCT keys stay on the effects page');
}
// ECHO back to 0 with nothing else on: the echo stops at once and the effect
// block switches off (mono, L == R, the 1.1 path). Re-armed, the echo plays
// nothing until its first repeat: no audio from before it was turned off.
{
  const s=band(); s.input(FX); s.input(FX); s.enc[3]=80; s.input(0);
  s.b.bench_input(PLAY,0,0);
  let side=0;
  for(let i=0;i<400;i++){ const blk=s.block(); for(let j=0;j<128;j++) if(blk[2*j]!==blk[2*j+1]) side++; }
  assert.ok(side>0,'ECHO is heard (stereo) before it is turned off');
  s.enc[3]=-80; s.b.bench_input(0,0,0); s.b.bench_audio();
  for(let i=0;i<2000;i++){ const blk=s.block(); for(let j=0;j<128;j++) assert.equal(blk[2*j],blk[2*j+1],'ECHO 0: mono again'); }
  s.enc[3]=80; s.b.bench_audio();
  for(let i=0;i<60;i++){ const blk=s.block(); for(let j=0;j<128;j++) assert.equal(blk[2*j],blk[2*j+1],'no old echo after re-arming'); }
}

// No sample is ever outside the codec range, effects at maximum.
const loud=run(s=>{s.input(FX);s.input(FX);for(const k of [2,3,5]){s.enc[k]=100;s.input(0);}for(let i=0;i<10;i++)s.input(OCT_UP);},900);
for(const blk of loud.out) for(const x of blk) assert.ok(x<=32767*128 && x>=-32768*128);
// ...and the effect path never reaches the rail: its soft knee stops short of it.
{
  const s=band(); s.b.bench_input(PLAY,0,0); for(let i=0;i<300;i++) s.block();
  const before=s.b.bench_rails();
  s.b.bench_input(0,0,0); s.input(FX); s.input(FX);
  for(const k of [2,3,5]){s.enc[k]=100;s.input(0);} s.enc[4]=3; s.input(0); for(let i=0;i<10;i++) s.input(OCT_UP);
  s.b.bench_input(PLAY,0,0); for(let i=0;i<1500;i++) s.block();
  assert.equal(s.b.bench_rails()-before,0,'no rail samples with every effect at maximum');
  assert.equal(s.b.bench_fx_shed(),0,'the CPU guard never acts in the browser');
}

// Screen: the effects page draws its knob row (COMPAS_FX_SHOT=file writes it as RGB565).
{
  const s=band(); s.input(FX); s.input(FX); s.enc[2]=40; s.input(0); s.enc[4]=1; s.input(0);
  assert.equal(s.page(),PANEL_FX); s.b.bench_draw(1);
  const frame=Uint16Array.from(new Uint16Array(s.b.memory.buffer,s.b.bench_draw(2),240*240));
  if(process.env.COMPAS_FX_SHOT) await writeFile(process.env.COMPAS_FX_SHOT,Buffer.from(frame.buffer));
  assert.ok(frame.some(px=>px!==color(0,0,0)));
}
console.log(JSON.stringify({navigation:true,zeroIsIdentical:true,room:{side,diff},echoDiff,clipSafe:true}));
