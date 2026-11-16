function normalizeSet(data, catalog) {
  if (data?.format !== 1 || Object.keys(data).some(key => !['format','songs'].includes(key)) ||
      !Array.isArray(data.songs) || data.songs.length < 1 || data.songs.length > 16)
    throw Error('Choose a Compás set with 1–16 songs.');
  return data.songs.map(song => {
    const study = catalog.find(entry => entry.id === song?.study);
    if (!study || typeof song.title !== 'string' || !/^[A-ZÁÉÍÓÚÑÜ0-9 /+\-]{1,18}$/.test(song.title) || !song.title.trim())
      throw Error('Each song needs a known study and a 1–18 character panel name.');
    if (Object.keys(song).some(key => !['study','title','section','kit','levels','mute','bpm','feel','root','clave','phrases','guide','countIn','transpose'].includes(key)))
      throw Error('The set contains an unknown field.');
    const value = {bpm:study.bpm,feel:study.piano?0:(study.feel??30),root:0,transpose:0,clave:0,phrases:0,section:'base',kit:false,mute:0,guide:false,countIn:false,levels:[100,100,100,100,100,100],...song};
    const limits={transpose:study.piano?[-6,5]:[0,0],bpm:[60,180],feel:[0,100],root:[0,study.id==='coast'?0:11],clave:[0,['salsa','son'].includes(study.id)?1:0],phrases:[0,64],mute:[0,study.id==='coast'?31:63]};
    for (const [key,[min,max]] of Object.entries(limits)) {
      if (!Number.isInteger(value[key]) || value[key]<min || value[key]>max) throw Error(song.title+' has an invalid '+key+'.');
    }
    if (!['base','lift','break'].includes(value.section) || typeof value.kit!=='boolean' ||
        !Array.isArray(value.levels) || value.levels.length!==6 ||
        value.levels.some(n => !Number.isInteger(n) || n<0 || n>100)) throw Error(song.title+' has an invalid mix or section.');
    if (typeof value.guide!=='boolean' || (value.guide && !study.guide))
      throw Error(song.title+' has an invalid vocal guide setting.');
    if (typeof value.countIn!=='boolean' || (study.piano && value.countIn))
      throw Error(song.title+' has an invalid groove count-in setting.');
    if (study.piano && (value.feel || value.phrases || value.root || value.clave || value.kit ||
        value.mute || value.section!=='base' || value.levels.some(level=>level!==100)))
      throw Error(song.title+' uses the score’s form; use Transpose for its pitch.');
    return value;
  });
}

export {normalizeSet};
