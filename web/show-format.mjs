// Version 1 wraps the unchanged song payload plus sixteen rhythm/performance
// records. The native owner validates musical fields before replacing a show.
const showSlots = 16;
const songBytes = 85;
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
function showPayload(data) {
  if(!data || typeof data!=='object' ||
     Object.keys(data).sort().join(',')!=='crc32,format,rhythms,set,version' ||
     data.format!=='compas-show' || data.version!==1)
    throw Error('Unsupported show format.');
  const payload=showBytes(data.set,showSlots*songBytes),rhythms=showBytes(data.rhythms,showSlots*rhythmSlotBytes);
  if(!Number.isInteger(data.crc32) || data.crc32!==showCRC(payload,rhythms))
    throw Error('The show file is damaged.');
  return {payload,rhythms};
}
function serializeShow(set,rhythms) {
  const data={format:'compas-show',version:1,set:Array.from(set),rhythms:Array.from(rhythms),
    crc32:showCRC(set,rhythms)};
  showPayload(data);
  return data;
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
