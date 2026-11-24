// SPDX-License-Identifier: GPL-3.0-only
// compas-song/2: the readable form of a Compás show (.cpshow), for people
// and coding agents to read, write and diff. Browser-only: the unit never
// sees it. Text -> bytes is exact; every CRC is recomputed. A slot whose
// bytes this format can't express exactly is written as a `raw` line, so
// no show ever loses a byte on the way through.
import {serializeShow, showPayload} from './show-format.mjs';

const SLOTS = 16, SONG = 86, RHYTHM = 151, C7R = 140, PROJECT = 62;
const ROLES = ['low', 'skin', 'hand', 'shake', 'scrape', 'lead'];
const SOUNDS = ['high-bongo', 'low-bongo', 'muted-hi-conga', 'open-hi-conga', 'low-conga',
  'high-timbale', 'low-timbale', 'high-agogo', 'low-agogo', 'cabasa', 'maracas',
  'short-whistle', 'long-whistle', 'quijada', 'star-chime'];
const ACCENTED = 'ÁÉÍÓÚÑÜ', MARKS = 'aeiounv';   // the unit stores Á as 'a', and so on

function crc32(bytes, end) {
  let crc = 0xffffffff;
  for (let i = 0; i < end; ++i) {
    crc ^= bytes[i];
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
const u32 = (b, at) => (b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) >>> 0;
function put32(b, at, value) { for (let i = 0; i < 4; ++i) b[at + i] = (value >>> (8 * i)) & 255; }
const hex = bytes => Array.from(bytes, x => x.toString(16).padStart(2, '0')).join('');
function unhex(text, size) {
  if (!/^[0-9a-f]*$/.test(text) || text.length !== size * 2) throw Error(`Expected ${size} bytes of hex.`);
  return Uint8Array.from(text.match(/../g) || [], pair => parseInt(pair, 16));
}
function grid(hits, accents, steps) {
  let line = '';
  for (let s = 0; s < steps; ++s) line += (accents >>> s) & 1 ? 'X' : (hits >>> s) & 1 ? 'x' : '.';
  return line;
}
function ungrid(line, steps) {
  if (line.length !== steps || /[^xX.]/.test(line)) throw Error(`A ${steps}-step grid uses x, X and . only.`);
  let hits = 0, accents = 0;
  for (let s = 0; s < steps; ++s) {
    if (line[s] !== '.') hits |= 1 << s;
    if (line[s] === 'X') accents |= 1 << s;
  }
  return [hits >>> 0, accents >>> 0];
}
function grid48(bytes, at, steps) {
  let line='';
  for(let step=0;step<steps;++step) line+=(bytes[at+(step>>3)]>>(step&7))&1?'x':'.';
  return line;
}
function ungrid48(line, steps) {
  if(line.length!==steps || /[^xX.]/.test(line)) throw Error(`A ${steps}-step grid uses x, X and . only.`);
  const bytes=new Uint8Array(6);
  for(let step=0;step<steps;++step) if(line[step]!=='.') bytes[step>>3]|=1<<(step&7);
  return bytes;
}
function titleText(bytes) {
  const end = bytes.indexOf(0), chars = bytes.subarray(0, end < 0 ? 19 : end);
  return String.fromCharCode(...chars).replace(/[aeiounv]/g, c => ACCENTED[MARKS.indexOf(c)]);
}
function titleBytes(text) {
  const out = new Uint8Array(19);
  const raw = [...text].map(c => ACCENTED.includes(c) ? MARKS[ACCENTED.indexOf(c)] : c);
  if (raw.length > 19 || raw.some(c => c.charCodeAt(0) > 126)) throw Error(`Title too long or not printable: ${text}`);
  raw.forEach((c, i) => { out[i] = c.charCodeAt(0); });
  return out;
}

// ---- writing --------------------------------------------------------------
function songLines(song) {
  const p = song.subarray(19, 19 + PROJECT), t = song.subarray(19 + PROJECT);
  const options=t[3]|t[4]<<8, assignments=[];
  for(let role=0;role<6;++role) assignments.push((t[role>>1]>>(4*(role&1)))&15);
  const lines = [
    `  groove ${p[6]}  bpm ${p[7]}  feel ${p[8]}  root ${p[57]}  clave ${p[10]}  seed ${u32(p, 11)}`,
    `  kit ${options&1}  energy ${options>>1&3}  phrases ${options>>3&127}  score ${options>>10&31}  guide ${options>>15}`,
    `  players ${assignments.join(' ')}`];
  for (let r = 0; r < 6; ++r)
    lines.push(`  role ${ROLES[r].padEnd(6)} level ${String(p[15 + r]).padStart(3)}  ${(p[9] >> r) & 1 ? 'muted' : 'on   '}  ${grid48(p,21+6*r,48)}`);
  return lines;
}
function rhythmLines(slot) {
  const c = slot.subarray(0, C7R), x = slot.subarray(C7R);
  const lines = [`  band origin ${x[0]}  bass ${x[1]} ${x[2]} ${x[3]}  overlays ${x[4]}  levels ${Array.from(x.subarray(5, 9)).join(' ')}  accent ${x[9]}  part ${x[10]}`];
  if (c.every(b => b === 0)) return [...lines, '  727 none'];
  const steps = c[4];
  lines.push(`  727 steps ${steps}  bpm ${c[5]}  feel ${c[6]}  hit ${c[7]}  accent ${c[8]}  level ${c[9]}  mode ${c[10]}  sound ${c[11]}`);
  for (let i = 0; i < 15; ++i)
    lines.push(`    ${SOUNDS[i].padEnd(15)} ${grid(u32(c, 16 + 8 * i), u32(c, 20 + 8 * i), steps)}`);
  return lines;
}

/** {payload, rhythms} (from showPayload) -> compas-song/2 text */
export function showToText({payload, rhythms}) {
  const out = ['compas-song/2',
    '# A Compás show. Steps: X accent, x hit, . rest. Lines starting # are notes.',
    '# Role lanes are 48 steps; 16- and 32-step grooves use the first steps.'];
  for (let i = 0; i < SLOTS; ++i) {
    const song = payload.subarray(i * SONG, (i + 1) * SONG), slot = rhythms.subarray(i * RHYTHM, (i + 1) * RHYTHM);
    let block;
    if (song[0] === 127 && song.subarray(1).every(b => b === 0) && slot.subarray(0, C7R).every(b => b === 0))
      block = [`slot ${i + 1} hidden`, `  band-raw ${hex(slot.subarray(C7R))}`];
    else block = [`slot ${i + 1} "${titleText(song.subarray(0, 19))}"`, ...songLines(song), ...rhythmLines(slot)];
    try {          // prove this block round-trips, or fall back to raw bytes
      const back = parseSlot(block);
      if (back.song.some((b, k) => b !== song[k]) || back.slot.some((b, k) => b !== slot[k])) throw Error();
    } catch {
      block = [`slot ${i + 1} raw`, `  song ${hex(song)}`, `  band ${hex(slot)}`];
    }
    out.push('', ...block);
  }
  return out.join('\n') + '\n';
}

// ---- reading --------------------------------------------------------------
function fields(line, names) {
  const words = line.trim().split(/\s+/), out = {};
  for (let i = 0; i < words.length; ++i) {
    if (!names.includes(words[i])) throw Error(`Unexpected "${words[i]}" in: ${line.trim()}`);
    const n = names[words[i]] ?? 1; out[words[i]] = words.slice(i + 1, i + 1 + n).map(Number); i += n;
  }
  for (const name of names) if (!(name in out)) throw Error(`Missing "${name}" in: ${line.trim()}`);
  return out;
}
const byte = (v, what) => { if (!Number.isInteger(v) || v < 0 || v > 255) throw Error(`${what} must be 0–255.`); return v; };

function parseSlot(lines) {
  const head = lines[0].trim(), song = new Uint8Array(SONG), slot = new Uint8Array(RHYTHM);
  const body = lines.slice(1).map(l => l.trim()).filter(Boolean);
  if (/^slot \d+ raw$/.test(head)) {
    song.set(unhex(body.find(l => l.startsWith('song ')).slice(5), SONG));
    slot.set(unhex(body.find(l => l.startsWith('band ')).slice(5), RHYTHM));
    return {song, slot};
  }
  if (/^slot \d+ hidden$/.test(head)) {
    song[0] = 127;
    slot.set(unhex(body.find(l => l.startsWith('band-raw ')).slice(9), RHYTHM - C7R), C7R);
    return {song, slot};
  }
  const title = head.match(/^slot \d+ "(.*)"$/);
  if (!title) throw Error(`Bad slot line: ${head}`);
  song.set(titleBytes(title[1]));
  const p = new Uint8Array(PROJECT), t = song.subarray(19 + PROJECT);
  put32(p, 0, 0x31535043); p[4] = 2; p[5] = 1;
  const g = fields(body[0], Object.assign(['groove', 'bpm', 'feel', 'root', 'clave', 'seed']));
  [p[6], p[7], p[8], p[57], p[10]] = ['groove', 'bpm', 'feel', 'root', 'clave'].map(k => byte(g[k][0], k));
  put32(p, 11, g.seed[0] >>> 0);
  const k = fields(body[1], ['kit', 'energy', 'phrases', 'score', 'guide']);
  const options=byte(k.kit[0],'kit')|(byte(k.energy[0],'energy')<<1)|(byte(k.phrases[0],'phrases')<<3)|
    (byte(k.score[0],'score')<<10)|(byte(k.guide[0],'guide')<<15);
  t[3]=options&255;t[4]=options>>8;
  const players = body[2].split(/\s+/);
  if (players[0] !== 'players' || players.length !== 7) throw Error('players needs six numbers.');
  players.slice(1).forEach((v, i) => { t[i>>1] |= byte(Number(v), 'player') << (4*(i&1)); });
  for (let r = 0; r < 6; ++r) {
    const m = body[3 + r].match(/^role (\S+)\s+level\s+(\d+)\s+(muted|on)\s+([xX.]{32}|[xX.]{48})$/);
    if (!m || m[1] !== ROLES[r]) throw Error(`Expected role ${ROLES[r]}: ${body[3 + r]}`);
    p[15 + r] = byte(Number(m[2]), 'level');
    if (m[3] === 'muted') p[9] |= 1 << r;
    p.set(ungrid48(m[4].replace(/X/g, 'x'),m[4].length),21+6*r);
  }
  put32(p, PROJECT - 4, crc32(p, PROJECT - 4));
  song.set(p, 19);
  const b = body[9].split(/\s+/);
  const want = ['band', 'origin', , 'bass', , , , 'overlays', , 'levels', , , , , 'accent', , 'part'];
  if (b.length !== 18 || want.some((w, i) => w && b[i] !== w)) throw Error(`Bad band line: ${body[9]}`);
  [b[2], b[4], b[5], b[6], b[8], b[10], b[11], b[12], b[13], b[15], b[17]]
    .forEach((v, i) => { slot[C7R + i] = byte(Number(v), 'band value'); });
  if (body[10] === '727 none') return {song, slot};
  const r = fields(body[10].replace(/^727 /, ''), ['steps', 'bpm', 'feel', 'hit', 'accent', 'level', 'mode', 'sound']);
  const c = slot.subarray(0, C7R);
  c.set([67, 55, 82, 49]);   // C7R1
  ['steps', 'bpm', 'feel', 'hit', 'accent', 'level', 'mode', 'sound'].forEach((key, i) => { c[4 + i] = byte(r[key][0], key); });
  if (c[4] !== 16 && c[4] !== 32) throw Error('727 steps must be 16 or 32.');
  for (let i = 0; i < 15; ++i) {
    const m = (body[11 + i] || '').match(/^(\S+)\s+([xX.]+)$/);
    if (!m || m[1] !== SOUNDS[i]) throw Error(`Expected 727 lane ${SOUNDS[i]}.`);
    const [hits, accents] = ungrid(m[2], c[4]);
    put32(c, 16 + 8 * i, hits); put32(c, 20 + 8 * i, accents);
  }
  put32(c, 136, crc32(c, 136));
  return {song, slot};
}

/** compas-song/1 or /2 text -> a current .cpshow object, ready for showPayload/JSON. */
export function textToShow(text) {
  const lines = text.replace(/\r/g, '').split('\n').filter(l => !l.trimStart().startsWith('#'));
  if (!['compas-song/1','compas-song/2'].includes(lines[0].trim())) throw Error('Not a compas-song/1 or /2 file.');
  const blocks = [];
  for (const line of lines.slice(1)) {
    if (/^slot /.test(line)) blocks.push([line]);
    else if (line.trim()) { if (!blocks.length) throw Error(`Text before the first slot: ${line}`); blocks.at(-1).push(line); }
  }
  if (blocks.length !== SLOTS) throw Error(`A show has ${SLOTS} slots; this file has ${blocks.length}.`);
  const set = new Uint8Array(SLOTS * SONG), rhythms = new Uint8Array(SLOTS * RHYTHM);
  blocks.forEach((block, i) => {
    if (Number(block[0].split(/\s+/)[1]) !== i + 1) throw Error(`Slots must run 1–16 in order (found ${block[0]}).`);
    const {song, slot} = parseSlot(block);
    set.set(song, i * SONG); rhythms.set(slot, i * RHYTHM);
  });
  return serializeShow(set, rhythms);
}

export {showPayload, parseSlot as _parseSlot, songLines as _songLines, rhythmLines as _rhythmLines, titleText as _titleText};
