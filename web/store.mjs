const key = 'compas-private-v1';
function active(storage) {
  const slot=storage.getItem(key);
  if(slot!==null && slot!=='a' && slot!=='b') throw Error('Save pointer invalid; use Open project.');
  return slot;
}
export function recall(storage) {
  const slot=active(storage);
  if(slot===null) throw Error('No project saved here.');
  const stored=storage.getItem(key+'-'+slot);
  if(!stored || !/^[\da-f]{110}$/.test(stored)) throw Error('Saved project unreadable.');
  return Uint8Array.from(stored.match(/../g),x=>parseInt(x,16));
}
export function save(storage, bytes) {
  if(!(bytes instanceof Uint8Array) || bytes.byteLength!==55) throw Error('Invalid snapshot.');
  const slot=active(storage)==='a' ? 'b' : 'a', item=key+'-'+slot;
  const encoded=Array.from(bytes,x=>x.toString(16).padStart(2,'0')).join('');
  storage.setItem(item,encoded);
  if(storage.getItem(item)!==encoded) throw Error('Save did not verify.');
  storage.setItem(key,slot);
  if(storage.getItem(key)!==slot) throw Error('Save did not commit.');
}
