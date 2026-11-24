const $=id=>document.getElementById(id);
const kitKey='compas-kumbia-kit-v2';
let kitSettings;
if(WebAssembly.Module.exports(module).some(entry=>entry.name==='bench_kit_restore')) {
  for(const key of [kitKey,'compas-kumbia-kit-v1']) {
    try {
      const values=JSON.parse(localStorage.getItem(key));
      if(Array.isArray(values) && (values.length===4 || values.length===5) &&
         values.every((v,i)=>Number.isInteger(v) && v>=[0,5,0,0,0][i] && v<=[100,100,24,54,100][i])) {
        kitSettings=values;break;
      }
    } catch {} // Storage can be unavailable for private or local-file browsing.
  }
}
function copySet(board,payload) {
  if (payload.length!==board.bench_set_size()) throw Error('Invalid prepared set length.');
  new Uint8Array(board.memory.buffer,board.bench_set_buffer(),payload.length).set(payload);
}
function fileBusy(busy) {
  for(const id of ['save-show','save-show-text','save-rhythm','open-rhythm','open-set','rehearsal-set']) $(id).disabled=busy;
}
function renderBoard(payload,rhythms) {
  const board=new WebAssembly.Instance(module).exports;
  if (payload) {
    copySet(board,payload);
    if(rhythms) {
      if(rhythms.length!==board.bench_rhythm_slots_size()) throw Error('Invalid show rhythm length.');
      new Uint8Array(board.memory.buffer,board.bench_rhythm_slots_buffer(),rhythms.length).set(rhythms);
    }
  }
  if (!board.bench_boot(Number(Boolean(payload))|(rhythms?2:0)|(startupHanan?4:0))) throw Error('The board rejected the prepared set.');
  if(kitSettings) {
    board.bench_kit_restore?.(...kitSettings.slice(0,4));
    board.bench_kit_level?.(kitSettings[4]??100);
  }
  board.bench_audio();
  board.bench_read_view();
  return board;
}
function decodeSet(set) {
  return set?.format==='compas-show'?showPayload(set):{payload:preparedPayload(set,setCatalog)};
}
const bookmark=location.hash.slice(1);
const linkedRehearsal=Object.hasOwn(rehearsalSets,bookmark) ? rehearsalSets[bookmark] : undefined;
const initialSet=linkedRehearsal?.set ?? startupSet;
let {payload:preparedSet,rhythms:preparedRhythms}=initialSet ? decodeSet(initialSet) : {};
let board=renderBoard(preparedSet,preparedRhythms), pendingSet, pendingRhythm, pendingShow;
$('set-name').textContent=linkedRehearsal?.name ?? startupName;
const screen=$('screen').getContext('2d');
const picture=screen.createImageData(240,240);
let audio,node,keys=0,padsReady=false,standbySong=0;
const time=()=>Math.floor(performance.now()*24000)>>>0;
function draw(addr) {
  const pixels=new Uint16Array(board.memory.buffer,addr,57600);
  for(let i=0;i<pixels.length;i++) {
    const p=pixels[i];
    const offset=i*4;
    picture.data[offset]=(p>>11)*255/31;
    picture.data[offset+1]=((p>>5)&63)*255/63;
    picture.data[offset+2]=(p&31)*255/31;
    picture.data[offset+3]=255;
  }
  screen.putImageData(picture,0,0);
  if(padsReady) updateKeys();
}
let splashTimer, idleTimer;
function showStandby() {
  clearTimeout(splashTimer);
  clearTimeout(idleTimer);
  splashTimer=undefined;
  draw(board.bench_draw(time()));
  // The main thread owns this clock even before an AudioWorklet exists.
  idleTimer=setTimeout(()=>{
    if(!node) draw(board.bench_draw(time()));
  },910);
}
draw(board.bench_splash());
splashTimer=setTimeout(showStandby,650);
let openingAudio, pendingPlay=false;
function enable() {
  if(node && !openingAudio) return;
  if(!openingAudio) openingAudio=startAudio().finally(()=>{openingAudio=undefined;});
  return openingAudio;
}
function audioDeadline(operation, message, milliseconds=4000) {
  let timer;
  const deadline=new Promise((_,reject)=>{
    timer=setTimeout(()=>reject(Error(message)),milliseconds);
  });
  return Promise.race([operation,deadline]).finally(()=>clearTimeout(timer));
}
async function loadAudioWorklet(context) {
  if(location.protocol!=='file:') {
    try {
      await audioDeadline(context.audioWorklet.addModule(new URL('board-worklet.mjs?v=4',location.href),{credentials:'same-origin'}),
        'The audio module did not load.',1500);
      return;
    } catch {
      // Private hosting and in-app browsers can block the separate module request.
    }
  }
  await audioDeadline(context.audioWorklet.addModule(embeddedWorkletURL),'The embedded audio module did not load.',1500);
}
function openCompatibilityAudio(context, source, options) {
  const fallback=context.createScriptProcessor(1024,1,2);
  const {port1,port2}=new MessageChannel();
  const renderer=new BoardAudio(options,context.sampleRate,(data,transfer)=>port2.postMessage(data,transfer??[]));
  fallback.port=port1;
  port2.onmessage=({data})=>renderer.receive(data);
  const render=({outputBuffer})=>{
    try {
      renderer.render([outputBuffer.getChannelData(0),outputBuffer.getChannelData(1)]);
    } catch(error) {
      fallback.onprocessorerror(error);
    }
  };
  fallback.addEventListener('audioprocess',render);
  fallback.release=()=>{
    source.disconnect(fallback);
    fallback.removeEventListener('audioprocess',render);
    port2.close();
  };
  source.connect(fallback);
  return fallback;
}
function disconnectAudioNode() {
  if(!node) return;
  node.disconnect();
  node.port.close();
  node.release?.();
  node=undefined;
}
function resetAudio() {
  disconnectAudioNode();
  if(audio && audio.state!=='closed') audio.close().catch(()=>{});
  audio=undefined;
  $('enable').disabled=false;
  $('transport').textContent='PLAY';
  $('transport').dataset.playing='false';
}
function connectAudioNode(context, candidate) {
  node=candidate;
  let ready;
  const rendering=new Promise((resolve,reject)=>{
    ready=resolve;
    node.onprocessorerror=()=>reject(Error('The audio engine did not start.'));
  });
  node.port.onmessage=({data})=>{
    if(data.cueResult) {
      $('message').textContent='Song cue could not be queued. Tap the song again.';
      return;
    }
    if(data.showResult !== undefined) {
      pendingShow=false;fileBusy(false);
      if(data.showResult) {
        $('message').textContent=data.showResult===2?'Press STOP before saving a show.':data.showResult===5?'Choose a set slot with GLO, then SAVE this rhythm before saving the show.':'Unable to save show. Let the current control change finish and try again.';
      } else {
        const dataFile=serializeShow(data.showSet,data.showRhythms);
        const asText=showAsText;showAsText=false;
        const blob=asText?new Blob([showToText(showPayload(dataFile))],{type:'text/plain'}):
          new Blob([JSON.stringify(dataFile)],{type:'application/json'});
        const url=URL.createObjectURL(blob),link=document.createElement('a');
        link.href=url;link.download=asText?'Compas-show.txt':'Compas.cpshow';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
        $('message').textContent='Show saved: song slots, attached rhythms and current edits. Reopening starts at the first slot.';
      }
      return;
    }
    if (data.rhythmResult !== undefined) {
      const requested=pendingRhythm;
      pendingRhythm=undefined;
      fileBusy(false);
      if(data.rhythmResult) {
        const why={1:'Another control change is pending. Try again.',2:'Press STOP before opening a rhythm.',
          3:'Choose a groove rather than a written score.',4:'Choose a groove with the same meter as this rhythm.',
          5:'This rhythm file is invalid or damaged.',8:'Audio fault; restart the instrument.'};
        $('message').textContent=(why[data.rhythmResult] || 'Rhythm request failed.')+' Current pattern kept.';
      } else if(data.rhythmAction==='export') {
        const blob=new Blob([data.rhythmPayload],{type:'application/octet-stream'});
        const url=URL.createObjectURL(blob), link=document.createElement('a');
        link.href=url;link.download='Compas-727.c7r';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
        $('message').textContent='Rhythm file saved. Keep it to restore this pattern later.';
      } else {
        $('message').textContent='Rhythm opened: '+requested.name+'. Press PLAY.';
      }
      return;
    }
    if (data.loadResult !== undefined) {
      const requested=pendingSet;
      pendingSet=undefined;
      fileBusy(false);
      if (data.loadResult === 'loaded') {
        acceptSet(requested);
      } else {
        $('message').textContent=data.loadResult === 'playing' ?
          'Press STOP before loading another set. PRESETS still cues songs. Current set kept.' : 'The board rejected the set. Current set kept.';
      }
      return;
    }
    if (data.savedSet) {
      copySet(board,data.savedSet);
      if (board.bench_update_set_display()) renderRunningOrder();
      else $('message').textContent='Unable to refresh the saved slot display.';
      return;
    }
    ready();
    const kit=kitFromSnapshot(data);
    if(kit && kit.some((value,i)=>value!==kitSettings?.[i])) {
      kitSettings=kit;
      try { localStorage.setItem(kitKey,JSON.stringify(kit)); } catch {}
    }
    new Uint8Array(board.memory.buffer,board.bench_snapshot(),data.length).set(data);
    draw(board.bench_draw(time()));
  };
  node.connect(context.destination);
  return audioDeadline(rendering,'The audio engine did not produce a first frame.',2500);
}
async function startAudio() {
  if(splashTimer!==undefined) showStandby();
  fileBusy(true);
  try {
    $('message').textContent='Starting audio…';
    audio=new AudioContext({latencyHint:'interactive'});
    // Start a silent source and resume in the tap, before any network work.
    const source=audio.createBufferSource();
    source.buffer=audio.createBuffer(1,128,audio.sampleRate);
    source.loop=true;
    source.connect(audio.destination);
    source.start();
    const resumed=audioDeadline(audio.resume(),'The browser did not enable audio. Open this page in Safari and tap PLAY again.');
    resumed.catch(()=>{});
    const options={module,preparedSet,preparedRhythms,standbySong,startupHanan,kitSettings};
    let compatibility=false;
    try {
      if(!audio.audioWorklet) throw Error('AudioWorklet is unavailable.');
      await loadAudioWorklet(audio);
      await Promise.all([resumed,connectAudioNode(audio,new AudioWorkletNode(audio,'compas-board',{
        numberOfInputs:0,outputChannelCount:[2],processorOptions:options}))]);
    } catch {
      disconnectAudioNode();
      // An activation failure needs a fresh gesture even on the compatibility path.
      await resumed;
      compatibility=true;
      await connectAudioNode(audio,openCompatibilityAudio(audio,source,options));
    }
    if(!compatibility) source.stop();
    node.onprocessorerror=()=>{
      pendingSet=pendingRhythm=pendingShow=undefined;
      fileBusy(false);
      resetAudio();
      $('message').textContent='Audio stopped. Tap PLAY to try again.';
    };
    node.port.postMessage({master:+$('master').value});
    $('enable').disabled=true;
    $('message').textContent=compatibility?'Compatibility audio enabled.':'Audio ready.';
  } catch(error) {
    resetAudio();
    $('message').textContent='Could not start audio: '+error.message;
  } finally {
    if(!pendingSet) fileBusy(false);
  }
}
async function play() {
  if(pendingPlay) return;
  pendingPlay=true;
  try {
    if(!node || openingAudio) await enable();
    if(!node) return;
    if(audio.state!=='running')
      await audioDeadline(audio.resume(),'Open this page in Safari and tap PLAY again.');
    send({buttons:1<<12});
  } catch(error) {
    resetAudio();
    $('message').textContent='Could not resume audio: '+error.message;
  } finally {
    pendingPlay=false;
  }
}
$('enable').onclick=enable;
$('transport').onclick=play;
$('master').oninput=()=>node?.port.postMessage({master:+$('master').value});
function acceptSet(requested) {
  board=requested.board;
  preparedSet=requested.payload;
  preparedRhythms=requested.rhythms;
  standbySong=0;
  renderRunningOrder();
  keys=0;
  for (const pad of pads) pad.setAttribute('aria-pressed','false');
  $('set-name').textContent=requested.name;
  $('message').textContent='Set loaded. Press PLAY.';
}
async function requestSet(name, read) {
  fileBusy(true);
  $('message').textContent='Loading set…';
  try {
    const {payload,rhythms}=decodeSet(await read());
    const requested={payload,rhythms,board:renderBoard(payload,rhythms),name};
    if (node) {
      pendingSet=requested;
      node.port.postMessage({loadSet:payload,loadRhythms:rhythms});
    } else {
      acceptSet(requested);
      showStandby();
    }
  } catch(error) {
    pendingSet=undefined;
    $('message').textContent=error.message+' Current set kept.';
  } finally {
    if (!pendingSet) fileBusy(false);
  }
}
const rehearsalPicker=$('rehearsal-set');
for (const [id,rehearsal] of Object.entries(rehearsalSets))
  rehearsalPicker.add(new Option(rehearsal.name, id));
