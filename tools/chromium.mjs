/* Run a muted Chromium smoke test over DevTools pipes (no debugger TCP port). */
import assert from 'node:assert/strict';
import {showPayload,serializeShow} from '../web/show-format.mjs';
import {spawn} from 'node:child_process';
import {mkdtemp,readFile,rm,writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {setTimeout as sleep} from 'node:timers/promises';

const browser=process.env.COMP_CHROMIUM || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const url=process.argv[2] || 'http://127.0.0.1:8765/web/';
const retryCheck=new URL(url).searchParams.has('audio-retry-check');
const compatibilityCheck=new URL(url).searchParams.has('audio-compatibility-check');
const touchCheck=retryCheck || compatibilityCheck;
const layoutCheck=new URL(url).searchParams.has('touch-layout-check');
const rackCheck=new URL(url).searchParams.has('rack-live-check');
const profile=await mkdtemp(join(tmpdir(),'compas-chromium-'));
const proc=spawn(browser,['--headless=new','--mute-audio','--no-first-run','--disable-background-networking',
  '--disable-component-update','--disable-sync','--remote-debugging-pipe',
  ...touchCheck||layoutCheck?[]:['--autoplay-policy=no-user-gesture-required'],
  '--user-data-dir='+profile,'about:blank'],{stdio:['ignore','ignore','ignore','pipe','pipe']});
let id=0, buffer='', session;
const pending=new Map(), errors=[];
proc.stdio[4].setEncoding('utf8');
proc.stdio[4].on('data',data=>{
  buffer+=data;
  for(let n;(n=buffer.indexOf('\0'))>=0;){
    const msg=JSON.parse(buffer.slice(0,n));buffer=buffer.slice(n+1);
    if(msg.id) {
      const task=pending.get(msg.id);
      if(task){pending.delete(msg.id);clearTimeout(task.timer);msg.error?task.reject(Error(msg.error.message)):task.resolve(msg.result);}
    } else if(msg.method==='Runtime.exceptionThrown') errors.push(msg.params.exceptionDetails.text+' '+msg.params.exceptionDetails.exception?.description);
  }
});
proc.stdio[3].on('error',()=>{});
function send(method,params={},tab=session){
  return new Promise((resolve,reject)=>{
    const request=++id;
    const timer=setTimeout(()=>{pending.delete(request);reject(Error('CDP timeout '+method));},8000);
    pending.set(request,{resolve,reject,timer});
    proc.stdio[3].write(JSON.stringify({id:request,method,params,sessionId:tab})+'\0');
  });
}
async function evaluate(expression) {
  const v=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true,userGesture:true});
  if(v.exceptionDetails)throw Error(v.exceptionDetails.exception?.description||v.exceptionDetails.text);
  return v.result.value;
}
async function until(expression, label, timeout=6500) {
  const expires=Date.now()+timeout;
  do {if(await evaluate(expression))return;await sleep(35);}while(Date.now()<expires);
  assert.fail(label+'; UI: '+await evaluate('document.body.innerText'));
}
const click=selector=>evaluate('document.querySelector('+JSON.stringify(selector)+').click()');
async function tapControl(selector) {
  const point=await evaluate(`(()=>{
    const el=document.querySelector(${JSON.stringify(selector)}), r=el.getBoundingClientRect();
    const x=Math.round(r.x+r.width/2), y=Math.round(r.y+r.height/2);
    return el.contains(document.elementFromPoint(x,y))?{x,y}:null;
  })()`);
  assert(point,selector+' is not reachable at the touch point');
  await send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[point]});
  await send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
}

const set=(selector,value)=>evaluate(`(()=>{const el=document.querySelector(${JSON.stringify(selector)});
  el.value=${JSON.stringify(value)};el.dispatchEvent(new Event('change'));})()`);
