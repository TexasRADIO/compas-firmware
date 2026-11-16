/* Offline identity contract. No MIDI, loader, transfer, or device access. */
function wrap(bytes) {
  let acc=0,bits=0;
  const wire=[0xf0];
  for (const byte of bytes) {
    acc|=byte<<bits;bits+=8;
    while(bits>=7){wire.push(acc&127);acc>>>=7;bits-=7;}
  }
  if(bits)wire.push(acc);
  wire.push(0xf7);
  return wire;
}
function frame(body) {
  return wrap([0,0x59,0x11,body.length,0,0,...body,(~body.reduce((a,b)=>a+b,0))&255]);
}
export const SHORT_QUERY=Object.freeze(frame([]));
export const FULL_QUERY=Object.freeze(frame([1]));
const exact=(a,b)=>a?.length===b.length&&b.every((n,i)=>n===a[i]);
export function identityReply(packet, fullIdentity) {
  if(!/^FM-1_[0-9]{8}$/.test(fullIdentity)) throw Error('Full bench identity required');
  const text=exact(packet,SHORT_QUERY) ? fullIdentity.slice(0,8) : exact(packet,FULL_QUERY) ? fullIdentity : null;
  if(!text)return null;
  const body=Array(27).fill(0);
  for(let i=0;i<text.length;i++)body[i]=text.charCodeAt(i);
  return frame(body);
}
export function readIdentity(wire) {
  if(wire?.length!==41||wire[0]!==0xf0||wire.at(-1)!==0xf7)return null;
  let acc=0,bits=0;
  const bytes=[];
  for(const n of wire.slice(1,-1)){
    if(!Number.isInteger(n)||n<0||n>127)return null;
    acc|=n<<bits;bits+=7;
    if(bits>=8){bytes.push(acc&255);acc>>>=8;bits-=8;}
  }
  if(acc||bytes.length!==34||!exact(bytes.slice(0,6),[0,0x59,0x11,27,0,0]))return null;
  const body=bytes.slice(6,33);
  if(bytes[33]!==((~body.reduce((a,b)=>a+b,0))&255))return null;
  const text=String.fromCharCode(...body).replace(/\0+$/,'');
  return /^FM-1_([0-9]{3}|[0-9]{8})$/.test(text) ? text : null;
}
export function verifyIdentities(shortReply,fullReply,expected) {
  return /^FM-1_[0-9]{8}$/.test(expected) && readIdentity(shortReply)===expected.slice(0,8)
    && readIdentity(fullReply)===expected;
}