rehearsalPicker.onchange=()=>{
  const choice=rehearsalSets[rehearsalPicker.value];
  rehearsalPicker.value='';
  if (choice && !rehearsalPicker.disabled)
    requestSet(choice.name,()=>choice.set);
};
$('open-set').onchange=event=>{
  const input=event.currentTarget, file=input.files[0];
  input.value='';
  if (!file || input.disabled) return;
  requestSet(file.name,async()=>{
    if (file.size>65536) throw Error('The set file is too large.');
    const text=await file.text();
    return text.trimStart().startsWith('compas-song/1')?textToShow(text):JSON.parse(text);
  });
};
function requestRhythm(action,payload,name) {
  if(!node) { $('message').textContent='Enable audio before saving or opening a rhythm.';return; }
  if(pendingSet || pendingRhythm || pendingShow) return;
  pendingRhythm={name};
  $('message').textContent=action==='export'?'Preparing rhythm file…':'Opening rhythm…';
  fileBusy(true);
  node.port.postMessage({rhythmAction:action,rhythmPayload:payload});
}
let showAsText=false;
$('save-show-text').onclick=()=>{ if(pendingShow) return; showAsText=true; $('save-show').onclick(); if(!pendingShow) showAsText=false; };
$('save-show').onclick=()=>{
  if(!node) { $('message').textContent='Enable audio before saving a show.';return; }
  if(pendingSet || pendingRhythm || pendingShow) return;
  pendingShow=true;fileBusy(true);$('message').textContent='Preparing show file…';
  node.port.postMessage({saveShow:true});
};
$('save-rhythm').onclick=()=>requestRhythm('export');
$('open-rhythm').onchange=async event=>{
  const input=event.currentTarget,file=input.files[0];input.value='';
  if(!file || input.disabled) return;
  try {
    if(file.size!==140) throw Error('This is not a Compás 727 rhythm file.');
    const payload=new Uint8Array(await file.arrayBuffer());
    requestRhythm('import',payload,file.name);
  } catch(error) { $('message').textContent=error.message+' Current pattern kept.'; }
};
function send(fields={}) {
  if (!openingAudio && !pendingSet) node?.port.postMessage({buttons:0,keys,time:time(),...fields});
}
function button(label,handler) {
  const el=document.createElement('button');
  el.textContent=label;
  el.type='button';
  if(handler) el.onclick=handler;
  return el;
}
const songCues=$('song-cues');
let cueButtons=[], displayedSong, displayedNext;
function cueSong(slot) {
  if(openingAudio || pendingSet || pendingRhythm || pendingShow) return;
  if(node) {
    node.port.postMessage({cueSong:slot});
  } else if(!board.bench_cue(slot)) {
    standbySong=slot;
    board.bench_audio();
    board.bench_read_view();
    showStandby();
  }
}
function renderRunningOrder() {
  const songs=[];
  cueButtons=[];
  for(let slot=0;slot<showSlots;++slot) {
    const title=board.bench_song_title(slot);
    if(!title) continue;
    const item=document.createElement('li');
    const cue=button(String(slot+1).padStart(2,'0')+'  '+decodeSongTitle(new Uint8Array(board.memory.buffer,title,18)),()=>cueSong(slot));
    const badge=document.createElement('span');
    badge.className='cue-state';
    cue.append(badge);
    item.append(cue);
    songs.push(item);
    cueButtons.push({slot,cue,badge});
  }
  songCues.replaceChildren(...songs);
  displayedSong=displayedNext=undefined;
}
function showCueStatus(song,next) {
  if(song===displayedSong && next===displayedNext) return;
  for(const {slot,cue,badge} of cueButtons) {
    if(slot===song) cue.setAttribute('aria-current','true');
    else cue.removeAttribute('aria-current');
    cue.dataset.next=String(slot===next);
    badge.textContent=slot===song?'NOW':slot===next?'NEXT':'';
  }
  displayedSong=song;
  displayedNext=next;
}
const orderInput=$('order-show'), orderList=$('show-order-list'), orderExport=$('export-show-order');
const orderMessage=text=>$('show-order-status').textContent=text;
let showDraft;
function renderShowOrder() {
  const {payload,order}=showDraft;
  orderList.replaceChildren(...order.map((slot,index)=>{
    const item=document.createElement('li');
    const title=showSongTitle(payload,slot);
    item.append(title+' ', ...[-1,1].map(delta=>{
      const target=index+delta;
      const label=delta<0?'Move up':'Move down';
      const move=button(label,()=>{
        [order[index],order[target]]=[order[target],order[index]];
        renderShowOrder();
        const controls=orderList.children[target].querySelectorAll('button');
        const preferred=controls[delta<0?0:1];
        (preferred.disabled ? controls[delta<0?1:0] : preferred).focus();
        orderMessage(title+' moved to song '+(target+1)+'.');
      });
      move.setAttribute('aria-label',label+': '+title);
      move.disabled=target<0 || target>=order.length;
      return move;
    }));
    return item;
  }));
}
orderInput.onchange=async()=>{
  const file=orderInput.files[0];
  orderInput.value='';
  if(!file || orderInput.disabled) return;
  orderInput.disabled=true;
  try {
    if(file.size>65536) throw Error('Show file is too large.');
    const {payload,rhythms}=showPayload(JSON.parse(await file.text()));
    const order=showSongSlots(payload);
    renderBoard(payload,rhythms);
    showDraft={payload,rhythms,order,name:file.name};
    renderShowOrder();
    orderExport.disabled=false;
    orderMessage('Opened '+order.length+' songs from '+file.name+'.');
  } catch(error) {
    orderMessage(error.message+(showDraft?' Current draft kept.':''));
  } finally {
    orderInput.disabled=false;
  }
};
orderExport.onclick=()=>{
  if(!showDraft) return;
  const result=reorderShow(showDraft,showDraft.order);
  const blob=new Blob([JSON.stringify(result)],{type:'application/json'});
  const url=URL.createObjectURL(blob), link=document.createElement('a');
  link.href=url;link.download=showDraft.name.replace(/\.cpshow$/i,'')+'-reordered.cpshow';
  link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  orderMessage('Exported '+showDraft.order.length+' songs with their saved settings.');
};

