// Replay the retained BB1 host response loop against a virtual request stream.
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';

const pins = {
  'midi-wire.mjs': '3dd353e6f2cd68ddf285d843a70b59115c320d1c01eb155a855ef590f5906946',
  'serve-image.mjs': 'a2b97cdfc34f7e513c8e20d3b9e31765933650a3d0befb9a614e27d1848c8e6b',
  'storage-layout.mjs': '3eb1bfbeb88261aa7a78fd21ca2aed0a6a81d740235e8de642d2cdb3c13c1961',
};
const folder = process.argv[2];
if (!folder) throw new Error('Supply the retained BB1 release-installer directory');
for (const [file, expected] of Object.entries(pins)) {
  const actual = createHash('sha256').update(readFileSync(resolve(folder, file))).digest('hex');
  if (actual !== expected) throw new Error('BB1 installer digest mismatch for ' + file);
}
if (process.argv[3] === '--layout') {
  const {STORAGE_LAYOUT, USER_REGIONS} = await import(pathToFileURL(resolve(folder, 'storage-layout.mjs')).href);
  process.stdout.write(JSON.stringify({id: STORAGE_LAYOUT, regions: USER_REGIONS}));
  process.exit(0);
}
const {serveImage} = await import(pathToFileURL(resolve(folder, 'serve-image.mjs')).href);
const {FINISH_WRITE} = await import(pathToFileURL(resolve(folder, 'midi-wire.mjs')).href);
const input = JSON.parse(readFileSync(0, 'utf8'));
const image = Buffer.from(input.image, 'hex');
const requests = input.requests.map(frame => Buffer.from(frame, 'hex'));
const replies = [];
let index = 0, clock = 0;
const result = await serveImage({
  image,
  finish: FINISH_WRITE,
  now: () => clock,
  sleep: async ms => { clock += ms; },
  assertIdentity: () => {}, // This replay has one immutable, in-process peer.
  link: {
    read() {
      if (index === requests.length) throw new Error('Request trace ended before completion');
      return requests[index++];
    },
    send(reply) {
      replies.push(Buffer.from(reply).toString('hex'));
      return true;
    },
  },
});
if (!result.finished || index !== requests.length || replies.length !== requests.length)
  throw new Error('BB1 host replay did not consume the full request trace');
process.stdout.write(JSON.stringify({replies, pins}));
