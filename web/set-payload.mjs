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

// Match the board's 16 packed Song records. The native launcher adds
// the NOR header, journal and marker around this payload.
function preparedPayload(data, catalog) {
  const songs = normalizeSet(data, catalog);
  const payload = new Uint8Array(16 * 85);
  const accent = Object.fromEntries([...'ÁÉÍÓÚÑÜ'].map((letter, index) => [letter, 'aeiounv'[index]]));
  for (let slot = 0; slot < 16; ++slot) {
    const start = slot * 85;
    if (slot >= songs.length) {
      payload[start] = 127;
      continue;
    }
    const song = songs[slot], study = catalog.find(entry => entry.id === song.study);
    const studyIndex = catalog.findIndex(entry => entry.id === (study.piano ? 'bolero' : study.id));
    payload.set([...song.title].map(char => (accent[char] ?? char).charCodeAt(0)), start);
    const project = payload.subarray(start + 19, start + 74);
    project.set([67, 80, 83, 49, 1, 1, studyIndex, song.bpm, song.feel, song.mute, song.clave]);
    const view = new DataView(project.buffer, project.byteOffset, project.byteLength);
    view.setUint32(11, 0x434f4d50, true);
    project.set(song.levels, 15);
    project[45] = study.piano ? (song.transpose+12)%12 : song.root;
    view.setUint32(51, crc32(project.subarray(0, 51)), true);
    payload.set([Number(song.kit), ['base','lift','break'].indexOf(song.section), song.phrases,
                 study.piano ?? 0, Number(study.piano ? song.guide : song.countIn)], start + 80);
  }
  return payload;
}

export {preparedPayload};
