import test from 'node:test';
import assert from 'node:assert/strict';
import {showPayload,serializeShow,showSongSlots,showSongTitle,reorderShow} from '../web/show-format.mjs';
test('show wrapper round-trips exact bytes and detects changes in either payload',()=>{
  const set=Uint8Array.from({length:1376},(_,i)=>i&255);
  const rhythms=Uint8Array.from({length:2416},(_,i)=>(i*17)&255);
  const file=serializeShow(set,rhythms);
  assert.deepEqual(showPayload(JSON.parse(JSON.stringify(file))),{payload:set,rhythms});
  for(const key of ['set','rhythms']) for(const index of [0,file[key].length-1]) {
    const bad=structuredClone(file);bad[key][index]^=1;
    assert.throws(()=>showPayload(bad),/damaged/);
  }
  for(const changes of [{version:3},{extra:true},{crc32:-1},{set:[1]},{rhythms:[0]}])
    assert.throws(()=>showPayload({...file,...changes}));
  for(const value of [-1,256,0.5,null,'0']) {
    const bad=structuredClone(file);bad.set[0]=value;
    assert.throws(()=>showPayload(bad),/Invalid/);
  }
});


test('reordering moves complete song and rhythm records around hidden slots',()=>{
  const set=new Uint8Array(1376), rhythms=new Uint8Array(2416);
  for(let slot=0;slot<16;++slot) {
    set[slot*86]=127;
    rhythms[slot*151+140]=255;
  }
  for(const [slot,title,tag] of [[0,'CUMBIA ELeCTRICA',1],[3,'SALSA',2]]) {
    const start=slot*86;
    set.set([...title].map(char=>char.charCodeAt(0)),start);
    set.fill(tag,start+19,start+86);
    rhythms.fill(tag,slot*151,(slot+1)*151);
  }
  const original=showPayload(serializeShow(set,rhythms));
  assert.deepEqual(showSongSlots(original.payload),[0,3]);
  assert.equal(showSongTitle(original.payload,0),'CUMBIA ELÉCTRICA');
  const reordered=showPayload(reorderShow(original,[3,0]));
  for(const [first,second,size] of [[original.payload,reordered.payload,86],[original.rhythms,reordered.rhythms,151]]) {
    assert.deepEqual(second.slice(0,size),first.slice(3*size,4*size));
    assert.deepEqual(second.slice(3*size,4*size),first.slice(0,size));
    assert.deepEqual(second.slice(size,3*size),first.slice(size,3*size));
    assert.deepEqual(second.slice(4*size),first.slice(4*size));
  }
  assert.deepEqual(original,{payload:set,rhythms});
  for(const order of [[0],[3,3],[0,1],[0,3,4]])
    assert.throws(()=>reorderShow(original,order),/Invalid song order/);
  const fallback=set.slice();fallback[86]=0;
  assert.throws(()=>showSongSlots(fallback),/Save this show/);
  const empty=set.slice();empty[0]=empty[3*86]=127;
  assert.throws(()=>showSongSlots(empty),/no songs/);
});
