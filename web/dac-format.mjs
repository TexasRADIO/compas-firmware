// DMA packing metadata: old pinned modules used 128; new modules use 256.
// Source/sample gain and audio block length are unrelated to this scale.
export function dacScale(board) {
  const value=board.bench_dac_scale?.()??128;
  if(value!==128 && value!==256) throw Error('Unsupported DAC word format.');
  return value;
}
