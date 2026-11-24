// Version 2 wraps the 48-step-capable song payload plus sixteen
// rhythm/performance records. The native owner validates musical fields before
// replacing a show.
const showSlots = 16;
const songBytes = 86;
const legacySongBytes = 85;
const rhythmSlotBytes = 151;

function showCRC(set, rhythms) {
  let crc=0xffffffff;
  for(const bytes of [set,rhythms]) for(const byte of bytes) {
    crc^=byte;
    for(let bit=0;bit<8;++bit) crc=(crc>>>1)^(crc&1?0xedb88320:0);
  }
  return (crc^0xffffffff)>>>0;
}
function showBytes(value,size) {
  if(!Array.isArray(value) || value.length!==size ||
     value.some(x=>!Number.isInteger(x) || x<0 || x>255))
    throw Error('Invalid show data.');
  return Uint8Array.from(value);
}
function crc32(bytes) {
  let crc=0xffffffff;
  for(const byte of bytes) { crc^=byte;for(let bit=0;bit<8;++bit) crc=(crc>>>1)^(crc&1?0xedb88320:0); }
  return (crc^0xffffffff)>>>0;
}
function migrateLegacySet(oldSet) {
  const set=new Uint8Array(showSlots*songBytes);
  for(let slot=0;slot<showSlots;++slot) {
    const old=oldSet.subarray(slot*legacySongBytes,(slot+1)*legacySongBytes);
    const song=set.subarray(slot*songBytes,(slot+1)*songBytes);
    song.set(old.subarray(0,19));
    if(!old[0]) continue;
    if(old[0]===127) continue;
    const source=old.subarray(19,74),project=song.subarray(19,81);
    project.set(source.subarray(0,21));project[4]=2;
    for(let role=0;role<6;++role) project.set(source.subarray(21+4*role,25+4*role),21+6*role);
    project[57]=source[45];
    new DataView(project.buffer,project.byteOffset,project.byteLength).setUint32(58,crc32(project.subarray(0,58)),true);
    for(let role=0;role<6;++role) song[81+(role>>1)]|=old[74+role]<<(4*(role&1));
    const options=old[80]|old[81]<<1|old[82]<<3|old[83]<<10|old[84]<<15;
    song[84]=options&255;song[85]=options>>8;
  }
  return set;
}
function showPayload(data) {
  if(!data || typeof data!=='object' ||
     Object.keys(data).sort().join(',')!=='crc32,format,rhythms,set,version' ||
     data.format!=='compas-show' || ![1,2].includes(data.version))
    throw Error('Unsupported show format.');
  const legacy=data.version===1;
  if(legacy!== (Array.isArray(data.set) && data.set.length===showSlots*legacySongBytes))
    throw Error('Show version and record layout disagree.');
  const original=showBytes(data.set,legacy?showSlots*legacySongBytes:showSlots*songBytes),rhythms=showBytes(data.rhythms,showSlots*rhythmSlotBytes);
  if(!Number.isInteger(data.crc32) || data.crc32!==showCRC(original,rhythms))
    throw Error('The show file is damaged.');
  const payload=legacy?migrateLegacySet(original):original;
  return {payload,rhythms};
}
function serializeShow(set,rhythms) {
  const legacy=bytes=>bytes.byteLength===showSlots*legacySongBytes;
  const data={format:'compas-show',version:legacy(set)?1:2,set:Array.from(set),rhythms:Array.from(rhythms),crc32:showCRC(set,rhythms)};
  const normalized=showPayload(data);
  const output={format:'compas-show',version:2,set:Array.from(normalized.payload),rhythms:Array.from(rhythms),
    crc32:showCRC(normalized.payload,rhythms)};
  showPayload(output);return output;
}
// A portable snapshot materializes every visible song and marks empty slots.
function showSongSlots(payload) {
  const slots=[];
  for(let slot=0;slot<showSlots;++slot) {
    const first=payload[slot*songBytes];
    if(!first) throw Error('Save this show on the instrument before reordering it.');
    if(first!==127) slots.push(slot);
  }
  if(!slots.length) throw Error('The show has no songs.');
  return slots;
}

function showSongTitle(payload, slot) {
  return decodeSongTitle(payload.subarray(slot*songBytes,slot*songBytes+18));
}
function decodeSongTitle(title) {
  const end=title.indexOf(0);
  return String.fromCharCode(...(end<0 ? title : title.subarray(0,end)))
    .replace(/[aeiounv]/g,char=>'ÁÉÍÓÚÑÜ'['aeiounv'.indexOf(char)]);
}

function reorderShow({payload,rhythms}, order) {
  const slots=showSongSlots(payload);
  if(order.length!==slots.length || new Set(order).size!==slots.length ||
      !order.every(slot=>slots.includes(slot))) throw Error('Invalid song order.');
  const set=payload.slice(), parts=rhythms.slice();
  // Move the full pair, including edits unknown to the running-order editor.
  for(const [index,slot] of slots.entries()) {
    const from=order[index];
    set.set(payload.subarray(from*songBytes,(from+1)*songBytes),slot*songBytes);
    parts.set(rhythms.subarray(from*rhythmSlotBytes,(from+1)*rhythmSlotBytes),slot*rhythmSlotBytes);
  }
  return serializeShow(set,parts);
}

export {showPayload, serializeShow, showSongSlots, showSongTitle, reorderShow};
