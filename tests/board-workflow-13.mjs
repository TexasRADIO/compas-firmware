// Compás 1.3 workflow: SAVE from any page, the unsaved mark, Energy on every
// page, the language knob, and the three fill states (waiting, sounding,
// finished) with the late, count-in and refused cases.
import assert from 'node:assert/strict';
import {bench,BUTTON as B,ENC as E,VIEW as V} from './lib/bench.mjs';

const groove={title:'CUMBIA',study:'mx',bpm:120};
const salsa={title:'SALSA',study:'salsa',bpm:108,clave:1};
const KIT=11, RESULT=13, SPEED=72;
const PAGE={HOME:0,MIX:1,EDIT:2,RACK:3,SET:4,BASS:5,PERC:6,HANAN:7,FX:8};
const BUSY=1, SESSION=11;
const pink=63857, gold=65312;
const page=s=>s.snapshot()[0], slot=s=>s.snapshot()[5];
const edited=s=>s.audio()[KIT]>>1;
const start=s=>{ s.input(B.PLAY); s.until(v=>v[V.playing]&&(v[V.position]>>2)===1); };

// ---- SAVE keeps the song where it lives, from any page
{
  const s=bench([groove,salsa],{live:true});
  assert.equal(edited(s),0,'a loaded song is clean');
  s.turn(E.K1,4);
  assert.equal(edited(s),1,'a tempo change marks the song');
  assert.equal(s.audio()[KIT]&1,s.audio()[KIT]&1,'kit bit kept');
  const v=s.input(B.SAVE);
  assert.equal(page(s),PAGE.HOME,'SAVE stays on Band');
  assert.equal(v[RESULT],SESSION,'saved');
  assert.equal(edited(s),0,'SAVE clears the mark');
  s.input(B.FX); s.turn(E.K3,-10);
  assert.equal(edited(s),1,'a level change marks the song');
  s.input(B.SAVE);
  assert.equal(page(s),PAGE.MIX,'SAVE stays on the mixer');
  assert.equal(edited(s),0);
  // Playing is not editing.
  s.input(B.HOME); s.input(0,1<<8); s.input(0,1<<1);
  assert.equal(edited(s),0,'layer toggles and pads do not mark the song');
  // A recall replaces the edits and the mark.
  s.turn(E.K1,2); assert.equal(edited(s),1);
  s.turn(E.PRESETS,1);
  assert.equal(s.audio()[V.song],1); assert.equal(edited(s),0,'a new song is clean');
}

// ---- A rhythm preset has no slot: SAVE opens the show on the first free one
{
  const s=bench([groove,salsa],{live:true});
  s.input(B.REC); s.turn(E.PRESETS,3);
  assert.equal(s.audio()[V.song],16,'a preset, not a show song');
  s.input(B.HOME); s.input(B.SAVE);
  assert.equal(page(s),PAGE.SET,'SAVE opens the show');
  assert.equal(slot(s),2,'on the first free slot');
  const v=s.input(B.SAVE);
  assert.equal(v[RESULT],SESSION); assert.equal(v[V.song],2,'saved there');
}

// ---- Energy is ALGORITHM on every page and steps from the queued level
{
  const s=bench([groove,salsa],{live:true});
  start(s);
  s.input(B.GLO); s.turn(E.ALGORITHM,1);
  assert.equal(s.audio()[V.pending],1,'Energy on the show page');
  s.input(B.FX); s.turn(E.ALGORITHM,1);
  assert.equal(s.audio()[V.pending],2,'the mixer steps from the queued level');
  s.input(B.EDIT); s.turn(E.ALGORITHM,-1);
  assert.equal(s.audio()[V.pending],1,'and so does the step page');
}

// ---- KNOB3 on the show page is the language; SELECT moves the cursor
{
  const s=bench([groove,salsa],{live:true});
  s.input(B.GLO);
  const label=()=>s.pixels(141,198,36,9).join();
  const english=label();
  s.turn(E.K3,1);
  assert.notEqual(label(),english,'the knob label follows the language');
  s.turn(E.K3,-1);
  assert.equal(label(),english,'and comes back');
  s.turn(E.SELECT,1);
  assert.equal(slot(s),1,'SELECT moves the cursor');
}

