import {save,recall} from './store.mjs';
const $ = id=>document.getElementById(id);
const message=text=>$('notice').textContent=text;
const names=['Base','Lift','Break'];
const results=['','Busy · an action is queued.','Stop first.','Part is silent or unavailable.','Action unavailable.','Invalid project · previous state kept.','Press Play first.'];
let ctx,node,booting=false,inflight,serial=0,selected=0,studyID=-1,stepButtons=[],parts=[],scores=[],levels=[];
let state=[0,0,3,0,0,0,94,30,0,6,0,31,44100,0,0];
let cameoTimer;
const cameo=document.querySelector('.channel');
function showJohnny() {
  clearTimeout(cameoTimer);
  cameo.classList.add('on-air');
  cameoTimer=setTimeout(()=>cameo.classList.remove('on-air'),900);
}
showJohnny();
const score=await fetch('../src/score.json').then(r=>r.json()).catch(()=>null);
if(!score) { $('play').disabled=true; message('Score unavailable.'); }
for (const [i,s] of (score?.studies ?? []).entries()) $('study').add(new Option(s.title,i));
const roots=['C','C♯ / D♭','D','E♭','E','F','F♯ / G♭','G','A♭','A','B♭','B'];
for (const [i,n] of roots.entries()) $('root').add(new Option(n,i));
const wasm=fetch('../build/engine.wasm').then(r=>{if(!r.ok)throw Error('Engine not built.');return r.arrayBuffer();}).then(WebAssembly.compile).catch(e=>e);
function command(cmd,a,b,project) {
  if(!node) return Promise.reject(Error('Press Play to enable the bench.'));
  if(inflight) return Promise.reject(Error('Busy · try again after the current action.'));
  return new Promise((resolve,reject)=>{
    const id=++serial;
    const timeout=setTimeout(()=>fail(Error('Engine did not reply. Audio stopped.')),1500);
    inflight={id,resolve,reject,timeout};
    node.port.postMessage({id,cmd,a,b,project});
  });
}
function fail(error) {
  if(inflight){clearTimeout(inflight.timeout);inflight.reject(error);inflight=undefined;}
  if(node) {node.disconnect();node.port.onmessage=null;node=null;}
  if(ctx && ctx.state!=='closed') ctx.close().catch(()=>{});
  ctx=null; state[0]=0; state[2]=3; state[3]=0;
  render(); message(error.message);
}
async function act(cmd,a,b,project,ok='') {
  try {
    const response=await command(cmd,a,b,project);
    if(response.result) {message(results[response.result]);return null;}
    message(ok);
    return response;
  } catch(error) {message(error.message);return null;}
}
async function start() {
  if(booting) return;
  if(node) { await act(1); return; }
  booting=true; $('play').disabled=true;
  try {
    ctx=new AudioContext({sampleRate:44100,latencyHint:'interactive'});
    await ctx.resume();
    const module=await wasm;
    if(module instanceof Error) throw module;
    await ctx.audioWorklet.addModule('processor.mjs');
    node=new AudioWorkletNode(ctx,'compas',{outputChannelCount:[1],numberOfInputs:0,processorOptions:{module}});
    node.onprocessorerror=()=>fail(Error('Audio engine fault. Audio stopped.'));
    node.port.onmessage=({data})=>{
      state=data.state;
      if(data.score){scores=data.score;levels=data.levels;}
      render();
      if(data.id && inflight?.id===data.id){
        clearTimeout(inflight.timeout);const resolve=inflight.resolve;inflight=undefined;resolve(data);
      }
    };
    node.connect(ctx.destination);
    await act(1);
  } catch(error) {fail(error);}
  finally {booting=false;render();}
}
function button(label, action, className) {
  const b=document.createElement('button'); b.textContent=label;
  if(className) b.className=className;
  b.addEventListener('click',action);return b;
}
function buildParts(s) {
  $('parts').replaceChildren();$('edit-part').replaceChildren();
  parts=s.parts.map(([role,voice],i)=>{
    const box=document.createElement('div');box.className='part';box.hidden=voice==='none';
    const pad=button(role,()=>act(13,i,105),'pad');
    const mute=button('Mute',()=>act(6,i));
    const solo=button('Solo',()=>act(7,i));
    mute.setAttribute('aria-label','Mute '+role);solo.setAttribute('aria-label','Solo '+role);
    const label=document.createElement('label'); label.textContent='Level ';
    const range=document.createElement('input'); range.type='range';range.min=0;range.max=100;
    range.setAttribute('aria-label',role+' level');
    range.onchange=()=>act(8,i,+range.value);label.append(range);
    box.append(pad,mute,solo,label);$('parts').append(box);
    if(voice!=='none') $('edit-part').add(new Option(role,i));
    return {box,pad,mute,solo,range,none:voice==='none'};
  });
  if(parts[selected]?.none) selected=0;
  $('edit-part').value=selected;
  $('steps').replaceChildren();
  stepButtons=[];
  for(let bar=0;bar<2;bar++){
    const row=document.createElement('div');row.className='bar'+(s.meter===2?' short':'');
    for(let j=0;j<s.meter*4;j++){
      const index=bar*s.meter*4+j;
      const b=button(String(j+1),()=>act(11,selected,index),'step'+(j%4===0?' beat':''));
      row.append(b);stepButtons.push(b);
    }
    $('steps').append(row);
  }
}
function render() {
  if(!score) return;
  const [playing,section,pending,fill,pos,id,bpm,feel,muted,solo,reverse,available,,,root]=state;
  const s=score.studies[id];
  if(studyID!==id){
    if(studyID>=0) showJohnny();
    studyID=id;buildParts(s);$('description').textContent=s.note;
    $('beat-lamps').replaceChildren(...Array.from({length:s.meter*2},()=>document.createElement('i')));
  }
  for(const [i,lamp] of [...$('beat-lamps').children].entries())
    lamp.classList.toggle('lit',!!playing && i===Math.floor(pos/4));
  $('study-title').textContent=s.short;
  $('transport').textContent=playing ? (available ? 'PLAYING' : 'PLAYING · MIX SILENT') : 'STOPPED';
  $('arrangement').textContent=names[section].toUpperCase()+(pending!==3?' → '+names[pending].toUpperCase():'')+(fill===1?' · FILL QUEUED':fill===2?' · FILL':'');
  $('readout').textContent=bpm+' BPM · '+s.meter+'/4'+((id===2||id===6)?(reverse?' · 2–3':' · 3–2'):'')+(playing?' · BAR '+(1+Math.floor(pos/(s.meter*4))):'');
  $('study').value=id;$('study').disabled=!node||!!playing;
  $('clave-row').hidden=id!==2&&id!==6;$('clave').value=reverse;$('clave').disabled=!node||!!playing;
  $('root-row').hidden=id===0;$('root').value=root;$('root').disabled=!node||!!playing;
  $('play').disabled=booting||!!playing;$('stop').disabled=!playing;
  $('fill').disabled=!playing;$('fill').setAttribute('aria-pressed',fill!==0);
  for (const b of document.querySelectorAll('[data-section]')){
    const target=+b.dataset.section;b.disabled=!node;b.classList.toggle('queued',target===pending);
    b.setAttribute('aria-pressed',target===section);
  }
  for(const [key,v] of [['tempo',bpm],['feel',feel]]){
    $(key).disabled=!node;
    if(document.activeElement!==$(key)) $(key).value=v;
    $(key+'-value').value=$(key).value;
  }
  for(const [i,p] of parts.entries()){
    p.box.classList.toggle('dim',!(available&(1<<i)));
    p.pad.disabled=p.none||!playing;p.mute.disabled=p.solo.disabled=p.range.disabled=p.none||!node;
    p.mute.setAttribute('aria-pressed',!!(muted&(1<<i)));p.solo.setAttribute('aria-pressed',solo===i);
    if(document.activeElement!==p.range) p.range.value=levels[i]??100;
  }
  for(const [i,b] of stepButtons.entries()){
    const value=scores[selected]?.[i]??0;
    b.disabled=!node||((id===2||id===6)&&selected===0);
    b.classList.toggle('now',!!playing&&pos===i);
    b.setAttribute('aria-pressed',!!value);
    b.setAttribute('aria-label','Step '+(i+1)+(value===2?' · accented':value>=16?' · bass +'+(value-16)+' semitones':''));
  }
  $('save').disabled=$('export').disabled=!node;
  $('load').disabled=$('import').disabled=!node||!!playing;
}
$('play').onclick=start;
$('stop').onclick=()=>act(2,0,0,undefined,'Stopped.');
$('fill').onclick=()=>act(5);
for(const b of document.querySelectorAll('[data-section]')) b.onclick=()=>act(4,+b.dataset.section);
$('study').onchange=async()=>{await act(3,+$('study').value);render();};
$('root').onchange=async()=>{await act(14,+$('root').value);render();};
$('clave').onchange=async()=>{await act(12,+$('clave').value);render();};
$('edit-part').onchange=()=>{selected=+$('edit-part').value;render();};
for (const [key,cmd] of [['tempo',9],['feel',10]]){
  $(key).oninput=()=>$(key+'-value').value=$(key).value;
  $(key).onchange=()=>act(cmd,+$(key).value);
}
$('save').onclick=async()=>{
  const result=await act('snapshot');if(!result)return;
  try {
    if(!navigator.locks) throw Error('Browser save lock unavailable; use Export project.');
    await navigator.locks.request('compas-save-v1',()=>save(localStorage,result.snapshot));
    message('Saved here.');
  }catch(error){message(error.message);}
};
$('load').onclick=()=>{
  try {void act('open',0,0,recall(localStorage),'Project recalled.');}
  catch(error){message(error.message);}
};
$('export').onclick=async()=>{
  const result=await act('snapshot');if(!result)return;
  const url=URL.createObjectURL(new Blob([result.snapshot],{type:'application/octet-stream'}));
  const a=document.createElement('a');a.download='compas.cps';a.href=url;a.click();
  setTimeout(()=>URL.revokeObjectURL(url),1000);message('Project exported.');
};
$('import').onchange=async()=>{
  const file=$('import').files[0];$('import').value='';
  if(!file)return;
  if(file.size!==55){message('Invalid project size · previous state kept.');return;}
  try {await act('open',0,0,new Uint8Array(await file.arrayBuffer()),'Project opened.');}
  catch(error){message(error.message);}
};
render();