const encoders=['SELECT','ALGORITHM','KNOB1','KNOB2','KNOB3','KNOB4','PRESETS'];
for(const [encoder,label] of encoders.entries()) {
  const el=document.createElement('div'), title=document.createElement('div');
  el.className='control'; title.textContent=label;
  el.append(title,...[-1,1].map(delta=>{
    const b=button(delta<0?'−':'+',()=>send({encoder,delta}));
    b.setAttribute('aria-label',label+(delta<0?' down':' up'));
    b.onwheel=event=>{event.preventDefault();send({encoder,delta:event.deltaY<0?1:-1});};
    return b;
  }));
  $('encoders').append(el);
}
const controls=['OCT−','OCT+','FX','SCL','ENV','LFO','EDIT','GLO','HOME','SAVE','ARP','SEQ','PLAY','REC'];
for(const [i,label] of controls.entries()) {
  const press=()=>send({buttons:1<<i});
  const control=button(label,label==='PLAY'?play:press);
  if(label==='ARP') {
    control.id='fill-control';
    control.title='Band: ARP queues a fill. 727 pads: ARP toggles Roll and Algorithm sets its rate.';
    control.onpointerdown=event=>{
      if(event.button===0) press();
    };
    control.onkeydown=event=>{
      if(event.key==='Enter') {
        event.preventDefault();
        if(!event.repeat) press();
      }
    };
    control.onclick=event=>{
      if(event.detail===0) press();
    };
  }
  if(label==='REC') control.title='Open the 727 pads. Press again on the pads to arm or disarm recording.';
  $('buttons').append(control);
}
const keyLabels=computerKeys;
const white=new Set(whiteKeys);
const pads=[];
function key(i,down) {
  const bit=1<<i;
  if(Boolean(keys&bit)===down) return;
  keys=down?keys|bit:keys&~bit;
  pads[i].setAttribute('aria-pressed',String(down));
  send();
}
for(const [i,label] of keyLabels.entries()) {
  const b=button(label);
  b.className=white.has(i)?'white':'black';
  const before=whiteKeys.filter(n=>n<i).length;
  b.style.left=(white.has(i)?before*6.25:before*6.25-2)+'%';
  b.innerHTML='<span class=job></span><kbd></kbd>';
  b.querySelector('kbd').textContent=label;
  b.setAttribute('aria-pressed','false');
  b.onpointerdown=e=>{e.preventDefault();b.setPointerCapture(e.pointerId);key(i,true);};
  b.onlostpointercapture=()=>key(i,false);
  b.onpointerup=()=>key(i,false);
  b.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();key(i,true);}};
  b.onkeyup=e=>{if(e.key==='Enter'){e.preventDefault();key(i,false);}};
  $('keys').append(b);pads.push(b);
}
for(const type of ['keydown','keyup']) window.addEventListener(type,event=>{
  if(event.repeat || event.ctrlKey || event.metaKey || event.altKey) return;
  const i=keyLabels.indexOf(event.key.toUpperCase());
  if(['INPUT','SELECT','TEXTAREA'].includes(event.target.tagName)) {
    if(type==='keyup' && i>=0) key(i,false);
    return;
  }
  if(i>=0) {event.preventDefault();key(i,type==='keydown');}
  if(event.key===' ' && event.target.tagName!=='BUTTON') {
    event.preventDefault();
    if(type==='keydown') play();
  }
});
window.addEventListener('blur',()=>{for(let i=0;i<27;i++) key(i,false);});