// ---- PERC twice opens Kumbia where the build has it; leaving the 727 closes its editor
{
  const s=bench([groove,salsa],{live:true});
  s.input(B.LFO); assert.equal(page(s),PAGE.PERC);
  s.input(B.LFO); assert([PAGE.HANAN,PAGE.HOME].includes(page(s)));
  if(page(s)===PAGE.HANAN) {
    // A Kumbia tile lights while its voice sounds.
    const tiles=new Set(); s.input(B.PLAY);
    for(let i=0;i<120;++i) { s.wait(10); tiles.add(s.pixels(5,150,1,1)[0]); }
    assert.equal(tiles.size,2,'the bombo tile lights and rests');
    s.input(B.PLAY); s.input(B.LFO); assert.equal(page(s),PAGE.PERC,'and back');
  }
  s.input(B.REC); s.input(B.SEQ);
  assert.equal(s.snapshot()[18],1,'step editor open');
  s.input(B.HOME);
  assert.equal(s.snapshot()[18],0,'HOME closes it');
}

// ---- Fill: waiting, sounding, finished
{
  const s=bench([groove,salsa],{live:true});
  const waiting=()=>s.pixels(141,94,12,9).includes(pink);
  const sounding=()=>s.pixels(153,94,36,9).includes(gold);
  start(s);
  assert(!waiting() && !sounding(),'no fill shown before one is asked for');
  const steps=32;
  s.input(B.ARP);
  assert.equal(s.audio()[V.fill],1); s.wait(60);
  assert(waiting() && !sounding(),'an armed fill reads as waiting');
  let v=s.until(v=>v[V.fill]===2);
  assert.equal(v[V.position],steps-4,'it sounds on the last beat of the phrase'); s.wait(60);
  assert(sounding() && !waiting(),'and reads as sounding while it does');
  v=s.until(v=>v[V.fill]===0);
  assert.equal(v[V.position],0,'finished at the top of the next phrase'); s.wait(60);
  assert(!waiting() && !sounding(),'a finished fill leaves the stage');

  // Too late for this phrase: it waits for the next one, and says so.
  s.until(v=>v[V.position]===steps-3);
  s.input(B.ARP);
  v=s.until(v=>v[V.position]===2);
  assert.equal(v[V.fill],1,'a late request waits a whole phrase'); s.wait(60);
  assert(waiting() && !sounding());
  v=s.until(v=>v[V.fill]===2);
  assert.equal(v[V.position],steps-4);
  s.until(v=>v[V.fill]===0);

  // With an Energy change queued the fill leads into it. A second press is
  // refused and changes nothing on stage.
  s.until(v=>v[V.position]===2);
  s.turn(E.ALGORITHM,1);
  v=s.input(B.ARP);
  assert.equal(v[V.fill],1,'the fill joins the queued change');
  v=s.input(B.ARP);
  assert.equal(v[RESULT],BUSY,'a second press is refused'); assert.equal(v[V.fill],1);
  v=s.until(v=>v[V.fill]===2);
  assert.equal(v[V.position],steps-4);
  s.until(v=>v[V.section]===1);
  assert.equal(s.audio()[V.fill],0);

  // REBAJADA slows the band, not the rule.
  s.input(B.FX); s.turn(E.K4,-20); s.input(B.HOME);
  assert.equal(s.audio()[SPEED],80);
  s.until(v=>v[V.position]===4);
  s.input(B.ARP);
  v=s.until(v=>v[V.fill]===2,20000);
  assert.equal(v[V.position],steps-4,'the fill keeps its place under REBAJADA');
}

