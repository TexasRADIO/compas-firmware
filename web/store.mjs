const key = 'compas-private-v1';
function crc32(bytes, end) {
  let crc=0xffffffff;
  for(let i=0;i<end;++i) {
    crc^=bytes[i];
    for(let bit=0;bit<8;++bit) crc=(crc>>>1)^(crc&1?0xedb88320:0);
  }
  return (crc^0xffffffff)>>>0;
}
function migrateV1(project) {
  const current=new Uint8Array(62);
  current.set(project.subarray(0,21));current[4]=2;
  for(let role=0;role<6;++role)
    current.set(project.subarray(21+4*role,25+4*role),21+6*role);
  current[57]=project[45];
  new DataView(current.buffer).setUint32(58,crc32(current,58),true);
  return current;
}
function active(storage) {
  const slot=storage.getItem(key);
  if(slot!==null && slot!=='a' && slot!=='b') throw Error('Save pointer invalid; use Open project.');
  return slot;
}
export function recall(storage) {
  const slot=active(storage);
  if(slot===null) throw Error('No project saved here.');
  const stored=storage.getItem(key+'-'+slot);
  if(!stored || !/^[\da-f]{110}$|^[\da-f]{124}$/.test(stored)) throw Error('Saved project unreadable.');
  const bytes=Uint8Array.from(stored.match(/../g),x=>parseInt(x,16));
  return bytes.byteLength===55?migrateV1(bytes):bytes;
}
export function save(storage, bytes) {
  if(!(bytes instanceof Uint8Array) || bytes.byteLength!==62) throw Error('Invalid snapshot.');
  const slot=active(storage)==='a' ? 'b' : 'a', item=key+'-'+slot;
  const encoded=Array.from(bytes,x=>x.toString(16).padStart(2,'0')).join('');
  storage.setItem(item,encoded);
  if(storage.getItem(item)!==encoded) throw Error('Save did not verify.');
  storage.setItem(key,slot);
  if(storage.getItem(key)!==slot) throw Error('Save did not commit.');
}