const content=(id,substring)=>`document.querySelector('#${id}')?.textContent.includes(${JSON.stringify(substring)})`;
try {
  const {targetId}=await send('Target.createTarget',{url:'about:blank'});
  ({sessionId:session}=await send('Target.attachToTarget',{targetId,flatten:true}));
  await send('Runtime.enable');await send('Page.enable');
  await send('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
  await send('Page.addScriptToEvaluateOnNewDocument',{source:`const connect=AudioNode.prototype.connect;
    AudioNode.prototype.connect=function(target){
      if(this instanceof AudioWorkletNode || this instanceof ScriptProcessorNode){
        window.tap=this.context.createAnalyser();connect.call(this,window.tap);
        this.port.addEventListener('message',e=>{if(e.data.savedSet) window.savedBrowserSet=e.data.savedSet;
          if(e.data instanceof Uint8Array && e.data.length===128) {
            if (${rackCheck} && window.boardSnapshot && e.data[127]!==boardSnapshot[127]) {
              const rack=new DataView(e.data.buffer,e.data.byteOffset+120,8);
              (window.rackEvents??=[]).push({hits:rack.getUint16(0,true),manual:rack.getUint16(4,true)});
            }
            window.boardSnapshot=e.data;
          }});
      }
      return connect.call(this,target);
    };
    window.energy=()=>{const x=new Float32Array(tap.fftSize);tap.getFloatTimeDomainData(x);return x.reduce((a,n)=>a+n*n,0);};`});
  if(touchCheck) {
    await send('Page.addScriptToEvaluateOnNewDocument',{source:`
      window.workletRequests=[];
      const addModule=Worklet.prototype.addModule;
      Worklet.prototype.addModule=function(url,options) {
        workletRequests.push(new URL(url).protocol);
        if(${compatibilityCheck} || !String(url).startsWith('data:')) return Promise.reject(Error('Worklet request denied'));
        return addModule.call(this,url,options);
      };
      const resume=AudioContext.prototype.resume;
      let requests=0;
      AudioContext.prototype.resume=function() {
        if(${retryCheck} && ++requests===1) return new Promise(()=>{});
        return resume.call(this);
      };
      document.addEventListener('click',event=>{
        if(event.target.id==='transport') window.trustedPlay=event.isTrusted;
      });
    `});
  }
  await send('Page.navigate',{url});
  const rehearsal=new URL(url).hash.slice(1);
  if (touchCheck && !rackCheck) {
    await until('document.querySelectorAll("#keys button").length===27','board did not load');
    const tapPlay=()=>tapControl('#transport');
    await tapPlay();
    if (retryCheck) {
      await until(content('message','did not enable audio'),'the stalled first start did not time out');
      assert.equal(await evaluate('trustedPlay && !document.querySelector("#enable").disabled'),true);
      await tapPlay();
    }
    await until('window.boardSnapshot?.[20] && energy()>0.01','trusted touch did not produce PCM');
    await until(content('transport','STOP'),'transport did not acknowledge Play');
    assert.equal(await evaluate('trustedPlay'),true);
    const requests=['http:','data:'];
    assert.deepEqual(await evaluate('workletRequests'),retryCheck?[...requests,...requests]:requests);
    if (compatibilityCheck)
      assert.equal(await evaluate(content('message','Compatibility audio enabled')),true);
    await tapPlay();
    await until('!boardSnapshot[20] && energy()===0','stop did not clear PCM');
    await tapPlay();
    await until('boardSnapshot[20] && energy()>0.01','transport did not restart');
    await tapPlay();
    await until('!boardSnapshot[20] && energy()===0','restart did not stop silently');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      trustedTouch:true,stalledResumeRecovered:retryCheck,compatibilityAudio:compatibilityCheck,
      deniedWorkletRecovered:true,playingPCM:true,silentStop:true,restart:true}));
  } else if (layoutCheck) {
    await send('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:2});
    const key=i=>`#keys button:nth-child(${i+1})`;
    const visible=selectors=>evaluate(`(()=>{
      const pane=document.querySelector('#key-scroll').getBoundingClientRect();
      return ${JSON.stringify(selectors)}.every(s=>{
        const r=document.querySelector(s).getBoundingClientRect();
        return r.left>=pane.left-1 && r.right<=pane.right+1 && r.bottom<=innerHeight;
      });
    })()`);
    const allParts=[7,9,11,12,14,16].map(key);
    await until(content('keys-center','Song parts'),'conductor keyboard did not load');
    assert(await visible(allParts),'all six song parts must fit above the fold');
    assert(await evaluate(`document.querySelector('#key-scroll').getBoundingClientRect().bottom<
      document.querySelector('#encoders').getBoundingClientRect().top`));
    assert.equal(await evaluate('document.documentElement.scrollWidth'),390);

    await tapControl('#keys-left');
    assert(await visible([key(0),key(6)]),'left drums are unreachable');
    await tapControl('#keys-right');
    assert(await visible([key(18),key(26)]),'right drums are unreachable');
    await tapControl('#keys-center');
    assert(await visible(allParts),'the center button did not restore the parts');

    await tapControl('#transport');
    await until('window.boardSnapshot?.[20] && energy()>0.01','Play produced no signal');
    await tapControl(key(12));
    await until(`document.querySelector('${key(12)}').dataset.queued==='true'`,'touch did not cue Chorus');
    if(process.env.COMP_SCREENSHOT) {
      const {data}=await send('Page.captureScreenshot',{format:'png'});
      await writeFile(process.env.COMP_SCREENSHOT+'-touch-conductor.png',Buffer.from(data,'base64'));
    }
    await until(`document.querySelector('${key(12)}').dataset.current==='true' && boardSnapshot[20]`,
      'Chorus did not land while playing',8500);

    const panel=label=>evaluate(`[...document.querySelectorAll('#buttons button')]
      .find(button=>button.textContent===${JSON.stringify(label)}).click()`);
    await panel('REC');
    await until(content('keys-center','Center keys'),'rack retained conductor controls');
    assert.equal(await evaluate("document.querySelector('#key-scroll').scrollLeft"),0);
    assert(await visible([key(0)]),'the rack must start at its first sound');
    await panel('SEQ');
    await until(content('key-hint','KNOB1 chooses the sound'),'727 step editor did not open');
    assert(await visible([key(0)]),'the editor must start at step 1');
    await panel('HOME');
    await until(content('keys-center','Song parts'),'HOME did not return to the song parts');
    assert(await visible(allParts));
    await tapControl('#transport');
    await until('!boardSnapshot[20] && energy()===0','Stop did not clear output');

    await send('Emulation.setDeviceMetricsOverride',{width:1024,height:900,deviceScaleFactor:1,mobile:false});
    await until("getComputedStyle(document.querySelector('.key-bank')).display==='none'",'desktop shows mobile controls');
    assert(await visible([key(0),key(26)]),'desktop did not show the entire keybed');
    await send('Emulation.setDeviceMetricsOverride',{width:320,height:780,deviceScaleFactor:1,mobile:true});
    await until("document.querySelector('#key-scroll').scrollLeft>0",'narrow viewport did not re-center');
    assert(await visible(allParts),'song parts do not fit on a 320px viewport');
    assert.equal(await evaluate('document.documentElement.scrollWidth'),320);
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      mobileWidths:[320,390],desktopWidth:1024,conductorInView:true,outerBanks:true,
      touchChorus:true,rackAtFirstSound:true,stepsAtStepOne:true,homeRecenters:true,silentStop:true}));
  } else if (rackCheck) {
    const openShow=(name,data)=>evaluate(`(()=>{
      const transfer=new DataTransfer();
      transfer.items.add(new File([${JSON.stringify(JSON.stringify(data))}],${JSON.stringify(name)}));
      const input=document.querySelector('#open-set');
      input.files=transfer.files;
      input.dispatchEvent(new Event('change'));
    })()`);
    const press=label=>evaluate(`[...document.querySelectorAll('#buttons button')].find(button=>button.textContent===${JSON.stringify(label)}).click()`);
    const padKey=type=>send('Input.dispatchKeyEvent',{type,key:'v',code:'KeyV'});
    const hits='new DataView(boardSnapshot.buffer,boardSnapshot.byteOffset).getUint32(80,true)';
    await until('document.querySelectorAll("#keys button").length===27','board did not load');
    await openShow('Live Rack.json',{format:1,songs:[{title:'LIVE RACK',study:'mx',bpm:120,feel:0}]});
    await until(content('set-name','Live Rack.json'),'blank rehearsal did not load');
    await tapControl('#transport');
    await until('window.boardSnapshot?.[20] && energy()>0.01','Play produced no signal');
    if (compatibilityCheck) {
      assert.equal(await evaluate('trustedPlay'),true);
      assert.equal(await evaluate(content('message','Compatibility audio enabled')),true);
    }
    await press('REC');
    await until('boardSnapshot[0]===3 && !boardSnapshot[18]','REC did not open the pads');
    await press('ARP');
    await until('(boardSnapshot[56]&7)===3','ARP did not enable sixteenth-note Roll');
    await click('[aria-label="ALGORITHM down"]');
    await until('(boardSnapshot[56]&7)===2','ALGORITHM did not select triplet Roll');

    await padKey('keyDown');
    await until('(window.rackEvents||[]).filter(event=>event.manual===8).length>=4',
      'held open conga did not repeat on the worklet clock');
    await padKey('keyUp');
    await sleep(240); // Allow a repeat already in flight to reach the display.
    const released=await evaluate('rackEvents.filter(event=>event.manual===8).length');
    await sleep(500);
    assert.equal(await evaluate('rackEvents.filter(event=>event.manual===8).length'),released,
      'Roll kept triggering after key release');
    await press('ARP');
    await until('(boardSnapshot[56]&7)===0','ARP did not leave Roll');
    await press('REC');
    await until('(boardSnapshot[56]&8) && boardSnapshot[89]===3','REC did not arm the open conga lane');
    await padKey('keyDown');
    await until(hits+'!==0','played hit did not reach the recorded step grid');
    await padKey('keyUp');
    const recorded=await evaluate(hits);
    assert.equal(recorded&(recorded-1),0,'single pad hit wrote multiple steps');
    await until('rackEvents.some(event=>event.hits===8 && event.manual===0)',
      'recorded open conga did not play back on the next pass');
    await click('#transport');
    await until('!boardSnapshot[20] && !(boardSnapshot[56]&8) && energy()===0',
      'STOP did not silence the set and disarm recording');

    await evaluate(`(()=>{const create=URL.createObjectURL;
      URL.createObjectURL=blob=>{if(blob.type==='application/json') window.showFile=blob;return create(blob);};
    })()`);
    await click('#save-show');
    await until(content('message','Show saved:'),'recorded show could not export');
    const saved=await evaluate('showFile.text().then(JSON.parse)');
    const {rhythms}=showPayload(saved);
    assert.equal(new DataView(rhythms.buffer,rhythms.byteOffset).getUint32(16+3*8,true),recorded);
    await openShow('Live Rack.cpshow',saved);
    await until(content('set-name','Live Rack.cpshow')+' && boardSnapshot[89]===3 && '+hits+'==='+recorded,
      'reopening the show lost the recorded lane');
    await evaluate('rackEvents=[]');
    await click('#transport');
    await until('boardSnapshot[20] && rackEvents.some(event=>event.hits===8 && event.manual===0)',
      'restored lane did not play');
    await click('#transport');
    await until('!boardSnapshot[20] && energy()===0','restored show did not stop silently');
    assert.deepEqual(errors,[]);
    if(process.env.COMP_SHOW_EXPORT)
      await writeFile(process.env.COMP_SHOW_EXPORT,JSON.stringify(saved),{flag:'wx'});
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      compatibilityAudio:compatibilityCheck,rollRate:'eighth-note triplets',heldPadRepeats:released,releaseStopsRoll:true,
      recordedVoice:'open conga',recordedGrid:recorded,nextPassPlayback:true,
      portableShowRoundTrip:true,restoredPlayback:true,silentStop:true}));
  } else if (new URL(url).searchParams.has('cue-check')) {
    const row=index=>'#song-cues li:nth-child('+index+') button';
    const badge=(index,label)=>`document.querySelector('${row(index)} .cue-state')?.textContent==='${label}'`;
    await until(badge(1,'NOW'),'running order did not mark the loaded song');
    await click('#running-order summary');
    await until(`document.querySelector('${row(5)}').textContent.includes('BOLERO CANCIÓN')`,'accented title was lost');
    await click(row(3));
    await until(badge(3,'NOW'),'standby song did not change');
    await click('#transport');
    await until('window.boardSnapshot?.[20] && boardSnapshot[48]===2 && energy()>0.01','PLAY lost the standby selection');
    await click(row(5));
    await until(badge(5,'NEXT')+' && boardSnapshot[48]===2','live cue did not wait at the phrase');
    await click(row(2));
    await until(badge(2,'NEXT')+' && '+badge(5,''),'new cue did not replace the old cue');
    await click(row(3));
    await until(badge(2,'')+' && boardSnapshot[49]===16','tapping NOW did not cancel the cue');
    await click(row(1));
    await until(badge(1,'NEXT'),'the final cue was not visible');
    await until(badge(1,'NOW')+' && boardSnapshot[20] && energy()>0.01','the cue did not land with playback running',8500);
    await click('#transport');
    await until('!boardSnapshot[20] && energy()===0','STOP did not silence the new song');

    const {payload,rhythms}=showPayload(JSON.parse(await readFile(new URL('../build/Colombian-Cumbia-727.cpshow',import.meta.url))));
    for(const [bytes,size] of [[payload,85],[rhythms,151]]) {
      bytes.copyWithin(9*size,2*size,3*size);
      bytes.copyWithin(2*size,15*size,16*size);
    }
    const sparse=JSON.stringify(serializeShow(payload,rhythms));
    await evaluate(`(()=>{
      const transfer=new DataTransfer();
      transfer.items.add(new File([${JSON.stringify(sparse)}],'Sparse.cpshow'));
      const input=document.querySelector('#open-set');
      input.files=transfer.files;
      input.dispatchEvent(new Event('change'));
    })()`);
    await until(content('set-name','Sparse.cpshow')+' && '+badge(1,'NOW'),'sparse show did not open');
    assert.equal(await evaluate("document.querySelectorAll('#song-cues li').length"),3);
    assert.match(await evaluate(`document.querySelector('${row(3)}').textContent`),/^10 +SOLEDAD/);
    await click(row(3));
    await until(badge(3,'NOW')+' && boardSnapshot[48]===9 && boardSnapshot[88]===2 && boardSnapshot[90]===21','sparse slot lost its attached rhythm');
    const press=label=>evaluate(`[...document.querySelectorAll('#buttons button')].find(button=>button.textContent===${JSON.stringify(label)}).click()`);
    await press('GLO');
    await until("document.querySelector('#keys button:nth-child(10)')?.dataset.current==='true' && "+
      "document.querySelector('#keys button:nth-child(3)')?.dataset.kind==='unused'",'GLO key lights did not reflect the sparse show');
    await press('OCT+');
    await press('SAVE');
    await until(`document.querySelector('${row(4)}')?.textContent.startsWith('11  SOLEDAD') && `+badge(4,'NOW'),
      'running order did not refresh after saving a new slot');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,standbySong:true,
      liveCue:true,newestCueWins:true,cancelCue:true,silentStop:true,sparseImportedShow:true,attachedRhythm:true,savedSlotRefresh:true}));
  } else if (new URL(url).searchParams.has('opening-check')) {
    await send('Page.addScriptToEvaluateOnNewDocument',{source:`
      window.openingFrames=[];
      const put=CanvasRenderingContext2D.prototype.putImageData;
      CanvasRenderingContext2D.prototype.putImageData=function(frame,...args) {
        if(this.canvas.id==='screen' && openingFrames.length<64) {
          const pixel=(x,y)=>Array.from(frame.data.slice(4*(240*y+x),4*(240*y+x)+3)).join(',');
          openingFrames.push({at:performance.now(),splash:pixel(1,1)==='255,45,140' &&
            pixel(81,1)==='255,231,0',cameo:pixel(205,53)==='255,45,140'});
        }
        return put.call(this,frame,...args);
      };
    `});
    for(const bookmark of ['', '#tribute']) {
      const address=new URL(url);
      address.hash=bookmark;
      await send('Page.navigate',{url:'about:blank'});
      await send('Page.navigate',{url:address.href});
      await until('window.openingFrames?.some(frame=>frame.cameo)','standby cameo did not appear');
      await until('openingFrames.some(frame=>frame.cameo) && !openingFrames.at(-1).cameo',
        'cameo did not expire before audio was enabled');
      const frames=await evaluate('openingFrames');
      assert(frames[0].splash);
      const cameo=frames.find(frame=>frame.cameo);
      assert(cameo.at-frames[0].at>=600 && cameo.at-frames[0].at<1100);
      assert(frames.at(-1).at-cameo.at>=890 && frames.at(-1).at-cameo.at<1400);
      assert.equal(await evaluate('window.tap===undefined && !document.querySelector("#enable").disabled'),true);
    }
    const address=new URL(url);
    address.hash='#tribute';
    await send('Page.navigate',{url:'about:blank'});
    await send('Page.navigate',{url:address.href});
    await until('window.openingFrames?.[0]?.splash','opening did not draw before early audio enable');
    await click('#enable');
    await until('window.boardSnapshot?.[48]===0 && !openingFrames.at(-1).splash','early audio did not open the panel');
    assert(await evaluate('openingFrames.find(frame=>!frame.splash).at-openingFrames[0].at<650'));
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      timedFactorySplash:true,timedTributeSplash:true,idleCameoExpiry:true,earlyAudioEnable:true}));
  } else if (new URL(url).pathname.endsWith('/Start%20Compas.html')) {
    const links={
      '#tribute':'Johnny Canales tribute','#regional':'ten original studies','#san-antonio':'San Antonio',
      '#colombian':'traditional percussion','#colombian-727':'727 percussion show','#merengue':'Derecho, Maco and Juana Quilina','#siboney':'1929 Feist voice and piano',
      '#son-salsa':'727 clave comparison','#zacatecas':'1895 Levy piano print','#la-mora':'1918 Havana piano excerpts',
      '#la-mora-finale':'singer to finale','#merenhouse':'New York rhythm study','#jarocho-bamba':'Veracruz accompaniment',
      '#valentina':'Apke guitar edition','#adelita':'Kemp quartet rehearsal','#cielito':'Cielito Lindo','#cucaracha':'La Cucaracha',
      '#golondrina':'La Golondrina','#ma-teodora':'1929 compound-meter rehearsal',
      '#tejano-rhythm':'Tejano rhythm section','#bolero-trova':'Cuban bolero','#jurame':'Schirmer song',
      '#sobre-las-olas':'1895 first strain to Fine',
      '':'Factory set',
    };
    for(const [fragment,name] of Object.entries(links)) {
      const link=`a[href="Compas-Sonidero-Board.html${fragment}"]`;
      await until(`!!document.querySelector(${JSON.stringify(link)})`,'rehearsal link missing');
      await click(link);
      await until(content('set-name',name),'wrong set from rehearsal link '+fragment);
      await send('Page.navigate',{url});
    }
    await send('Page.navigate',{url:new URL('Compas-Sonidero-Board.html#__proto__',url).href});
    await until(content('set-name','Factory set'),'unknown bookmark did not use the factory set');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      rehearsalLinks:Object.keys(links).length,unknownBookmarkFallback:true}));
  } else if (url.includes('Compas-Regional-Cumbia.html') || rehearsal==='regional') {
    await until(content('set-name','ten original studies'),'regional set did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','regional AudioWorklet did not enable');
    await evaluate(`(()=>{const el=document.querySelector('#master');el.value='74';
      el.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    const stopped=study=>until(`window.boardSnapshot?.[21]===${study} && !boardSnapshot[20]`,'wrong stopped study');
    const playing=study=>until(`window.boardSnapshot?.[21]===${study} && boardSnapshot[20] && energy()>0.01`,'wrong or silent playing study');
    await play();
    await playing(10);
    await play();
    await stopped(10);
    for(const study of [11,9,12,13,14,16]){
      await click('[aria-label="PRESETS up"]');
      await stopped(study);
    }
    await play();
    await playing(16);
    for(const study of [17,18,19]){
      await click('[aria-label="PRESETS up"]');
      await playing(study);
    }
    await play();
    await until('energy()===0','STOP left regional audio running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      regionalStartup:true,regionalStudiesPlayed:[10,16,17,18,19],liveCues:3,silentStop:true}));
  } else if (url.includes('Compas-Johnny-Canales.html') || rehearsal==='tribute') {
    await until(content('set-name','Johnny Canales tribute'),'tribute set did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{
      const master=document.querySelector('#master');
      master.value='74';
      master.dispatchEvent(new Event('input'));
    })()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    const playing=(slot,score)=>until(
      `window.boardSnapshot?.[48]===${slot} && boardSnapshot[51]===${score} && boardSnapshot[20] && energy()>0.01`,
      'expected audible slot '+(slot+1));
    await play();
    await playing(0,0);
    await click('[aria-label="PRESETS up"]');
    await playing(1,8);
    assert.equal(await evaluate('boardSnapshot[56]'),1);
    await click('[aria-label="PRESETS up"]');
    await playing(2,0);
    await play();
    await until('energy()===0','STOP left tribute audio running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      tributeStartup:true,liveTransitions:['Tejano Cumbia','La Cucaracha','Cumbia Coast'],silentStop:true}));
  } else if (rehearsal==='tejano-rhythm') {
    await until(content('set-name','Tejano rhythm section'),'Tejano rhythm rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const button=label=>evaluate(`[...document.querySelectorAll('#buttons button')].find(b=>b.textContent===${JSON.stringify(label)}).click()`);
    await button('PLAY');
    const masks=[24,0,7,32];
    for(const [slot,mask] of masks.entries()){
      if(slot){
        await click('[aria-label="PRESETS up"]');
        await until(`window.boardSnapshot?.[49]===${slot}`,'Tejano rhythm slot did not queue');
        if(slot===1) assert.deepEqual(await evaluate('Array.from(boardSnapshot.slice(66,70))'),[1,45,45,1]);
      }
      await until(`window.boardSnapshot?.[48]===${slot} && boardSnapshot[26]===105 &&
        boardSnapshot[28]===${mask} && boardSnapshot[20] && energy()>0.01`,
        'expected audible Tejano rhythm slot '+(slot+1));
      assert.deepEqual(await evaluate('Array.from(boardSnapshot.slice(66,70))'),[0,44,45,0]);
      if(slot===0){
        await button('ENV');
        await until('boardSnapshot[0]===5','Bass page did not open');
        await click('[aria-label="KNOB4 up"]');
        await click('[aria-label="KNOB1 up"]');
        await button('HOME');
        await until('boardSnapshot[0]===0','Band page did not open');
        const key=type=>evaluate(`window.dispatchEvent(new KeyboardEvent(${JSON.stringify(type)},{key:'w',bubbles:true}))`);
        await key('keydown');
        await until('boardSnapshot[66]===1 && boardSnapshot[67]===45 && boardSnapshot[69]===1','live bass and conga changes did not take effect');
        await key('keyup');
      }
    }
    await button('PLAY');
    await until('boardSnapshot[20]===0 && energy()===0','STOP left the rhythm rehearsal running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      masks,tempo:105,livePhraseCues:3,songLayerIsolation:true,silentStop:true}));
  } else if (rehearsal==='jurame' || rehearsal==='la-mora-finale') {
    const [title, bookmarkName, score, cadenceBar]=rehearsal==='jurame'
      ? ['Júrame','Schirmer song',24,84] : ['La Mora','singer to finale',25,41];
    await until(content('set-name',bookmarkName),title+' rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    await evaluate('[...document.querySelectorAll("#buttons button")].find(b=>b.textContent==="PLAY").click()');
    const settings=[[1,0],[0,0],[0,10]];
    for(const [slot,[guide,key]] of settings.entries()) {
      if(slot) await click('[aria-label="PRESETS up"]');
      await until(`window.boardSnapshot?.[48]===${slot} && boardSnapshot[51]===${score} &&
        boardSnapshot[56]===${guide} && boardSnapshot[57]===2 && (boardSnapshot[95]&15)===${key} &&
        boardSnapshot[20] && energy()>0.001`, title+' did not play slot '+(slot+1));
    }
    await evaluate(`window.dispatchEvent(new KeyboardEvent('keydown',{key:'h',bubbles:true}))`);
    await until(`boardSnapshot[20] && boardSnapshot[53]>=${cadenceBar}`,title+' did not cue the final cadence');
    await evaluate(`window.dispatchEvent(new KeyboardEvent('keyup',{key:'h',bubbles:true}))`);
    await until('boardSnapshot[20]===0 && energy()===0',title+' natural ending was not silent',13000);
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      slots:3,guideMute:true,transpose:-2,liveCues:2,silentNaturalStop:true}));
  } else if (rehearsal==='merenhouse' || rehearsal==='jarocho-bamba') {
    const [name,study,tempo,masks]=rehearsal==='merenhouse' ?
      ['New York rhythm study',27,128,[34,32,0]] :
      ['Veracruz accompaniment',28,136,[33,1,0]];
    await until(content('set-name',name),name+' rehearsal did not open');
    await click('#transport');
    for(const [slot,mute] of masks.entries()) {
      if(slot) {
        await click('[aria-label="PRESETS up"]');
        await until(`boardSnapshot[49]===${slot}`,name+' did not queue the next layer');
      }
      await until(`window.boardSnapshot?.[48]===${slot} && boardSnapshot[21]===${study} &&
        boardSnapshot[28]===${mute} && boardSnapshot[26]===${tempo} && boardSnapshot[20] && energy()>0.01`,
        'expected audible '+name+' slot '+(slot+1));
    }
    await click('#transport');
    await until('boardSnapshot[20]===0 && energy()===0',name+' STOP was not silent');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      masks,tempo,liveCues:2,silentStop:true}));
  } else if (rehearsal==='bolero-trova') {
    await until(content('set-name','trova and canción'),'Cuban bolero rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(b=>b.textContent==="PLAY").click()');
    await play();
    const slots=[[26,0],[26,8],[5,0],[26,32]];
    for(const [slot,[study,mute]] of slots.entries()) {
      if(slot) await click('[aria-label="PRESETS up"]');
      await until(`window.boardSnapshot?.[48]===${slot} && boardSnapshot[21]===${study} &&
        boardSnapshot[28]===${mute} && boardSnapshot[26]===76 && boardSnapshot[20] && energy()>0.01`,
        'expected audible Cuban bolero slot '+(slot+1));
    }
    await play();
    await until('boardSnapshot[20]===0 && energy()===0','Cuban bolero STOP was not silent');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      slots,tempo:76,liveCues:3,silentStop:true}));
  } else if (rehearsal==='golondrina') {
    await until(content('set-name','La Golondrina'),'Galveston rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const button=label=>evaluate(`[...document.querySelectorAll('#buttons button')].find(b=>b.textContent===${JSON.stringify(label)}).click()`);
    const tapKey=async index=>{
      const key=`document.querySelectorAll('#keys button')[${index}]`;
      await evaluate(`${key}.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter'}))`);
      await sleep(90);
      await evaluate(`${key}.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter'}))`);
    };
    await until(`document.querySelector('#keys [aria-label^="Bolero percussion · on"]')`,'bolero key was not shown');
    await tapKey(9);
    await button('PLAY');
    await until('window.boardSnapshot?.[20] && boardSnapshot[51]===19 && boardSnapshot[52]===2 && energy()>0.01',
      'Galveston verse was silent');
    await tapKey(13);
    await until('boardSnapshot[93]===3 && boardSnapshot[20] && energy()>0.01','Band mute interrupted the score');
    await click('[aria-label="SELECT up"]');
    await click('[aria-label="SELECT up"]');
    await until('boardSnapshot[0]===6','Percussion page did not open');
    await tapKey(13);
    await until('boardSnapshot[93]===1 && boardSnapshot[20] && energy()>0.01','Percussion page did not re-enable bolero');
    await click('[aria-label="PRESETS up"]');
    await until('boardSnapshot[48]===1 && boardSnapshot[51]===5 && boardSnapshot[20] && energy()>0.01',
      'clean piano slot was silent or did not cue');
    assert.equal(await evaluate('document.querySelectorAll("#keys button")[13].getAttribute("aria-disabled")'),'true');
    await button('PLAY');
    await until('!boardSnapshot[20] && energy()===0','STOP was not silent');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      liveBoleroMute:true,liveBoleroRestore:true,scoreContinued:true,cleanScoreKeyDisabled:true,silentStop:true}));
  } else if (rehearsal==='valentina') {
    await until(content('set-name','Apke guitar edition'),'La Valentina did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(b=>b.textContent==="PLAY").click()');
    const playing=(slot,shift)=>until(`window.boardSnapshot?.[48]===${slot} && boardSnapshot[51]===28 &&
      boardSnapshot[26]===80 && boardSnapshot[57]===2 && (boardSnapshot[95]&15)===${shift} &&
      boardSnapshot[20] && energy()>0.01`,'La Valentina slot was silent or incorrect');
    await play();
    await playing(0,0);
    await click('[aria-label="PRESETS up"]');
    await playing(1,10);
    const ending=`document.querySelectorAll('#keys button')[16]`;
    await evaluate(`${ending}.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter'}))`);
    await sleep(90);
    await evaluate(`${ending}.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter'}))`);
    await until('boardSnapshot[20] && boardSnapshot[53]===47','La Valentina ending cue did not land');
    await until('!boardSnapshot[20] && energy()===0','La Valentina cadence did not stop');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      meter:'6/8',tempo:80,liveTransposeSlot:-2,finalBar:48,silentNaturalStop:true}));
  } else if (rehearsal==='sobre-las-olas') {
    await until(content('set-name','1895 first strain to Fine'),'Sobre las olas did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(b=>b.textContent==="PLAY").click()');
    await play();
    for(const [slot,id,bpm,shift] of [[0,22,132,0],[1,0,111,0],[2,22,132,10]]) {
      if(slot) await click('[aria-label="PRESETS up"]');
      await until(`window.boardSnapshot?.[48]===${slot} && boardSnapshot[51]===${id} &&
        boardSnapshot[26]===${bpm} && (boardSnapshot[95]&15)===${shift} &&
        boardSnapshot[20] && energy()>0.01`,'expected audible Sobre las olas slot '+(slot+1));
      if(id) assert.equal(await evaluate('boardSnapshot[57]'),3);
    }
    await play();
    await until('boardSnapshot[20]===0 && energy()===0','Sobre las olas STOP was not silent');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      playedScores:[22,0,22],tempos:[132,111,132],meters:['3/4','2/4','3/4'],
      lastSlotShift:-2,liveCues:2,silentStop:true}));
  } else if (rehearsal==='son-salsa') {
    await until(content('set-name','727 clave comparison'),'son/salsa show did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const button=label=>evaluate(`[...document.querySelectorAll('#buttons button')].find(b=>b.textContent===${JSON.stringify(label)}).click()`);
    const verify=(slot,origin,clave)=>until(`window.boardSnapshot?.[48]===${slot} &&
      boardSnapshot[21]===${slot<2?6:2} && boardSnapshot[26]===${slot<2?103:108} &&
      boardSnapshot[28]===${slot<2?18:2} && (boardSnapshot[59]&1)===${clave} &&
      boardSnapshot[88]===1 && boardSnapshot[90]===${origin} && boardSnapshot[20] && energy()>0.01`,
      'son/salsa show did not recall slot '+(slot+1));
    await button('PLAY');
    await verify(0,13,0);
    await button('LFO');
    await button('OCT+');
    await until('boardSnapshot[59]>>1===1 && boardSnapshot[90]===13','linked direction did not queue');
    await verify(0,12,1);
    await button('OCT−');
    await verify(0,13,0);
    await button('HOME');
    for(const [slot,origin,clave] of [[1,12,1],[2,10,0],[3,9,1]]) {
      await click('[aria-label="PRESETS up"]');
      await until(`boardSnapshot[49]===${slot}`,'son/salsa slot did not queue');
      await verify(slot,origin,clave);
    }
    await button('PLAY');
    await until('boardSnapshot[20]===0 && energy()===0','STOP left the son/salsa show running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      sourceOrigins:[13,12,10,9],tempos:[103,103,108,108],livePhraseCues:3,
      linkedLiveClaveBothDirections:true,replacedRolesMuted:true,layerPCM:true,silentStop:true}));
  } else if (rehearsal==='colombian-727') {
    const showName='727 percussion show';
    await until(content('set-name',showName),'attached 727 show did not open before enabling audio');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    const verifySlot=(slot,bpm,playing,origin=19+slot)=>until(`window.boardSnapshot?.[48]===${slot} &&
      boardSnapshot[26]===${bpm} && boardSnapshot[88]===2 && boardSnapshot[90]===${origin} &&
      boardSnapshot[20]===${+playing} && ${playing?'energy()>0.01':'energy()===0'}`,
      '727 show did not recall slot '+(slot+1));
    await verifySlot(0,94,false);
    await play();
    for(const [slot,bpm] of [[0,94],[1,97],[2,110]]) {
      if(slot) {
        await click('[aria-label="PRESETS up"]');
        await until(`window.boardSnapshot?.[49]===${slot}`,'show cue was never pending');
      }
      await verifySlot(slot,bpm,true);
    }
    await set('#rehearsal-set','factory');
    await until(content('message','Press STOP before loading'),'live picker did not keep the show');
    await verifySlot(2,110,true);
    await play();
    await verifySlot(2,110,false);
    await evaluate(`(()=>{
      const create=URL.createObjectURL;
      URL.createObjectURL=blob=>{if(blob.type==='application/json') window.showFile=blob;return create(blob);};
    })()`);
    await click('#save-show');
    await until(content('message','Show saved:'),'the attached show could not export');
    const saved=showPayload(await evaluate('showFile.text().then(JSON.parse)'));
    assert.deepEqual([0,1,2].map(slot=>saved.rhythms[slot*151+140]),[19,20,21]);
    await set('#rehearsal-set','factory');
    await until(content('set-name','Factory set'),'factory set could not replace the stopped show');
    await set('#rehearsal-set','colombian-727');
    await until(content('set-name',showName),'show could not reopen from the picker');
    await verifySlot(0,94,false);
    const putFile=(id,name,data)=>evaluate(`(()=>{
      const dt=new DataTransfer();
      dt.items.add(new File([${JSON.stringify(JSON.stringify(data))}],${JSON.stringify(name)}));
      const input=document.getElementById(${JSON.stringify(id)});
      input.files=dt.files;
      input.dispatchEvent(new Event('change'));
    })()`);
    const sourceShow=await evaluate('showFile.text().then(JSON.parse)');
    await play();await verifySlot(0,94,true);
    await putFile('order-show','Colombian.cpshow',sourceShow);
    await until(content('show-order-status','Opened 3 songs'),'saved show did not open for reordering');
    assert.equal(await evaluate('document.querySelector("#show-order-list li").textContent.includes("BANQUEÑA")'),true);
    await click('#show-order-list li:nth-child(3) button:first-child');
    await click('#show-order-list li:nth-child(2) button:first-child');
    await verifySlot(0,94,true);
    const invalid=saved.rhythms.slice();invalid[invalid.length-1]=1;
    await putFile('order-show','bad.cpshow',serializeShow(saved.payload,invalid));
    await until(content('show-order-status','Current draft kept'),'invalid attachment replaced the draft');
    await click('#export-show-order');
    await until(content('show-order-status','Exported 3 songs'),'reordered file did not export');
    const exported=await evaluate('showFile.text().then(JSON.parse)');
    const reordered=showPayload(exported);
    for(const [source,result,size] of [[saved.payload,reordered.payload,85],[saved.rhythms,reordered.rhythms,151]]) {
      for(const [to,from] of [2,0,1].entries())
        assert.deepEqual(result.slice(to*size,(to+1)*size),source.slice(from*size,(from+1)*size));
      assert.deepEqual(result.slice(3*size),source.slice(3*size));
    }
    await play();await verifySlot(0,94,false);
    await putFile('open-set','Colombian-reordered.cpshow',exported);
    await until(content('set-name','Colombian-reordered.cpshow'),'reordered show did not reopen');
    await verifySlot(0,110,false,21);
    await play();await verifySlot(0,110,true,21);
    await click('[aria-label="PRESETS up"]');
    await verifySlot(1,94,true,19);
    await play();await verifySlot(1,94,false,19);
    if(process.env.COMP_SHOW_ORDER_EXPORT)
      await writeFile(process.env.COMP_SHOW_ORDER_EXPORT,JSON.stringify(exported),{flag:'wx'});
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      builtInShowBeforeAudio:true,stoppedShowReplacement:true,playingShowKept:true,attachedShowExport:true,
      reorderedShow:true,pairedRecordsVerified:true,badDraftKept:true,reorderedLiveCue:true,
      recalledPatternOrigins:[19,20,21],tempos:[94,97,110],livePhraseCues:2,drumsOnlyPCM:true,silentStop:true}));
  } else if (rehearsal==='colombian') {
    await until(content('set-name','traditional percussion'),'Colombian cumbia set did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    await play();
    for (const [slot,bpm] of [[0,94],[1,97],[2,110]]) {
      if(slot) await click('[aria-label="PRESETS up"]');
      await until(`window.boardSnapshot?.[48]===${slot} && boardSnapshot[21]===${23+slot} &&
        boardSnapshot[26]===${bpm} && boardSnapshot[20] && energy()>0.01`,
        'expected audible Colombian cumbia slot '+(slot+1));
    }
    await play();
    await until('energy()===0','STOP left Colombian cumbia audio running');
    const panelButton=label=>evaluate(`[...document.querySelectorAll('#buttons button')].find(b=>b.textContent===${JSON.stringify(label)}).click()`);
    await panelButton('REC');
    await panelButton('SEQ');
    for(let id=0;id<20;++id) {
      await click('[aria-label="PRESETS up"]');
      await until(`window.boardSnapshot?.[90]===${id}`,'727 selection did not advance to '+id);
    }
    await panelButton('FX');
    await play();
    await until('window.boardSnapshot?.[20] && energy()>0.01','Banqueña 727 was silent');
    for(const id of [20,21]) {
      await click('[aria-label="PRESETS up"]');
      await until(`window.boardSnapshot?.[90]===${id} && energy()>0.01`,'727 cue did not land');
    }
    await play();
    await until('energy()===0','STOP left the 727 adaptation running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      colombianCumbiaSlots:3,livePhraseCues:2,colombian727Patterns:3,liveBarCues:2,silentStop:true}));
  } else if (url.includes('Compas-Merengue.html') || rehearsal==='merengue') {
    await until(content('set-name','Juana Quilina'),'merengue rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    const playing=(slot,study,score=0)=>until(
      `window.boardSnapshot?.[48]===${slot} && boardSnapshot[21]===${study} && boardSnapshot[51]===${score} && boardSnapshot[20] && energy()>0.01`,
      'expected audible merengue slot '+(slot+1));
    await play();
    await playing(0,15);
    for(const [slot,study,score] of [[1,15],[2,8],[3,20],[4,21],[5,22],[6,5,14]]) {
      await click('[aria-label="PRESETS up"]');
      await playing(slot,study,score);
    }
    assert.equal(await evaluate(`document.querySelector('a[href="https://creativecommons.org/licenses/by-sa/3.0/"]').textContent`),'CC BY-SA 3.0');
    await play();
    await until('energy()===0','STOP left Juana Quilina audio running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      merengueSlots:7,liveCueIntoJuanaQuilina:true,attribution:true,silentStop:true}));
  } else if (url.includes('Compas-La-Adelita.html')) {
    await until(content('set-name','Kemp quartet rehearsal'),'Adelita rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    const playing=(slot,score,guide)=>until(
      `window.boardSnapshot?.[48]===${slot} && boardSnapshot[51]===${score} && boardSnapshot[56]===${guide} && boardSnapshot[20] && energy()>0.01`,
      'expected audible rehearsal slot '+(slot+1));
    await play();
    await playing(0,9,1);
    const guide=`document.querySelectorAll('#keys button')[8]`;
    await evaluate(`${guide}.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter'}))`);
    await sleep(90);
    await evaluate(`${guide}.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter'}))`);
    await playing(0,9,0);
    await click('[aria-label="PRESETS up"]');
    await playing(1,0,0);
    await click('[aria-label="PRESETS up"]');
    await playing(2,9,0);
    await play();
    await until('energy()===0','STOP left rehearsal audio running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      adelitaStartup:true,guideToggle:true,crossMeterCues:true,silentStop:true}));
  } else if (url.includes('Compas-Siboney.html') || rehearsal==='siboney') {
    await until(content('set-name','1929 Feist voice and piano'),'Siboney rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{
      const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));
    })()`);
    await evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    const playing=(slot,score,guide)=>until(
      `window.boardSnapshot?.[48]===${slot} && boardSnapshot[51]===${score} && boardSnapshot[56]===${guide} && boardSnapshot[20] && energy()>0.01`,
      'expected audible Siboney slot '+(slot+1));
    const tapKey=async index=>{
      const key=`document.querySelectorAll('#keys button')[${index}]`;
      await evaluate(`${key}.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter'}))`);
      await sleep(90);
      await evaluate(`${key}.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter'}))`);
    };
    await playing(0,17,1);
    await tapKey(8);
    await playing(0,17,0);
    for (const [slot,score,guide] of [[1,17,0],[2,18,1],[3,18,0],[4,26,1],[5,26,0],[6,27,1],[7,27,0]]) {
      await click('[aria-label="PRESETS up"]');
      await playing(slot,score,guide);
    }
    for (const [key, bar] of [[9,13],[12,41],[16,177]]) {
      await tapKey(key);
      await until(`boardSnapshot[20] && boardSnapshot[53]===${bar}`,'Siboney score cue did not land');
    }
    await until('!boardSnapshot[20] && energy()===0','Siboney cadence did not stop');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      siboneyGuideToggle:true,liveBackingCue:true,verseRefrainAndCadenceCues:true,silentNaturalStop:true}));
  } else if (url.includes('Compas-Zacatecas.html')) {
    await until(content('set-name','1895 Levy piano print'),'Zacatecas rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{
      const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));
    })()`);
    await evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    await until('window.boardSnapshot?.[51]===16 && boardSnapshot[20] && energy()>0.01',
      'Zacatecas piano did not play');
    const ending=`document.querySelectorAll('#keys button')[16]`;
    await evaluate(`${ending}.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter'}))`);
    await sleep(90);
    await evaluate(`${ending}.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter'}))`);
    await until('boardSnapshot[20] && boardSnapshot[53]===70','Zacatecas ending cue did not land');
    await until('!boardSnapshot[20] && energy()===0','Zacatecas ending did not stop');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      zacatecasStartup:true,secondEndingCue:true,silentNaturalStop:true}));
  } else if (url.includes('Compas-La-Mora.html')) {
    await until(content('set-name','1918 Havana piano excerpts'),'La Mora rehearsal did not open');
    await click('#enable');
    await until('document.querySelector("#enable").disabled','AudioWorklet did not enable');
    await evaluate(`(()=>{const master=document.querySelector('#master');
      master.value='74'; master.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    const playing=(slot,score)=>until(
      `window.boardSnapshot?.[48]===${slot} && boardSnapshot[51]===${score} && boardSnapshot[20] && energy()>0.01`,
      'expected audible La Mora slot '+(slot+1));
    await play();
    await playing(0,10);
    await click('[aria-label="PRESETS up"]');
    await playing(1,15);
    await click('[aria-label="PRESETS up"]');
    await playing(2,12);
    await click('[aria-label="PRESETS up"]');
    await playing(3,13);
    await until('boardSnapshot[56]===1','La Mora did not enable the score guide');
    const guideKey=type=>evaluate(`window.dispatchEvent(new KeyboardEvent(${JSON.stringify(type)},{key:'w',bubbles:true}))`);
    await guideKey('keydown');
    await until('boardSnapshot[56]===0 && boardSnapshot[20] && energy()>0.001','La Mora did not keep the backing after muting the guide');
    await guideKey('keyup');
    await click('[aria-label="PRESETS up"]');
    await playing(4,11);
    await play();
    await until('energy()===0','STOP left La Mora audio running');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      moraStartup:true,moraSlots:5,guideMutedWithBackingPlaying:true,liveCueBetweenScoreExcerpts:true,silentStop:true}));
  } else if (url.includes('Compas-Set-Builder.html')) {
    await until('document.querySelectorAll(".song").length===16','example set did not load');
    assert.equal(await evaluate('document.querySelector("#add").disabled'),true);
    assert.equal(await evaluate('document.querySelectorAll("#order-slots button").length'),16);
    assert.equal(await evaluate('document.querySelector("#order-slots li:nth-child(9) .order-detail").textContent'),'88 BPM · Score · stops at end');
    await click('#order-slots li:nth-child(9) button');
    assert.equal(await evaluate('document.activeElement.textContent'),'Slot 9');
    await click('.song:nth-child(9) .slot-head a');
    assert.equal(await evaluate('document.activeElement.id'),'order-title');
    const editField=(label,value,song='.song:first-child')=>evaluate(`(()=>{
      const field=[...document.querySelectorAll(${JSON.stringify(song+' label')})].find(n=>n.firstChild.textContent===${JSON.stringify(label)});
      const input=field.querySelector('input');
      input.value=${JSON.stringify(value)};
      input.dispatchEvent(new Event('input'));
    })()`);
    await editField('Panel name','soundcheck');
    await editField('Tempo','115');
    await editField('Phrases (0 = hold)','1');
    assert.equal(await evaluate('document.querySelector("#order-slots li:first-child .order-title").textContent'),'01 · SOUNDCHECK');
    assert.equal(await evaluate('document.querySelector("#order-slots li:first-child .order-detail").textContent'),'115 BPM · Next after 1 phrase');
    await editField('Panel name','CUMBIA COAST');
    await editField('Tempo','94');
    await editField('Phrases (0 = hold)','0');
    await editField('Phrases (0 = hold)','2','.song:last-child');
    assert.equal(await evaluate('document.querySelector("#order-slots li:last-child .order-detail").textContent'),'112 BPM · Final groove holds');
    await editField('Phrases (0 = hold)','0','.song:last-child');
    assert.equal(await evaluate('[...document.querySelectorAll(".song p")].filter(p=>p.textContent.includes("Original Compás accompaniment")).length'),5);
    await evaluate(`(()=>{URL.createObjectURL=blob=>{window.setBlob=blob;return 'blob:test';};
      HTMLAnchorElement.prototype.click=function(){};})()`);
    await click('#save');
    await until(content('status','Exported 16 songs'),'regional export failed');
    const regional=await evaluate('setBlob.text().then(JSON.parse)');
    assert.deepEqual(regional.songs.slice(10).map(s=>s.study),['cdmx','mty-dub','tribal','villera','lima','chile']);
    await click('#library summary');
    const searchSource = value=>evaluate(`(()=>{
      const input=document.querySelector('#repertoire-search');
      input.value=${JSON.stringify(value)};
      input.dispatchEvent(new Event('input'));
    })()`);
    await searchSource('Júrame');
    assert.equal(await evaluate('document.querySelector("#repertoire-results h3").textContent'),'Júrame');
    const jurame=await evaluate(`atob(document.querySelector('#repertoire-results a[download="jurame-schirmer-refrain.mid"]').href.split(',')[1])`);
    assert.equal(jurame.slice(0,4),'MThd');
    assert.equal(jurame.charCodeAt(11),3);
    const fullJurame=await evaluate(`atob(document.querySelector('#repertoire-results a[download="jurame-schirmer-song.mid"]').href.split(',')[1])`);
    assert.equal(fullJurame.slice(0,4),'MThd');
    assert.equal(fullJurame.charCodeAt(11),4);
    assert(await evaluate('!!document.querySelector("#repertoire-results .repertoire-slot")'));
    await searchSource('Tristeza');
    assert.deepEqual(await evaluate('[...document.querySelectorAll("#repertoire-results h3")].map(n=>n.textContent)'),['Tristeza']);
    assert.equal(await evaluate('document.querySelectorAll("#repertoire-results a[download], #repertoire-results .repertoire-slot").length'),0);
    assert.equal(await evaluate('!!document.querySelector("#repertoire-results a[href*=resdanza]") && !!document.querySelector("#repertoire-results a[href*=frontera]")'),true);
    for (const [title, source] of [
      ['Alla en el Rancho Grande', 'themorgan.org'],
      ['Jesusita en Chihuahua', 'digital.library.unt.edu'],
      ['Me Estoy Enamorando', 'MN0117025'],
      ['Nuestra Cancion', 'expressnews.com'],
    ]) {
      await searchSource(title);
      const cards=await evaluate('[...document.querySelectorAll("#repertoire-results article")].map(card=>({'+
        'links:[...card.querySelectorAll("a")].map(a=>a.href),'+
        'playable:!!card.querySelector("a[download], .repertoire-slot")}))');
      assert.equal(cards.length,1,title);
      assert(cards[0].links.some(link=>link.includes(source)),title);
      assert.equal(cards[0].playable,false,title);
    }
    for (const [title, witnesses, playable] of [
      ['Alla en el Rancho Grande', ['elranchogrande-1gma2hk.pdf#page=2', 'Stasny-20sgpv6.pdf#page=1',
        'metadc38867/m1/125/', 'el-rancho-grande-24'], false],
      ['La Bamba', ['hess_ch6oag.pdf#page=22', 'archival_objects/284377',
        'heritageobject/la-bamba', 'music.apple.com/us/song/1442962335'], false],
      ['Merengue (Suite de Impresiones', ['M-3054', 'etd.zffi-977w', 'bath-kh57'], false],
      ['Yo me Encelere', ['etd.zffi-977w', '7tja-nr40'], false],
      ['Juangomero', ['f5bd53c4-5830-431f-bfcb-e29cefd53791', '00085523PI', 'biblionumber=60784', '00274076SR', '00270111SR'], false],
      ['La Morena', ['0743213241623'], false],
      ['Fiesta Caliente', ['0743213241623', '0884977590708'], false],
      ['La Valentina', ['5_Canciones_mexicanas_para_guitarra_', 'La_Valentina_',
        'Arreglos_Mexicanos_Revolucionarios_', 'la-adelita-con-la-valentina', '332372'], true],
    ]) {
      await searchSource(title);
      const cards = await evaluate(`[...document.querySelectorAll('#repertoire-results article')].map(card=>({
        links:[...card.querySelectorAll('a')].map(a=>a.href),
        playable:!!card.querySelector('a[download], .repertoire-slot')
      }))`);
      assert.equal(cards.length,1,title);
      for (const witness of witnesses) assert(cards[0].links.some(link=>link.includes(witness)),witness);
      assert.equal(cards[0].playable,playable,title);
    }
    const valentinaMIDI=await evaluate(`atob(document.querySelector('a[download="la-valentina-apke-guitar.mid"]').href.split(',')[1])`);
    assert.deepEqual([...valentinaMIDI.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,5,0,24]);
    assert(valentinaMIDI.includes('CC BY-SA 4.0'));
    assert.equal(await evaluate(`document.querySelector('#repertoire-results option[value="la-valentina-apke"]').textContent`),'La Valentina · Apke guitar');
    await searchSource('cielito lindo');
    const destination='#repertoire-results .repertoire-slot label:nth-child(2) select';
    const place='#repertoire-results .repertoire-slot button';
    assert.equal(await evaluate(`document.querySelector('${place}').disabled`),true);
    await set(destination,'15');
    await click(place);
    await click('#save');
    const replaced=await evaluate('setBlob.text().then(JSON.parse)');
    assert.deepEqual(replaced.songs.slice(0,15),regional.songs.slice(0,15));
    assert.equal(replaced.songs.length,16);
    assert.equal(replaced.songs[15].study,'cielito-lindo-piano');
    assert.equal(replaced.songs[15].title,'CIELITO LINDO');
    await click('#library summary');
    // Free six slots before the existing add/reorder/score round-trip checks.
    for(let i=0;i<6;i++) await click('.song:last-child .actions button:last-child');
    await until('document.querySelectorAll(".song").length===10','remove failed');
    await click('#library summary');
    await evaluate(`(()=>{const input=document.querySelector('#repertoire-search');
      input.value='sebastian';input.dispatchEvent(new Event('input'));})()`);
    assert.deepEqual(await evaluate('[...document.querySelectorAll("#repertoire-results h3")].map(n=>n.textContent)'),['La Paloma']);
    assert.equal(await evaluate('document.querySelectorAll("#repertoire-results a[download]").length'),5);
    await set('#repertoire-results .repertoire-slot label:first-child select','la-paloma-piano');
    assert.equal(await evaluate(`document.querySelector('${place}').textContent`),'Add score');
    await click(place);
    await click('#save');
    const appended=await evaluate('setBlob.text().then(JSON.parse)');
    assert.deepEqual(appended.songs.slice(0,10),regional.songs.slice(0,10));
    assert.equal(appended.songs.length,11);
    assert.equal(appended.songs[10].study,'la-paloma-piano');
    assert.equal(appended.songs[10].bpm,88);
    assert.equal(appended.songs[10].guide,false);
    await click('.song:last-child .actions button:last-child');
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download="la-paloma-piano-rehearsal.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,4,0,24]);
    assert.equal(await evaluate('atob(document.querySelector("#repertoire-results a[download]").href.split(",")[1]).slice(0,4)'),'MThd');
    await evaluate(`(()=>{const input=document.querySelector('#repertoire-search');
      input.value='el manisero';input.dispatchEvent(new Event('input'));})()`);
    assert.equal(await evaluate(`document.querySelector('#repertoire-results a[href$="#page=34"]')?.textContent.includes('Bohemia')`),true);
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download="el-manisero-bohemia-opening.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,4,0,24]);
    assert.equal(await evaluate(`atob(document.querySelector('#repertoire-results a[download="el-manisero-bohemia-opening.mid"]').href.split(',')[1]).includes('RISING BASS')`),true);
    assert.equal(await evaluate(`document.querySelector('#repertoire-results a[href$="#page=2"]')?.textContent.includes('Kempton')`),true);
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download="el-manisero-kempton-calls.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,5,0,24]);
    assert.equal(await evaluate(`document.querySelector('#repertoire-results a[download="el-manisero-kempton-bass.mid"]')?.textContent`),'Piano left-hand MIDI');
    assert.equal(await evaluate(`document.querySelector('#repertoire-results a[download="el-manisero-kempton-bass-rehearsal.mid"]')?.textContent.startsWith('Rehearsal MIDI')`),true);
    assert.equal(await evaluate(`document.querySelectorAll('.song select option[value="el-manisero-kempton-bass"]').length > 0`),true);
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download="el-manisero-kempton-piano-rehearsal.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,5,0,24]);
    assert.equal(await evaluate(`document.querySelector('.song select option[value="el-manisero-kempton-piano"]')?.textContent`),'El Manisero · Kempton printed piano');
    for(const [name,count] of [['el-manisero-kempton-whistle.mid',2],['el-manisero-kempton-whistle-rehearsal.mid',6]]) {
      const download=await evaluate(`atob(document.querySelector('a[download="${name}"]').href.split(',')[1])`);
      assert.deepEqual([...download.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,count,0,24]);
      assert(download.includes('Peanut-whistle imitation'));
    }
    await evaluate(`(()=>{const input=document.querySelector('#repertoire-search');
      input.value='lamento borincano';input.dispatchEvent(new Event('input'));})()`);
    assert.equal(await evaluate(`document.querySelector('#repertoire-results a').href`),'https://files.eric.ed.gov/fulltext/ED059933.pdf#page=33');
    assert.equal(await evaluate('document.querySelectorAll("#repertoire-results a[download]").length'),0);
    await evaluate(`(()=>{const input=document.querySelector('#repertoire-search');
      input.value='lagrimas negras';input.dispatchEvent(new Event('input'));})()`);
    assert.equal(await evaluate(`document.querySelectorAll('#repertoire-results a[href*="biblionumber=65362"]').length`),1);
    assert.equal(await evaluate(`document.querySelector('.song select option[value="tribal"]')?.textContent`),'Tribal guarachero · 132');
    await evaluate(`(()=>{const input=document.querySelector('#repertoire-search');
      input.value='aquilina';input.dispatchEvent(new Event('input'));})()`);
    assert.equal(await evaluate(`document.querySelector('#repertoire-results h3')?.textContent`),'Juana Quilina');
    assert.equal(await evaluate(`document.querySelector('#repertoire-results a[href="https://creativecommons.org/licenses/by-sa/3.0/"]')?.textContent`),'CC BY-SA 3.0');
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download="juana-quilina-fragment.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,2,0,24]);
    assert.equal(await evaluate(`document.querySelector('${place}').textContent`),'Add score');
    await click(place);
    assert.equal(await evaluate(`document.querySelector('.song:last-child select').value`),'juana-quilina');
    await click('.song:last-child .actions button:last-child');
    await evaluate(`(()=>{const input=document.querySelector('#repertoire-search');
      input.value='santiago';input.dispatchEvent(new Event('input'));})()`);
    assert.equal(await evaluate(`[...document.querySelectorAll('#repertoire-results a')].some(a=>a.textContent.includes('Columbia 3081-X') && a.textContent.includes('W 96308'))`),true);
    assert.equal(await evaluate('document.querySelectorAll("#repertoire-results a[download]").length'),0);
    for (const name of ['Juangomero', 'Juan Gomero']) {
      await searchSource(name);
      const entries = await evaluate(`[...document.querySelectorAll('#repertoire-results article')].map(card=>({
        title:card.querySelector('h3').textContent,
        links:[...card.querySelectorAll('a')].map(a=>a.href),
        playable:!!card.querySelector('a[download], .repertoire-slot')
      }))`);
      assert.equal(entries.length,1,name);
      assert.equal(entries[0].title,'Juangomero (Juan Gomero)');
      for (const witness of ['I00002DlhWcTRApE', 'f5bd53c4', '00274076SR', '00270111SR']) {
        assert(entries[0].links.some(link=>link.includes(witness)),witness);
      }
      assert.equal(entries[0].playable,false);
    }
    for(const title of ['compadre pedro juan','merengue del soldado','el diente de oro','cabo e’ vela','la faldita']) {
      await searchSource(title);
      assert.equal(await evaluate('document.querySelectorAll("#repertoire-results article").length'),1);
      assert.equal(await evaluate('document.querySelectorAll("#repertoire-results a[download], #repertoire-results .repertoire-slot").length'),0);
    }
    for(const [title,score,recording,broadcast] of [
      ['Baila Esta Cumbia','MN0075339','1633344247',true],
      ['La Carcacha','00310950','1489663147',false],
      ['Techno Cumbia','MixedFolios.pdf#page=14','1703308142',false],
      ['No Debes Jugar','00310950','721269517',true],
      ['La Llamada','00310950','714064735',true],
      ['Juana la Cubana','21699929.html','juana-la-cubana-0',false],
    ]) {
      await searchSource(title);
      const card='#repertoire-results article';
      assert.equal(await evaluate(`document.querySelectorAll('${card}').length`),1);
      assert.equal(await evaluate(`document.querySelector('${card} h3').textContent`),title);
      assert.equal(await evaluate(`document.querySelectorAll('${card} a[href*="${score}"]').length`),1);
      assert.equal(await evaluate(`document.querySelectorAll('${card} a[href*="${recording}"]').length`),1);
      assert.equal(await evaluate(`document.querySelectorAll('${card} a[href*="johnny-canales-presenta-selena"]').length`),Number(broadcast));
      assert.equal(await evaluate(`document.querySelector('${card}').textContent.includes('distribution permissions remain unverified')`),true);
      assert.equal(await evaluate(`document.querySelectorAll('${card} a[download], ${card} .repertoire-slot').length`),0);
    }
    await searchSource('missing my baby');
    assert.equal(await evaluate('document.querySelector("#repertoire-results h3").textContent'),'Missing My Baby');
    assert.deepEqual(await evaluate(`[...document.querySelectorAll('#repertoire-results article a')].map(a=>a.href)`),[
      'https://www.halleonard.com/dealers/bin/PromoSpanishPublications.pdf',
      'https://selena-official.com/products/dreaming-of-you-30th-anniversary-cd',
      'https://music.apple.com/us/song/1443161536',
    ]);
    assert.equal(await evaluate('document.querySelectorAll("#repertoire-results a[download], #repertoire-results .repertoire-slot").length'),0);
    await searchSource('sobre las olas');
    assert.equal(await evaluate('document.querySelector("#repertoire-results h3").textContent'),'Sobre las olas');
    assert.equal(await evaluate(`document.querySelector('${place}').textContent`),'Add score');
    const olas=await evaluate(`atob(document.querySelector('#repertoire-results a[download="sobre-las-olas-trifet-theme.mid"]').href.split(',')[1])`);
    assert.deepEqual([...olas.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,3,0,24]);
    assert.ok(olas.includes('ANSWERING PHRASE') && olas.includes('CLOSING TWO BARS'));
    const strain=await evaluate(`atob(document.querySelector('#repertoire-results a[download="sobre-las-olas-trifet-strain.mid"]').href.split(',')[1])`);
    assert.deepEqual([...strain.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,3,0,24]);
    assert.ok(strain.includes('FIRST ENDING') && strain.includes('SECOND ENDING AND FINE'));
    await set('#repertoire-results .repertoire-slot select','sobre-las-olas-trifet-strain');
    await click(place);
    assert.equal(await evaluate(`document.querySelector('.song:last-child select').value`),'sobre-las-olas-trifet-strain');
    await click('.song:last-child .actions button:last-child');
    await searchSource('ma teodora');
    assert.equal(await evaluate('document.querySelector("#repertoire-results h3").textContent'),'Son de la Má Teodora');
    assert.equal(await evaluate('document.querySelectorAll("#repertoire-results .repertoire-slot").length'),1);
    assert.equal(await evaluate(`document.querySelector('${place}').textContent`),'Add score');
    await click(place);
    assert.equal(await evaluate(`document.querySelector('.song:last-child select').value`),'ma-teodora-1929');
    assert.equal(await evaluate(`document.querySelector('.song:last-child input').value`),'MÁ TEODORA');
    assert.equal(await evaluate(`document.querySelector('.song:last-child').textContent.includes('Tempo (dotted-quarter BPM)')`),true);
    await click('.song:last-child .actions button:last-child');
    const teodora = await evaluate(`atob(document.querySelector('#repertoire-results a[download="ma-teodora-cuba-musical-1929.mid"]').href.split(',')[1])`);
    assert.deepEqual([...teodora.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,2,0,24]);
    assert.ok(teodora.includes('ECHOES') && teodora.includes('FINAL RESTS'));
    await searchSource('dile a catalina');
    assert.equal(await evaluate(`document.querySelectorAll('#repertoire-results a[href*="bid=7235"]').length`),1);
    assert.equal(await evaluate('document.querySelectorAll("#repertoire-results a[download], #repertoire-results .repertoire-slot").length'),0);
    await searchSource('la adelita');
    assert.equal(await evaluate('document.querySelector("#repertoire-results h3").textContent'),'La Adelita');
    assert.equal(await evaluate(`document.querySelector('#repertoire-results a[href="https://creativecommons.org/licenses/by/4.0/"]').textContent`),'CC BY 4.0');
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download="la-adelita-kemp-opening.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,5,0,24]);
    assert.equal(await evaluate(`document.querySelector('${place}').textContent`),'Add score');
    await click(place);
    assert.equal(await evaluate(`document.querySelector('.song:last-child select').value`),'la-adelita-kemp');
    await click('.song:last-child .actions button:last-child');
    const adelita = await evaluate(`atob(document.querySelector('#repertoire-results a[download="la-adelita-kemp-quartet.mid"]').href.split(',')[1])`);
    assert.deepEqual([...adelita.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,5,0,24]);
    assert.ok(adelita.includes('PAGE 4') && adelita.includes('CC BY 4.0'));
    await searchSource('siboney');
    const siboney = await evaluate(`atob(document.querySelector('#repertoire-results a[download="siboney-feist-chorus.mid"]').href.split(',')[1])`);
    assert.deepEqual([...siboney.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,5,0,24]);
    assert.ok(siboney.includes('Vocal guide') && siboney.includes('SECOND PHRASE'));
    const last = await evaluate(`atob(document.querySelector('#repertoire-results a[download="siboney-feist-last.mid"]').href.split(',')[1])`);
    assert.deepEqual([...last.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,6,0,24]);
    assert.ok(last.includes('Lower piano counterline') && last.includes('FINAL CADENCE'));
    const verseLast = await evaluate(`atob(document.querySelector('#repertoire-results a[download="siboney-feist-verse-last.mid"]').href.split(',')[1])`);
    assert.deepEqual([...verseLast.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,6,0,24]);
    assert.ok(verseLast.includes('Bass counterline') && verseLast.includes('SEGNO LEAD') && verseLast.includes('LAST ENDING'));
    assert.equal(await evaluate(`document.querySelector('#repertoire-results select option[value="siboney-feist-verse-last"]')?.textContent`),
      'Siboney · Feist first verse through Last');
    const siboneySong = await evaluate(`atob(document.querySelector('#repertoire-results a[download="siboney-feist-song.mid"]').href.split(',')[1])`);
    assert.deepEqual([...siboneySong.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,6,0,24]);
    for (const marker of ['FIRST ENDING','CHORUS REPEAT','SECOND ENDING','D.S. RETURN','VERSE TWO','LAST ENDING']) {
      assert.ok(siboneySong.includes(marker),marker);
    }
    assert.equal(await evaluate(`document.querySelector('#repertoire-results select option[value="siboney-feist-song"]')?.textContent`),
      'Siboney · Feist two verses and D.S. return');
    await searchSource('zacatecas');
    const zacatecas = await evaluate(`atob(document.querySelector('#repertoire-results a[download="zacatecas-levy-opening.mid"]').href.split(',')[1])`);
    assert.deepEqual([...zacatecas.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,4,0,24]);
    assert.ok(zacatecas.includes('FIRST ENDING') && zacatecas.includes('SECOND ENDING'));
    await searchSource('la mora');
    assert.equal(await evaluate('document.querySelector("#repertoire-results h3").textContent'),'La Mora');
    assert.deepEqual(await evaluate(`[...document.querySelectorAll('#repertoire-results a[download]')].map(a=>a.download)`),
      ['la-mora-grenet-opening-bass-piano-rehearsal.mid','la-mora-grenet-opening-bass.mid',
        'la-mora-grenet-opening-treble.mid','la-mora-grenet-refrain.mid','la-mora-grenet-interlude.mid',
        'la-mora-grenet-return.mid','la-mora-grenet-sung-bass.mid',
        'la-mora-grenet-sung-guide.mid','la-mora-grenet-sung-bass-rehearsal.mid','la-mora-grenet-singer-finale.mid']);
    const moraSung=await evaluate(`atob(document.querySelector('#repertoire-results a[download="la-mora-grenet-sung-bass-rehearsal.mid"]').href.split(',')[1])`);
    assert.deepEqual([...moraSung.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,3,0,24]);
    assert.ok(moraSung.includes('Canto guide') && moraSung.includes('SECOND ENDING'));
    const refrain = await evaluate(`atob(document.querySelector('#repertoire-results a[download="la-mora-grenet-refrain.mid"]').href.split(',')[1])`);
    assert.deepEqual([...refrain.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,3,0,24]);
    assert.ok(refrain.includes('CLOSING FIGURE'));
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),
      [0,1,0,6,0,24]);
    assert.equal(await evaluate(`atob(document.querySelector('#repertoire-results a[download]').href.split(',')[1]).slice(0,4)`),'MThd');
    const moraFinale=await evaluate(`atob(document.querySelector('#repertoire-results a[download="la-mora-grenet-singer-finale.mid"]').href.split(',')[1])`);
    assert.deepEqual([...moraFinale.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,5,0,24]);
    assert.ok(moraFinale.includes('CONTINUATION') && moraFinale.includes('FINALE') && moraFinale.includes('Cross-staff piano answer'));
    const moraSelect='#repertoire-results .repertoire-slot label:first-child select';
    assert.deepEqual(await evaluate(`[...document.querySelector('${moraSelect}').options].map(o=>o.value)`),
      ['la-mora-opening','la-mora-refrain','la-mora-interlude','la-mora-sung-bass','la-mora-return','la-mora-singer-finale']);
    const moraReturn = await evaluate(`atob(document.querySelector('#repertoire-results a[download="la-mora-grenet-return.mid"]').href.split(',')[1])`);
    assert.deepEqual([...moraReturn.slice(8,14)].map(c=>c.charCodeAt(0)),[0,1,0,6,0,24]);
    assert.ok(moraReturn.includes('FIRST ENDING') && moraReturn.includes('ALTERNATE SECOND ENDING'));
    await set(moraSelect,'la-mora-return');
    await click(place);
    assert.equal(await evaluate(`document.querySelector('.song:last-child select').value`),'la-mora-return');
    await click('.song:last-child .actions button:last-child');
    await click('.song:nth-child(9) input[type=checkbox]');
    await evaluate(`(()=>{const input=document.querySelector('#repertoire-search');
      input.value='la golondrina';input.dispatchEvent(new Event('input'));})()`);
    assert.deepEqual(await evaluate(`[...document.querySelectorAll('#repertoire-results .repertoire-slot label:first-child select option')].map(o=>o.value)`),
      ['la-golondrina','la-golondrina-piano','la-golondrina-piano-bolero']);
    assert.deepEqual(await evaluate('[...document.querySelectorAll("#repertoire-results a[download]")].map(a=>a.download)'),
      ['la-golondrina-left-hand.mid','la-golondrina-canto.mid','la-golondrina-rehearsal.mid',
       'la-golondrina-right-hand.mid','la-golondrina-piano-rehearsal.mid']);
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download="la-golondrina-piano-rehearsal.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,4,0,24]);
    assert.deepEqual(await evaluate(`[...atob(document.querySelector('#repertoire-results a[download$="-rehearsal.mid"]').href.split(',')[1]).slice(8,14)].map(c=>c.charCodeAt(0))`),[0,1,0,3,0,24]);
    await click('#add');
    await until('document.querySelectorAll(".song").length===11','add failed');
    await click('.song:last-child button');
    assert.equal(await evaluate('document.activeElement.closest(".song").querySelector("h2").textContent'),'Slot 10');
    assert.equal(await evaluate('document.activeElement.textContent'),'Move up');
    assert.equal(await evaluate('document.querySelector("#order-slots li:nth-child(10) .order-title").textContent'),'10 · NEW SONG');
    await click('.song:last-child input[type=checkbox]');
    await evaluate(`(()=>{URL.createObjectURL=blob=>{window.setBlob=blob;return 'blob:test';};
      HTMLAnchorElement.prototype.click=function(){};})()`);
    await click('#save');
    await until(content('status','Exported 11 songs'),'export failed');
    const saved=await evaluate('setBlob.text().then(JSON.parse)');
    assert.equal(saved.songs[9].title,'NEW SONG');
    assert.equal(saved.songs[8].study,'la-paloma');
    assert.equal(saved.songs[8].guide,true);
    assert.equal(saved.songs[10].study,'la-golondrina');
    assert.equal(saved.songs[10].guide,true);
    await evaluate(`(()=>{const dt=new DataTransfer();
      dt.items.add(new File(['{"format":2}'],'bad.json'));
      const input=document.querySelector('#file');input.files=dt.files;input.dispatchEvent(new Event('change'));})()`);
    await until(content('status','Current set kept'),'invalid import did not fail closed');
    assert.equal(await evaluate('document.querySelectorAll(".song").length'),11);
    await evaluate(`(async()=>{const dt=new DataTransfer();
      dt.items.add(new File([await setBlob.text()],'set.json'));
      const input=document.querySelector('#file');input.files=dt.files;input.dispatchEvent(new Event('change'));})()`);
    await until(content('status','Opened 11 songs'),'portable import failed');
    assert.equal(await evaluate('document.querySelectorAll("#order-slots button").length'),11);
    assert.equal(await evaluate('document.querySelector("#order-slots li:last-child .order-title").textContent'),'11 · LA GOLONDRINA');
    assert.equal(await evaluate('document.querySelector(".song:nth-child(9) input[type=checkbox]").checked'),true);
    assert.equal(await evaluate('document.querySelector(".song:last-child input[type=checkbox]").checked'),true);
    const tribute=await readFile(new URL('../repertoire/johnny-canales-rehearsal-set.json',import.meta.url),'utf8');
    await evaluate(`(()=>{
      const file=new DataTransfer();
      file.items.add(new File([${JSON.stringify(tribute)}],'johnny-canales.json'));
      const input=document.querySelector('#file');
      input.files=file.files;input.dispatchEvent(new Event('change'));
    })()`);
    await until('document.querySelector("#order-slots li:first-child .order-title")?.textContent==="01 · TEJANO CUMBIA"','tribute set did not load');
    assert.equal(await evaluate('document.querySelectorAll("#order-slots li").length'),11);
    assert.equal(await evaluate('document.querySelector("#order-slots li:nth-child(2) .order-title").textContent'),'02 · LA CUCARACHA');
    if (process.env.COMP_REHEARSAL_EXPORT) {
      const grooves={format:1,songs:[0,2,3,4,5,6].map(index=>regional.songs[index])};
      await evaluate(`(()=>{
        const files=new DataTransfer();
        files.items.add(new File([${JSON.stringify(JSON.stringify(grooves))}],'six-styles.json'));
        const input=document.querySelector('#file');
        input.files=files.files;
        input.dispatchEvent(new Event('change'));
      })()`);
      await until(content('status','Opened 6 songs'),'six-style rehearsal set did not load');
      await searchSource('cielito lindo');
      await click(place);
      for(let i=0;i<3;i++) await click(`.song:nth-child(${7-i}) .actions button:first-child`);
      await click('.song:nth-child(4) input[type=checkbox]');
      await searchSource('el manisero');
      await set('#repertoire-results .repertoire-slot label:first-child select','el-manisero-kempton-piano');
      await click(place);
      await click('#save');
      await until(content('status','Exported 8 songs'),'mixed rehearsal set did not export');
      const serialized=await evaluate('setBlob.text()');
      const mixed=JSON.parse(serialized);
      assert.deepEqual(mixed.songs.map(song=>song.study),
        ['coast','bolero','son','cielito-lindo-piano','salsa','merengue','tejano','el-manisero-kempton-piano']);
      assert.equal(mixed.songs[3].guide,true);
      await writeFile(process.env.COMP_REHEARSAL_EXPORT,serialized,{flag:'wx'});
    }
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,editorRoundTrip:true,runningOrder:true,tributeSetImport:true,repertoireSearchAndMidi:true,repertoireAppendAndReplace:true,songs:11,exported:saved}));
  } else if (new URL(url).searchParams.has('fill-check')) {
    const arp='#buttons button:nth-child(11)', chorus='#keys button:nth-child(13)';
    await until('document.querySelectorAll("#keys button").length===27','board did not load');
    assert.equal(await evaluate(`(()=>{const play=document.querySelector('#transport');
      return play?.textContent==='PLAY' && play.getBoundingClientRect().bottom<innerHeight &&
        Number(document.querySelector('#master').value)>0 && !document.querySelector('#enable').disabled;})()`),true);
    await click('#transport');
    await until('window.boardSnapshot?.[20] && energy()>0.01','first PLAY did not start audible audio');
    await until(content('transport','STOP'),'primary transport did not show STOP');
    const locate=async selector=>evaluate(`(()=>{const rect=document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();
      return {x:Math.round(rect.x+rect.width/2),y:Math.round(rect.bottom-12)};})()`);
    await evaluate(`document.querySelector('#keys').scrollIntoView({block:'end',inline:'nearest'})`);
    const arpPoint={...await locate(arp),id:1}, chorusPoint={...await locate(chorus),id:2};
    const touches=async points=>send('Input.dispatchTouchEvent',{type:points.length?'touchStart':'touchEnd',touchPoints:points});
    await touches([arpPoint]);
    await until('boardSnapshot[24]===1','ARP did not queue a fill while held',800);
    await touches([arpPoint,chorusPoint]);
    await until('boardSnapshot[65]===3 && boardSnapshot[24]!==0','Chorus did not cue behind the fill');
    await touches([]);
    await until('boardSnapshot[64]===3 && boardSnapshot[24]===0','Chorus did not follow the fill');
    await send('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,x:arpPoint.x,y:arpPoint.y});
    await until('boardSnapshot[24]!==0','mouse press did not queue a fill');
    await until('boardSnapshot[24]===0','held mouse fill did not finish');
    await send('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,x:arpPoint.x,y:arpPoint.y});
    await sleep(130);
    assert.equal(await evaluate('boardSnapshot[24]'),0,'pointer release queued a second fill');
    await evaluate(`document.querySelector('${arp}').focus({preventScroll:true})`);
    const enter={key:'Enter',code:'Enter',windowsVirtualKeyCode:13};
    await send('Input.dispatchKeyEvent',{...enter,type:'keyDown'});
    await send('Input.dispatchKeyEvent',{...enter,type:'keyUp'});
    await until('boardSnapshot[24]!==0','Enter on ARP did not queue a fill');
    await click('#transport');
    await until('!boardSnapshot[20] && !boardSnapshot[24] && energy()===0','STOP did not clear the fill and PCM');
    await until(content('transport','PLAY'),'primary transport did not return to PLAY');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      visiblePlay:true,coldPlayPCM:true,audioRate:await evaluate('tap.context.sampleRate'),touchFillIntoChorus:true,mouseReleaseDidNotRepeat:true,keyboardActivation:true,silentStop:true}));
  } else if (url.includes('Compas-Sonidero-Board.html')) {
    const first={format:1,songs:[
      {title:'PAMBICHE',study:'pambiche',kit:true},
      {title:'PALOMA GUIADA',study:'la-paloma',guide:true,bpm:97}]};
    const second={format:1,songs:[{title:'EL MANISERO',study:'el-manisero-kempton-piano',bpm:180,guide:true}]};
    const importSet=(name,data)=>evaluate(`(()=>{
      const dt=new DataTransfer();
      dt.items.add(new File([${JSON.stringify(typeof data==='string'?data:JSON.stringify(data))}],${JSON.stringify(name)}));
      const input=document.querySelector('#open-set');
      input.files=dt.files;
      input.dispatchEvent(new Event('change'));
    })()`);
    const canvas="document.querySelector('#screen').toDataURL()";
    await until('document.querySelectorAll("#keys button").length===27','board bench did not load');
    const splash=await evaluate(canvas);
    const pick=name=>set('#rehearsal-set',name);
    const builtInSets=await evaluate('[...document.querySelector("#rehearsal-set").options].map(option=>option.value)');
    assert.equal(new Set(builtInSets).size,builtInSets.length);
    for (const name of ['factory','tribute','jurame','la-mora-finale','colombian-727','son-salsa']) {
      assert(builtInSets.includes(name), 'Missing rehearsal '+name);
    }
    assert.equal(await evaluate('document.querySelector("#rehearsal-set").getBoundingClientRect().right<=innerWidth'),true);
    await pick('san-antonio');
    await until(content('set-name','San Antonio'),'built-in San Antonio set did not open before audio');
    assert.notEqual(await evaluate(canvas),splash);
    await pick('factory');
    await until(content('set-name','Factory set'),'factory set did not reopen');
    await importSet('bad.json',{format:2,songs:[]});
    await until(content('message','Current set kept'),'bad set was not rejected');
    assert.equal(await evaluate('document.querySelector("#set-name").textContent'),'Factory set');
    await importSet('first.json',first);
    await until(content('set-name','first.json'),'set import before audio did not complete');
    assert.notEqual(await evaluate(canvas),splash);
    await click('#enable');
    await until('document.querySelector("#enable").disabled','audio worklet did not enable');
    await evaluate(`(()=>{const el=document.querySelector('#master');el.value='74';el.dispatchEvent(new Event('input'));})()`);
    const play=()=>evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="PLAY").click()');
    await play();
    await until('window.tap && energy()>0.01','imported set produced no PCM');
    assert.equal(await evaluate(`document.querySelector('#rehearsal-set').dispatchEvent(
      new KeyboardEvent('keydown',{key:' ',bubbles:true,cancelable:true}))`),true);
    await pick('adelita');
    await until(content('message','PRESETS still cues songs'),'built-in set was not refused during playback');
    assert.equal(await evaluate('document.querySelector("#set-name").textContent'),'first.json');
    await until('energy()>0.01','built-in set refusal interrupted playback');
    await importSet('second.json',second);
    await until(content('message','before loading another set'),'live set import did not refuse');
    assert.equal(await evaluate('document.querySelector("#set-name").textContent'),'first.json');
    await until('energy()>0.01','refusal interrupted playback');
    await play();
    await until('energy()===0','STOP did not clear browser output');
    await pick('adelita');
    await until(content('set-name','Kemp quartet rehearsal'),'built-in set did not open with audio enabled');
    await play();
    await until('window.boardSnapshot?.[51]===9 && energy()>0.01','built-in score did not play');
    await play();
    await until('energy()===0','STOP did not clear built-in score output');
    await importSet('second.json',second);
    await until(content('set-name','second.json'),'stopped set replacement did not complete');
    await evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="GLO").click()');
    await click('[aria-label="SELECT up"]');
    await evaluate('[...document.querySelectorAll("#buttons button")].find(el=>el.textContent==="SAVE").click()');
    await until('window.savedBrowserSet && new TextDecoder().decode(savedBrowserSet.slice(85,96))==="EL MANISERO"',
                'saving to an empty imported slot did not reach the browser display owner');
    assert.equal(await evaluate('new TextDecoder().decode(savedBrowserSet.slice(0,11))'),'EL MANISERO');
    await play();
    await until('energy()>0.01','replacement set produced no PCM');
    await play();
    await until('energy()===0','STOP did not clear the replacement score');
    await importSet('Tribal-132.json',{format:1,songs:[
      {title:'TRIBAL 132',study:'tribal',feel:0,section:'lift',kit:true}]});
    await until(content('set-name','Tribal-132.json'),'Tribal set did not load');
    await play();
    await until('energy()>0.01','Tribal study produced no PCM');
    await play();
    await until('energy()===0','STOP did not clear the Tribal study');
    await importSet('727-editor.json',{format:1,songs:[{title:'727 EDIT',study:'mx',feel:0}]});
    await until(content('set-name','727-editor.json'),'editor groove did not load');
    const panelButton=label=>evaluate(`[...document.querySelectorAll('#buttons button')].find(b=>b.textContent===${JSON.stringify(label)}).click()`);
    await panelButton('REC');
    const rackImage=await evaluate(canvas);
    await panelButton('SEQ');
    await until(`${canvas}!==${JSON.stringify(rackImage)}`,'SEQ did not open the editor');
    for(const i of [0,4,8,12]) {
      await evaluate(`document.querySelectorAll('#keys button')[${i}].dispatchEvent(new KeyboardEvent('keydown',{key:'Enter'}))`);
      await sleep(60);
      await evaluate(`document.querySelectorAll('#keys button')[${i}].dispatchEvent(new KeyboardEvent('keyup',{key:'Enter'}))`);
      await sleep(60);
    }
    await panelButton('FX'); // Layer -> Drums Only; source backing is suppressed.
    await play();
    await until('energy()>0.001','programmed 727 lane produced no PCM');
    await evaluate(`(()=>{const create=URL.createObjectURL;URL.createObjectURL=blob=>{if(blob.type==='application/octet-stream') window.rhythmFile=blob;return create(blob);};})()`);
    await click('#save-rhythm');
    await until('window.rhythmFile?.size===140','rhythm export did not arrive from audio owner');
    const savedRhythm=await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()');
    assert.deepEqual(savedRhythm.slice(0,4),[67,55,82,49]);
    assert.equal(savedRhythm[10],2);
    const openRhythm=bytes=>evaluate(`(()=>{const dt=new DataTransfer();dt.items.add(new File([new Uint8Array(${JSON.stringify(bytes)})],'personal.c7r'));const el=document.querySelector('#open-rhythm');el.files=dt.files;el.dispatchEvent(new Event('change'));})()`);
    await openRhythm(savedRhythm);
    await until(content('message','Press STOP'),'live rhythm import was not refused');
    await until('energy()>0.001','live import refusal interrupted audio');
    await play();
    await until('energy()===0','727 pattern did not stop');
    await importSet('empty-editor.json',{format:1,songs:[{title:'EMPTY EDIT',study:'mx',feel:0}]});
    await until(content('set-name','empty-editor.json'),'fresh audio owner did not boot');
    await openRhythm(savedRhythm);
    await until(content('message','Rhythm opened'),'portable rhythm did not restore');
    await click('#save-rhythm');
    await until(content('message','Rhythm file saved'),'restored rhythm could not export');
    assert.deepEqual(await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()'),savedRhythm);
    const damaged=[...savedRhythm];damaged[20]^=1;
    await openRhythm(damaged);
    await until(content('message','invalid or damaged'),'bad rhythm was not rejected');
    await click('#save-rhythm');
    await until(content('message','Rhythm file saved'),'post-rejection export failed');
    assert.deepEqual(await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()'),savedRhythm);
    await play();await until('energy()>0.001','restored rhythm was silent');
    await play();await until('energy()===0','restored rhythm failed to stop');
    await panelButton('SEQ');
    await panelButton('REC');
    await panelButton('SEQ');
    await click('[aria-label="PRESETS up"]');
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'conga export failed');
    const congaFile=await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()');
    const congaView=new DataView(Uint8Array.from(congaFile).buffer);
    assert.equal(congaView.getUint32(16+2*8,true),0x04100410);
    assert.equal(congaView.getUint32(16+3*8,true),0x00010001);
    assert.equal(congaView.getUint32(16+4*8,true),0x50005000);
    await play();await until('energy()>0.001','reference conga was silent');
    await click('[aria-label="PRESETS up"]');
    await sleep(3000);
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'queued conga export failed');
    const sambaFile=await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()');
    assert.equal(new DataView(Uint8Array.from(sambaFile).buffer).getUint32(16+2*8,true),0x10101010);
    await play();await until('energy()===0','conga preset STOP failed');
    // Save two distinct rhythm/performance slots, replace the entire owner,
    // and recover them through the same file controls a performer uses.
    await panelButton('GLO');await panelButton('SAVE');
    await click('[aria-label="SELECT up"]');await panelButton('SAVE');
    await panelButton('REC');await panelButton('SEQ');await click('[aria-label="PRESETS up"]');
    await evaluate(`(()=>{const create=URL.createObjectURL;URL.createObjectURL=blob=>{if(blob.type==='application/json') window.showFile=blob;return create(blob);};})()`);
    await click('#save-show');await until(content('message','Show saved:'),'show export failed');
    const savedShow=await evaluate('showFile.text().then(JSON.parse)');
    if(process.env.COMP_SHOW_EXPORT)
      await writeFile(process.env.COMP_SHOW_EXPORT,JSON.stringify(savedShow),{flag:'wx'});
    const packedShow=showPayload(savedShow);
    assert.equal(packedShow.rhythms[140],1);
    assert.equal(packedShow.rhythms[151+140],2);
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'second slot rhythm export failed');
    const secondSlot=await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()');
    await play();await until('energy()>0.001','show slot produced no audio');
    await click('#save-show');await until(content('message','Press STOP'),'playing show export not refused');
    await importSet('live.cpshow',savedShow);await until(content('message','before loading another set'),'playing show import not refused');
    await until('energy()>0.001','show refusal interrupted audio');
    await play();await until('energy()===0','show test stop failed');
    await importSet('replacement.json',first);await until(content('set-name','replacement.json'),'replacement failed');
    await importSet('rehearsal.cpshow',savedShow);await until(content('set-name','rehearsal.cpshow'),'show did not restore');
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'first restored slot failed');
    assert.deepEqual(await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()'),sambaFile);
    await panelButton('HOME');await click('[aria-label="PRESETS up"]');
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'second restored slot failed');
    assert.deepEqual(await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()'),secondSlot);
    // Valid outer checksum with invalid late-slot metadata must still be
    // rejected by the fresh native owner, preserving the playing candidate.
    const badRhythms=packedShow.rhythms.slice();badRhythms[badRhythms.length-1]=1;
    await importSet('invalid.cpshow',serializeShow(packedShow.payload,badRhythms));
    await until(content('message','Current set kept'),'native show validation failed');
    assert.equal(await evaluate('document.querySelector("#set-name").textContent'),'rehearsal.cpshow');
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'rejected show damaged live owner');
    assert.deepEqual(await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()'),secondSlot);
    await importSet('percussion-bank.json',{format:1,songs:[{title:'PERCUSSION BANK',study:'mx',feel:0}]});
    await until(content('set-name','percussion-bank.json'),'percussion bank owner did not boot');
    await panelButton('REC');await panelButton('SEQ');
    for(let i=0;i<9;i++) {await click('[aria-label="PRESETS up"]');await sleep(45);}
    const percussionMasks=[
      [0,0,0x05550555,0x50005000,0],
      [0,0,0x04150555,0x50005000,0x01400000],
      [0,0,0x05550415,0x50005000,0x00000140],
      [0x45554555,0x10001000,0,0,0],
      [0x45554555,0x10001000,0x04150555,0x50005000,0x01400000],
      [0x45554555,0x10001000,0x05550415,0x50005000,0x00000140]
    ];
    for(const [index,masks] of percussionMasks.entries()) {
      if(index) {await click('[aria-label="PRESETS up"]');await sleep(45);}
      await click('#save-rhythm');await until(content('message','Rhythm file saved'),'percussion export failed');
      const bytes=await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()');
      const view=new DataView(Uint8Array.from(bytes).buffer);
      assert.deepEqual(Array.from({length:15},(_,i)=>view.getUint32(16+i*8,true)),[...masks,...Array(10).fill(0)]);
      await play();await until('energy()>0.001','percussion preset '+index+' silent');
      await play();await until('energy()===0','percussion preset STOP failed');
    }
    await panelButton('SEQ'); // Return to pads; FX still owns the same mode.
    await panelButton('FX');
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'pad-mode FX export failed');
    assert.equal(await evaluate('rhythmFile.arrayBuffer().then(b=>new Uint8Array(b)[10])'),2);
    await click('#save-show');await until(content('message','Show saved:'),'new-bank show export failed');
    const newBankShow=await evaluate('showFile.text().then(JSON.parse)');
    assert.equal(showPayload(newBankShow).rhythms[140],13);
    await importSet('new-bank.cpshow',newBankShow);
    await until(content('set-name','new-bank.cpshow'),'new-bank show did not restore');
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'new-bank restore export failed');
    const newBankBytes=await evaluate('(async()=>Array.from(new Uint8Array(await rhythmFile.arrayBuffer())))()');
    assert.equal(newBankBytes[10],2);
    assert.equal(new DataView(Uint8Array.from(newBankBytes).buffer).getUint32(16+4*8,true),0x140);
    await importSet('score-to-bank.json',{format:1,songs:[{title:'WRITTEN SCORE',study:'la-golondrina'}]});
    await until(content('set-name','score-to-bank.json'),'score-to-bank setup failed');
    await panelButton('REC');await click('[aria-label="PRESETS up"]');
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'complete groove did not leave score mode');
    assert.equal(await evaluate('rhythmFile.arrayBuffer().then(b=>new Uint8Array(b)[5])'),94);
    await play();await until('energy()>0.001','complete coast rhythm silent');
    await click('[aria-label="PRESETS up"]');await sleep(5600);
    await click('#save-rhythm');await until(content('message','Rhythm file saved'),'live complete-rhythm export failed');
    assert.equal(await evaluate('rhythmFile.arrayBuffer().then(b=>new Uint8Array(b)[5])'),101);
    await play();await until('energy()===0','complete bank STOP failed');
    await click('#save-show');await until(content('message','Choose a set slot'),'unsaved bank did not explain save destination');
    await panelButton('GLO');await panelButton('SAVE');
    await click('#save-show');await until(content('message','Show saved:'),'complete bank slot could not export');
    const completeShow=await evaluate('showFile.text().then(JSON.parse)');
    assert.equal(completeShow.set[25],1);assert.equal(completeShow.set[83],0);
    await importSet('complete-bank.cpshow',completeShow);await until(content('set-name','complete-bank.cpshow'),'complete bank show failed to reopen');
    await play();await until('energy()>0.001','complete bank reopened silently');
    await play();await until('energy()===0','complete bank reopen STOP failed');
    await panelButton('ENV');await panelButton('OCT+');
    await play();await until('energy()>0.001','transposed rhythm silent');
    await panelButton('OCT+');await sleep(5300);
    await play();await until('energy()===0','transposed rhythm STOP failed');
    await panelButton('GLO');await panelButton('SAVE');
    await click('#save-show');await until(content('message','Show saved:'),'transposed show export failed');
    const keyShow=await evaluate('showFile.text().then(JSON.parse)');
    assert.equal(keyShow.set[19+45],2);
    await importSet('key-show.cpshow',keyShow);await until(content('set-name','key-show.cpshow'),'transposed show did not reopen');
    await click('#save-show');await until(content('message','Show saved:'),'transposed show re-export failed');
    assert.equal(await evaluate('showFile.text().then(t=>JSON.parse(t).set[64])'),2);
    await importSet('clave.json',{format:1,songs:[{title:'SALSA SON',study:'salsa',bpm:180,feel:0}]});
    await until(content('set-name','clave.json'),'clave setup failed');
    await panelButton('LFO');await panelButton('OCT+');
    await play();await until('energy()>0.001','2-3 salsa silent');
    await panelButton('OCT−');await sleep(3200);
    await play();await until('energy()===0','clave STOP failed');
    await panelButton('GLO');await panelButton('SAVE');
    await click('#save-show');await until(content('message','Show saved:'),'clave show export failed');
    const claveShow=await evaluate('showFile.text().then(JSON.parse)');
    assert.equal(claveShow.set[29],0); // Live 2-3 -> 3-2 landed before STOP.
    await panelButton('LFO');await panelButton('OCT+');
    await panelButton('GLO');await panelButton('SAVE');
    await click('#save-show');await until(content('message','Show saved:'),'2-3 show export failed');
    const reverseShow=await evaluate('showFile.text().then(JSON.parse)');
    assert.equal(reverseShow.set[29],1);
    await importSet('clave.cpshow',reverseShow);await until(content('set-name','clave.cpshow'),'clave show did not reopen');
    await click('#save-show');await until(content('message','Show saved:'),'clave re-export failed');
    assert.equal(await evaluate('showFile.text().then(t=>JSON.parse(t).set[29])'),1);
    for(const study of ['cdmx','mty-dub','villera','lima','chile']) {
      const file=study+'.json';
      await importSet(file,{format:1,songs:[{title:study.toUpperCase(),study,feel:0,section:'lift',kit:true}]});
      await until(content('set-name',file),study+' did not load');
      if(study==='cdmx') {
        await panelButton('FX');
        const normal=await evaluate(canvas);
        for(let i=0;i<25;i++) {await click('[aria-label="KNOB4 down"]');await sleep(25);}
        await until(`${canvas}!==${JSON.stringify(normal)}`,'SPEED did not redraw Mix');
      }
      await play();
      await until('energy()>0.01',study+' produced no PCM');
      await play();
      await until('energy()===0',study+' did not stop');
      if(study==='cdmx') {
        await click('#save-show');await until(content('message','Show saved:'),'rebajada show export failed');
        const originalTempo=await evaluate('showFile.text().then(JSON.parse).then(s=>s.set[26])');
        assert.equal(originalTempo,100,'master speed must not rewrite the original tempo');
      }
    }
    await importSet('La-Paloma-Printed-Piano.json',{format:1,songs:[
      {title:'LA PALOMA PIANO',study:'la-paloma-piano',guide:true}]});
    await until(content('set-name','La-Paloma-Printed-Piano.json'),'full piano set did not load');
    await play();
    await until('energy()>0.01','full piano score produced no PCM');
    await play();
    await until('energy()===0','STOP did not clear the full piano score');
    if (process.env.COMP_REHEARSAL_IMPORT) {
      await importSet('Compas-Mixed-Rehearsal.json',await readFile(process.env.COMP_REHEARSAL_IMPORT,'utf8'));
      await until(content('set-name','Compas-Mixed-Rehearsal.json'),'mixed export did not import');
      await play();
      await until('energy()>0.01','mixed export was silent');
      await panelButton('GLO');
      const cueSlot=async index=>{
        const pad=`document.querySelectorAll('#keys button')[${index}]`;
        await evaluate(`${pad}.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter'}))`);
        await sleep(90);
        await evaluate(`${pad}.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter'}))`);
        await until(`${pad}.dataset.current==='true'`,'live cue to slot '+(index+1)+' did not land');
        await until('energy()>0.01','slot '+(index+1)+' was silent');
      };
      await cueSlot(3);
      await cueSlot(4);
      await play();
      await until('energy()===0','mixed set did not stop');
    }
    await importSet('La-Golondrina-Printed-Piano.json',{format:1,songs:[
      {title:'GOLONDRINA PIANO',study:'la-golondrina-piano',guide:false}]});
    await until(content('set-name','La-Golondrina-Printed-Piano.json'),'Galveston piano set did not load');
    await play();
    await until('energy()>0.01','Galveston piano produced no PCM');
    await play();
    await pick('golondrina');
    await until(content('set-name','La Golondrina'),'Galveston rehearsal did not load');
    await play();
    await until('window.boardSnapshot?.[51]===19 && window.boardSnapshot[93]===1 && energy()>0.01',
      'Galveston bolero score did not play');
    await play();
    await until('window.boardSnapshot?.[20]===0 && energy()===0','Galveston bolero STOP did not settle');
    await pick('ma-teodora');
    await until(content('set-name','1929 compound-meter rehearsal'),'Má Teodora rehearsal did not load');
    await play();
    await until('window.boardSnapshot?.[51]===20 && window.boardSnapshot[57]===2 && energy()>0.01',
      'Má Teodora compound-meter score did not play');
    await send('Page.reload');
    await until('document.querySelectorAll("#keys button").length===27 && !document.querySelector("#enable").disabled','fresh page did not boot');
    await importSet('reopened.cpshow',savedShow);await until(content('set-name','reopened.cpshow'),'show failed before enabling audio');
    await click('#enable');await until('document.querySelector("#enable").disabled','show audio did not enable');
    await evaluate(`(()=>{const el=document.querySelector('#master');el.value='74';el.dispatchEvent(new Event('input'));})()`);
    await play();await until('window.tap && energy()>0.001','reopened show produced no audio');
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,
      showBeforeEnable:true,preparedBeforeEnable:true, invalidSetKeptFactory:true, liveImportKeptPlaying:true,
      stoppedReplacement:true, pcmFromBothSets:true, tribalLiftPCM:true, printedPianoPCM:true,
      galvestonPianoPCM:true, galvestonBoleroPCM:true, compoundMeterPCM:true, mixedRehearsalImport:!!process.env.COMP_REHEARSAL_IMPORT, regionalLiftPCM:5, afroCubanPresets:6, completeGrooveBank:true, liveKeyRoundTrip:true, liveClaveRoundTrip:true, rebajadaPCM:true, rackStepEditorPCM:true, portableRhythmRoundTrip:true,portableShowRoundTrip:true,congaBankCues:true, savedSlotReachedRenderer:true}));
  } else {
  await until(`document.querySelector('#study option[value="8"]')!==null`,'score failed to load');
  await click('#play');
  await until(content('transport','PLAYING'),'PLAY did not start');
  await until('window.tap && energy()>0.01','no nonzero browser PCM');
  await until('!document.querySelector(".channel").classList.contains("on-air")','boot cameo did not clear');
  await until('document.querySelectorAll("#beat-lamps .lit").length===1','live beat indicator stayed dark');
  assert.equal(await evaluate('[...document.images].every(img=>img.complete && img.naturalWidth>0)'),true);
  if(process.env.COMP_SCREENSHOT) {
    const capture=async (name,width,height,mobile)=>{
      await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile});
      const {data}=await send('Page.captureScreenshot',{format:'png'});
      await writeFile(process.env.COMP_SCREENSHOT+'-'+name+'.png',Buffer.from(data,'base64'));
    };
    await capture('desktop',1060,960,false);
    await capture('mobile',390,844,true);
  }
  await click('#fill');
  await until(content('arrangement','FILL QUEUED'),'fill not queued');
  await click('[data-section="1"]');
  await until(content('notice','Busy'),'queue did not refuse conflicting action');
  await until(content('arrangement','· FILL')+' && !'+content('arrangement','QUEUED'),'fill did not start');
  await click('[data-section="1"]');
  await until(content('arrangement','→ LIFT'),'Lift not queued');
  await until(`document.querySelector('#arrangement').textContent==='LIFT'`,'Lift did not take effect');
  await click('#stop');
  await until(content('transport','STOPPED')+' && energy()===0','stop did not silence PCM');
  await set('#study','2');
  await until(content('study-title','SALSA SON'),'study failed');
  await until('document.querySelector(".channel").classList.contains("on-air")','study change cameo did not appear');
  await set('#clave','1');
  await until(content('readout','2–3'),'clave did not rotate');
  await set('#root','7');
  await until("document.querySelector('#root').value==='7'",'root did not apply');
  await set('#edit-part','1');
  const step='#steps button:nth-child(2)';
  const pressed=await evaluate(`document.querySelector('${step}').getAttribute('aria-pressed')`);
  await click(step);
  await until(`document.querySelector('${step}').getAttribute('aria-pressed')!==${JSON.stringify(pressed)}`,'edit did not apply');
  await click('#save');await until(content('notice','Saved here'),'save failed');
  await set('#study','3');await until(content('readout','2/4'),'conjunto did not load');
  await click('#load');await until(content('notice','Project recalled'),'recall failed');
  assert.ok(await evaluate(content('readout','2–3')));
  assert.equal(await evaluate("document.querySelector('#root').value"),'7');
  await evaluate(`(()=>{const dt=new DataTransfer();dt.items.add(new File([new Uint8Array(55)],'bad.cps'));
    const input=document.querySelector('#import');input.files=dt.files;input.dispatchEvent(new Event('change'));})()`);
  await until(content('notice','Invalid project'),'bad import not rejected');
  assert.ok(await evaluate(content('readout','2–3')));
  assert.equal(await evaluate("document.querySelector('#root').value"),'7');
  await evaluate(`(()=>{const create=URL.createObjectURL;
    URL.createObjectURL=function(blob){window.portable=blob;return create.call(this,blob);};
    const anchor=HTMLAnchorElement.prototype.click;
    HTMLAnchorElement.prototype.click=function(){if(!this.download)return anchor.call(this);};})()`);
  await click('#export');await until(content('notice','Project exported'),'export failed');
  assert.equal(await evaluate('window.portable.size'),55);
  await set('#study','4');await until(content('study-title','TEJANO CUMBIA'),'Tejano did not load');
  await evaluate(`(async()=>{const dt=new DataTransfer();dt.items.add(new File([await portable.arrayBuffer()],'compas.cps'));
    const input=document.querySelector('#import');input.files=dt.files;input.dispatchEvent(new Event('change'));})()`);
  await until(content('notice','Project opened'),'portable import failed');
  assert.ok(await evaluate(content('readout','2–3')));
  assert.equal(await evaluate("document.querySelector('#root').value"),'7');
  await click('#play');await until(content('transport','PLAYING'),'restart failed');
  await click('[aria-label="Solo Clave"]');
  await until('document.querySelector(\'[aria-label="Solo Clave"]\').getAttribute("aria-pressed")==="true"','solo failed');
  await click('[aria-label="Mute Clave"]');
  await until(content('transport','MIX SILENT')+' && energy()===0','mix silent did not silence PCM');
  assert.equal(await evaluate('document.documentElement.scrollWidth'),390);
  assert.deepEqual(errors,[]);
  console.log(JSON.stringify({browser:(await send('Browser.getVersion')).product,url,
    nonzeroPCM:true,stoppedPCMZero:true,mutePCMZero:true,queueRefusal:true,fillAndLift:true,edit:true,
    saveAndRecall:true,portableRoundTrip:true,badImportRetainedProject:true,width:390,uncaughtErrors:errors.length}));
  }
} finally {
  const closed=new Promise(resolve=>proc.once('exit',resolve));
  proc.kill();
  await Promise.race([closed,sleep(3000)]);
  await rm(profile,{recursive:true,force:true,maxRetries:4,retryDelay:100});
}
