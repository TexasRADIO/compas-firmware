// Interpret the board's published Panel / BoardView / PanelKeys snapshot.
// Musical key numbers and masks remain owned by panel.c; this adds browser labels.
const computerKeys=['Z','X','C','V','B','N','M','A','W','S','E','D','F','T','G','Y','H','U','J','K','I','L','O','P',';','[',']'];
const whiteKeys=[0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26];
const partKeys=[7,9,11,12,14,16], layerKeys=[8,10,13,15,17];
const drumNames=['High bongo','Low bongo','Muted high conga','Open high conga','Low conga','High timbale','Low timbale','High agogo','Low agogo','Cabasa','Maracas','Short whistle','Long whistle','Quijada','Star chime','Accent'];
const drumLabels=['H B','L B','M C','O C','L C','H T','L T','H A','L A','CAB','MAR','S W','L W','QUI','CHM','ACC'];
const drumColors=['#ff8914','#ff8914','#ff2e88','#ff2e88','#ff2e88','#ffe600','#ffe600','#17d4ee','#17d4ee','#43ff91','#43ff91','#8cbbff','#8cbbff','#ef7373','#b46bff','#fff8ec'];
function keyPresentation(bytes,catalog) {
  if(bytes.length!==128 && bytes.length!==136) throw Error('Unsupported board display snapshot');
  const d=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const page=bytes[0], bar=bytes[2], edit=bytes[18], v=bytes.subarray(20,96);
  const [parts,upper,pads,active,queued,enabled]=Array.from({length:6},(_,i)=>d.getUint32(bytes.length-32+i*4,true));
  const study=catalog[v[1]], score=catalog.find(s=>s.piano===v[31]);
  const band=[0,5,6].includes(page), rack=page===3, steps=rack&&edit;
  const list=computerKeys.map((shortcut,i)=>{
    const bit=1<<i;
    let kind='unused',label='—',name='Unused',color='#6b5c7a',on=false,current=false,next=false;
    const drum=index=>{kind='drum';label=drumLabels[index];name=drumNames[index];color=drumColors[index];};
    if(band) {
      if(parts&bit) {
        const n=partKeys.indexOf(i);kind='part';
        [name,label]=study.conductorParts[n];
        if(v[31]) {
          const cielito=score?.id==='cielito-lindo-piano';
          const cucaracha=score?.id==='la-cucaracha-piano';
          label={0:cielito?'PICK':cucaracha?'BEG':'INT',1:'VERSE',3:'CHO',5:cielito?'END':'CODA'}[n];
          name={0:cielito?'Pickup':cucaracha?'Start':'Intro',1:'Verse',3:'Chorus',5:cielito?'Ending':'Coda'}[n];
          if(score?.id==='jurame-schirmer-refrain') {
            label={0:'BEG',1:'REF',3:'ANS',5:'CAD'}[n];
            name={0:'Start',1:'Refrain',3:'Answer',5:'Cadence'}[n];
          }
          if(score?.id==='ma-teodora-1929') {
            label={0:'PICK',1:'CALL',3:'ECH',5:'END'}[n];
            name={0:'Pickup',1:'Call',3:'Echoes',5:'Close'}[n];
          }
        }
        current=!!(active&bit);next=!!(queued&bit);
      } else if(upper&bit) {
        const n=layerKeys.indexOf(i);kind='layer';
        label=v[31]?['GUIDE','HOLD','BOL'][n]:['CON','MAR','TIM','BEL','BAS'][n];
        name=v[31]?['Melody guide','Hold current phrase','Bolero percussion'][n]:['Conga layer','Maraca layer','Timbal layer','Bell layer','Bass'][n];
        on=!!(enabled&bit);
      } else if(pads&bit) drum(i<7?i:i-11);
    } else if(rack && i<16 && !(steps&&v[31])) {
      if(steps) {
        kind='step';label=String(bar+i+1);name=`Step ${bar+i+1}`;
        on=!!(enabled&bit);current=!!(active&bit);next=!!(queued&bit);
      } else {drum(i);current=!!(active&bit);on=!!(enabled&bit);}
    } else if(page===7 && i<8) {
      const names=['Bombo','Güiro','Campana','Conga','White pattern','Yellow pattern','Red pattern','Green pattern'];
      const labels=['BOM','GUI','CAM','CON','WHT','YEL','RED','GRN'];
      const colors=['#ffe600','#43ff91','#fff8ec','#ff2e88','#fff8ec','#ffe600','#ff2e88','#43ff91'];
      kind=i<4?'drum':'slot';label=labels[i];name=names[i];color=colors[i];
      current=!!(active&bit);next=!!(queued&bit);
    } else if(page===4 && i<16) {
      label=String(i+1);
      kind=pads&bit?'slot':'unused';
      name=`${kind==='slot'?'Song':'Empty song'} slot ${i+1}`;
      current=!!(active&bit);next=!!(queued&bit);
    } else if(page===2 && i<(study?.meter===2?8:16)) {
      kind='step';label=String(bar+i+1);name=`Step ${bar+i+1}`;
      on=!!(d.getUint32(60,true)&(1<<(bar+i)));
      current=!!v[0]&&v[5]===bar+i;
    } else if(page===1 && !v[31] && i<12) {
      const role=i%6, roleName=study?.parts?.[role]?.[0]||`Voice ${role+1}`;
      kind='layer';label=(i<6?'M':'S')+(role+1);name=(i<6?'Mute ':'Solo ')+roleName;
      on=i<6?!!(v[8]&(1<<role)):v[9]===role;
    }
    const state=kind==='step'?(on?'hit':'empty')+(next?', accented':''):
      next?'queued':current?(v[0]?'playing':'selected'):kind==='layer'?(on?'on':'off'):'';
    return {shortcut,kind,label,name,color,on,current,next,state};
  });
  const scoreHint='White keys: song sections. '+(v[38]?'Upper keys: guide and hold. ':'Second upper key: hold. ')+(v[73]?'Third upper key: bolero. ':'')+'Drums at either side.';
  const hint=band?(v[31]?scoreHint:'Six centre white keys: song parts. Upper keys: layers and bass. Drums at either side.'):
    steps?'First 16 keys: steps. KNOB1 chooses the sound. OCT− / OCT+ changes bar.':
    rack?(v[31]?'First 15 keys: live 727 sounds. Key 16: accent.':
      'First 15 keys: 727 sounds. ARP rolls, REC records the selected lane and SEQ edits steps. Key 16: accent.'):
    page===7?'Keys 1–4 play the kit. Keys 5–8 or PRESETS cue patterns. FX switches Off, Layer or Drums Only.':
    page===4?'First 16 keys: song slots. Yellow is current; pink is queued.':
    page===2?'First keys: steps for the selected instrument. OCT− / OCT+ changes bar.':
    'First six keys: instrument mutes. Next six: solos.';
  return {keys:list,hint,song:v[28],nextSong:v[29]};
}
export {keyPresentation,computerKeys,whiteKeys};
