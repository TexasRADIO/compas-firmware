import test from 'node:test';
import assert from 'node:assert/strict';
import {Updater} from '../web/installer/fm1ota.js';
const product='FM-1_92210003';
const make=()=>new Updater({inputs:new Map(),outputs:new Map()});
test('completed transfer without startup reply stays unverified; no retry write',async()=>{
 const up=make();let writes=0,steps=[];
 up.write=async()=>{writes++;};
 up.waitFor=async(filter,ms)=>{assert.equal(ms,60000);assert(!filter({model:'ota-FM-1'}));return null;};
 await assert.rejects(up.verifyRestart(product,(...s)=>steps.push(s)),e=>e.code==='written-unverified'&&e.transferComplete===true&&e.expectedIdentity===product&&/transfer completed/i.test(e.message));
 assert.equal(writes,0);assert.deepEqual(steps,[]);
});
test('read-only restart recheck succeeds only on the exact full identity',async()=>{
 const up=make();let closed=0,steps=[];
 up.waitFor=async filter=>{const id={model:'FM-1',text:product};assert(filter(id));return {id,link:{close(){closed++;}}};};
 assert.equal(await up.verifyRestart(product,(...s)=>steps.push(s)),product);
 assert.equal(closed,1);assert.deepEqual(steps,[['done',product]]);
});
test('different returned identity never reports success',async()=>{
 const up=make();let done=false;
 up.waitFor=async()=>({id:{model:'FM-1',text:'FM-1_92200203'},link:{close(){}}});
 await assert.rejects(up.verifyRestart(product,()=>{done=true}),e=>e.code==='mismatch'&&e.transferComplete===true);
 assert.equal(done,false);
});
