import {normalizeSet} from './set-format.mjs';

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit)
      crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

// Match the board's v2 packed Song records. The native launcher adds
// the NOR header, journal and marker around this payload.
function preparedPayload(data, catalog) {
  const songs = normalizeSet(data, catalog);
  const projectBytes=62, songBytes=86;
  const payload = new Uint8Array(16 * songBytes);
  const accent = Object.fromEntries([...'ÁÉÍÓÚÑÜ'].map((letter, index) => [letter, 'aeiounv'[index]]));
  for (let slot = 0; slot < 16; ++slot) {
    const start = slot * songBytes;
    if (slot >= songs.length) {
      payload[start] = 127;
      continue;
    }
    const song = songs[slot], study = catalog.find(entry => entry.id === song.study);
    const studyIndex = catalog.findIndex(entry => entry.id === (study.piano ? 'bolero' : study.id));
    payload.set([...song.title].map(char => (accent[char] ?? char).charCodeAt(0)), start);
    const project = payload.subarray(start + 19, start + 19 + projectBytes);
    project.set([67, 80, 83, 49, 2, 1, studyIndex, song.bpm, song.feel, song.mute, song.clave]);
    const view = new DataView(project.buffer, project.byteOffset, project.byteLength);
    view.setUint32(11, 0x434f4d50, true);
    project.set(song.levels, 15);
    project[57] = study.piano ? (song.transpose+12)%12 : song.root;
    view.setUint32(58, crc32(project.subarray(0, 58)), true);
    const options=Number(song.kit)|(['base','lift','break'].indexOf(song.section)<<1)|
      (song.phrases<<3)|((study.piano??0)<<10)|(Number(study.piano ? song.guide : song.countIn)<<15);
    payload[start+84]=options&255; payload[start+85]=options>>8;
  }
  return payload;
}

export {preparedPayload};