const keyScroll=$('key-scroll');
let keyLayout, keyBank='left';
function showKeyBank(bank) {
  keyBank=bank;
  const left=pads[partKeys[0]], right=pads[partKeys.at(-1)];
  const center=(left.offsetLeft+right.offsetLeft+right.offsetWidth-keyScroll.clientWidth)/2;
  keyScroll.scrollLeft=bank==='center'?center:bank==='right'?keyScroll.scrollWidth:0;
}
for(const bank of ['left','center','right']) $('keys-'+bank).onclick=()=>showKeyBank(bank);
window.matchMedia('(max-width:727px)').addEventListener('change',()=>showKeyBank(keyBank));

function updateKeyLayout(snapshot,state) {
  const conductor=state.keys.some(key=>key.kind==='part');
  const layout=conductor?'conductor':snapshot[0]+':'+snapshot[18];
  if(layout===keyLayout) return;
  keyLayout=layout;
  $('keys-left').textContent=conductor?'Drums left':'Left keys';
  $('keys-center').textContent=conductor?'Song parts':'Center keys';
  $('keys-right').textContent=conductor?'Drums right':'Right keys';
  showKeyBank(conductor?'center':'left');
}

function updateKeys() {
  const snapshot=new Uint8Array(board.memory.buffer,board.bench_snapshot(),board.bench_snapshot_size());
  const state=keyPresentation(snapshot,setCatalog);
  showCueStatus(state.song,state.nextSong);
  const playing=Boolean(snapshot[20]);
  $('transport').textContent=playing?'STOP':'PLAY';
  $('transport').dataset.playing=String(playing);
  $('key-hint').textContent=state.hint;
  updateKeyLayout(snapshot,state);
  state.keys.forEach((k,i)=>{
    const pad=pads[i];
    pad.querySelector('.job').textContent=k.label;
    pad.dataset.kind=k.kind;
    pad.dataset.current=String(k.current);pad.dataset.queued=String(k.next);pad.dataset.on=String(k.on);
    pad.style.setProperty('--family',k.color);
    pad.title=k.name+(k.state?' · '+k.state:'')+' · computer key '+k.shortcut;
    pad.setAttribute('aria-label',pad.title);
    pad.setAttribute('aria-disabled',String(k.kind==='unused'));
  });
}
padsReady=true;
renderRunningOrder();
updateKeys();
$('transport').disabled=false;
$('enable').disabled=false;
$('message').textContent='Press PLAY to start. Enable audio separately for finger drumming.';