// ---- Refused during the count-in
{
  const s=bench([{...groove,countIn:true},salsa],{live:true});
  s.input(B.PLAY); s.wait(300);
  const v=s.input(B.ARP);
  assert.equal(v[V.fill],0,'no fill is armed during the count');
  assert.equal(v[RESULT],BUSY);
  assert(!s.pixels(141,94,12,9).includes(pink));
}
// ---- Animation carries state: lane lamps, the clave under the bar
{
  const s=bench([groove,salsa],{live:true});
  const mint=2047>>0, AUDIBLE=10;
  s.input(B.FX);
  assert.equal(s.audio()[AUDIBLE]>>1,0,'no lane sounds while stopped');
  s.input(B.PLAY);
  const v=s.until(v=>v[AUDIBLE]>>1);
  const lanes=v[AUDIBLE]>>1; s.wait(40);
  for(let i=0;i<6;++i) {
    const lit=s.pixels(4+i*39,133,36,3).some(p=>p!==s.pixels(4+i*39,133,36,3)[0]) ? null : s.pixels(4+i*39,133,1,1)[0];
    const now=s.audio()[AUDIBLE]>>1;
    if(((now>>i)&1)===((lanes>>i)&1)) assert.notEqual(lit,null,'a lamp is one flat colour');
  }
  assert.equal(s.audio()[AUDIBLE]&1,1,'the sounding flag keeps its meaning');
  s.input(B.PLAY);

  // Salsa: five strokes under the cones, and the one being played lights.
  s.input(B.HOME); s.turn(E.PRESETS,1); s.wait(1500);
  assert.equal(s.audio()[V.study],2,'salsa');
  const tick=step=>s.pixels(5+22*(step>>2)+(step>=16?6:0)+(step&3)*5+1,128,1,1)[0];
  // This song is saved 2-3, so the two-side comes first.
  const rest=tick(4);
  for(const step of [4,8,16,22,28]) assert.equal(tick(step),rest,'stroke '+step+' is marked');
  assert.notEqual(tick(0),rest,'and nothing else is');
  s.input(B.PLAY); s.until(v=>v[V.playing]&&v[V.position]===8); s.wait(40);
  assert.notEqual(tick(8),rest,'the stroke being played lights');
  assert.equal(tick(16),rest);
}
// ---- The marquee plays along
{
  const s=bench([groove,salsa],{live:true});
  const lamp=i=>s.pixels(2+i*8+2,3,1,1)[0];
  // Head lamp follows each sixteenth, not only the beat.
  s.input(B.PLAY);
  const seen=new Set();
  for(const step of [5,6,7]) {
    s.until(v=>v[V.playing]&&v[V.position]===step); s.wait(20);
    const c=lamp(step); seen.add(c);
    assert.notEqual(lamp(step),lamp(step+8),'lamp '+step+' differs from a resting lamp of the same colour');
  }
  assert.equal(seen.size,1,'the head lamp is one colour on every step');
  const head=[...seen][0];
  // On LIFT the head drags a tail of two lamps.
  s.turn(E.ALGORITHM,1); s.until(v=>v[V.section]===1);
  s.until(v=>v[V.position]===12); s.wait(20);
  assert.deepEqual([lamp(10),lamp(11),lamp(12)],[head,head,head],'a three-lamp streak on LIFT');
  // Stopped, the sign waits calmly, then idles in a slow chase.
  s.input(B.PLAY); s.wait(1000);
  const calm=Array.from({length:30},(_,i)=>lamp(i)).join();
  s.wait(4000);
  const idle1=Array.from({length:30},(_,i)=>lamp(i)).join();
  assert.notEqual(idle1,calm,'after four seconds the sign idles');
  s.wait(500);
  const idle2=Array.from({length:30},(_,i)=>lamp(i)).join();
  assert.notEqual(idle2,idle1,'and the chase moves');
  s.input(B.PLAY); s.wait(60);
  assert.equal(lamp(15),lamp(15),'playing again');
}

// ---- The count-in is written large on the sign; SAVE flashes it
{
  const s=bench([{...groove,countIn:true},salsa],{live:true});
  const numeral=i=>s.pixels(138-4*25+i*50+4,40,6,8);
  const pink=63857;
  s.input(B.PLAY); s.wait(40);
  assert(numeral(0).includes(pink),'count one');
  assert(!numeral(1).includes(pink));
  s.until(v=>(v[V.position]>>2)===2); s.wait(40);
  assert(numeral(2).includes(pink),'count three'); assert(!numeral(0).includes(pink));
  s.input(B.PLAY); s.wait(60);
  const paper=s.pixels(120,30,1,1)[0];
  s.turn(E.K1,2); s.input(B.SAVE); s.wait(40);
  assert.notEqual(s.pixels(120,30,1,1)[0],paper,'the sign flashes on SAVE');
  s.wait(500);
  assert.equal(s.pixels(120,30,1,1)[0],paper,'and settles');
}
console.log('1.3 workflow pass: SAVE anywhere, unsaved mark, Energy, language knob, fill states.');
