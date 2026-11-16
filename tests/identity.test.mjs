import {test} from 'node:test';
import assert from 'node:assert/strict';
import {SHORT_QUERY,FULL_QUERY,identityReply,readIdentity,verifyIdentities} from '../tools/identity.mjs';

test('plain query gives short model family, explicit request gives full identity',()=>{
  const mock='FM-1_99901001'; // Synthetic fixture only, never assigned to a device.
  assert.deepEqual(SHORT_QUERY,[0xf0,0,0x32,0x45,0,0,0,0x40,0x7f,0xf7]);
  assert.deepEqual(FULL_QUERY,[0xf0,0,0x32,0x45,0x08,0,0,0x40,0,0x7e,0x01,0xf7]);
  const short=identityReply(SHORT_QUERY,mock),full=identityReply(FULL_QUERY,mock);
  assert.equal(readIdentity(short),'FM-1_999');
  assert.equal(readIdentity(full),mock);
  assert.equal(verifyIdentities(short,full,mock),true);
  for(const [a,b] of [[short,short],[full,full],[short,null],[short,identityReply(FULL_QUERY,'FM-1_99901002')]])
    assert.equal(verifyIdentities(a,b,mock),false);
  full[38]^=1;assert.equal(readIdentity(full),null);
  const malformed=FULL_QUERY.slice();malformed[9]^=1;
  assert.equal(identityReply(malformed,mock),null);
  assert.equal(identityReply([0xf0,0x22,0x24,0x35,0x7f,0xf7],mock),null);
});
