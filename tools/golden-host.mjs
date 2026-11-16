// Replay every golden session natively (tests/golden-replay.c) and check each
// framebuffer against tests/golden-frames.json. Run tools/golden-frames.mjs
// first: it writes build/golden-trace.txt from the compiled browser board.
// Usage: node tools/golden-host.mjs build/standin/golden-replay [...]
//        node tools/golden-host.mjs --screens DIR [--record]
//   checks (or records) tests/golden-stage-screens.json: SHA-256 of each PPM
//   that tests/stage-host.c draws, the stage-only states the bench can't reach.
import {createHash} from 'node:crypto';
import {readFileSync,readdirSync,writeFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';

if(process.argv[2]==='--screens') {
  const dir=process.argv[3], file=new URL('../tests/golden-stage-screens.json',import.meta.url);
  const got=Object.fromEntries(readdirSync(dir).filter(f=>f.endsWith('.ppm')).sort()
    .map(f=>[f.slice(0,-4),createHash('sha256').update(readFileSync(`${dir}/${f}`)).digest('hex')]));
  if(process.argv.includes('--record')) {
    writeFileSync(file,JSON.stringify(got,null,1)+'\n');
    console.log(`Recorded ${Object.keys(got).length} stage screens.`); process.exit(0);
  }
  const want=JSON.parse(readFileSync(file));
  const diff=Object.keys({...want,...got}).filter(k=>want[k]!==got[k]);
  if(diff.length) { console.error('Stage screens differ:',diff.join(', ')); process.exit(1); }
  console.log(`Stage screens identical: ${Object.keys(got).length} states.`); process.exit(0);
}

const want=JSON.parse(readFileSync(new URL('../tests/golden-frames.json',import.meta.url)));
const lines=readFileSync(new URL('../build/golden-trace.txt',import.meta.url),'utf8').trim().split('\n');
const binaries=process.argv.slice(2);
if(!binaries.length) { console.error('usage: golden-host.mjs <golden-replay binary>...'); process.exit(2); }
let failed=0;
for(const binary of binaries) {
  const seen=new Set(), bad=[];
  for(const line of lines) {
    const name=line.slice(0,line.indexOf(' '));
    const run=spawnSync(binary,{input:line+'\n',maxBuffer:1<<24});
    if(run.status!==0) { bad.push(`${name} (exit ${run.status}: ${run.stderr})`); continue; }
    if(run.stdout.length!==240*240*2) { bad.push(`${name} (${run.stdout.length} bytes)`); continue; }
    seen.add(name);
    if(createHash('sha256').update(run.stdout).digest('hex')!==want[name]) bad.push(name);
  }
  const missing=Object.keys(want).filter(k=>!seen.has(k)&&!bad.some(b=>b.startsWith(k)));
  if(bad.length||missing.length) { failed=1; console.error(`${binary}: differs from wasm golden: ${[...bad,...missing].join(', ')}`); }
  else console.log(`${binary}: ${seen.size} native frames identical to the wasm golden frames.`);
}
process.exit(failed);
