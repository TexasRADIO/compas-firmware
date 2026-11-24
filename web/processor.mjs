class Compas extends AudioWorkletProcessor {
  constructor({processorOptions: {module}}) {
    super();
    this.e = new WebAssembly.Instance(module).exports;
    const e = this.e;
    if (e.cp_init(sampleRate)) throw Error('Use 44.1 or 48 kHz');
    this.pcm = new Int16Array(e.memory.buffer, e.cp_pcm(), 256);
    this.blob = new Uint8Array(e.memory.buffer, e.cp_blob(), e.cp_snapshot());
    this.frames = 0;
    this.port.onmessage = ({data: {id, cmd, a=0, b=0, project}}) => {
      let result = 0, snapshot;
      if (cmd === 'open') {
        if (project?.byteLength === this.blob.length) this.blob.set(project);
        result = e.cp_restore(project?.byteLength ?? 0);
      } else if (cmd === 'snapshot') {
        e.cp_snapshot();
        snapshot = this.blob.slice();
      } else if (cmd !== 'ready') result = e.cp_command(cmd,a,b);
      // CP_STEP_COUNT is deliberately queried from the engine rather than
      // inferred from legacy study IDs.  That keeps the worklet's score
      // snapshot correct for a reviewed 6/8 (24-step) or 12/8 (48-step)
      // study as well as the established 16- and 32-step arrangements.
      const steps = e.cp_status(15); // CP_STEP_COUNT in src/engine.h
      this.port.postMessage({id,result,snapshot,state:this.state(),
        score:Array.from({length:6},(_,r)=>Array.from({length:steps},(_,j)=>e.cp_step(r,j))),
        levels:Array.from({length:6},(_,r)=>e.cp_level(r))});
    };
  }
  state() { return Array.from({length:15},(_,k)=>this.e.cp_status(k)); }
  process(_inputs, outputs) {
    const out = outputs[0][0];
    if (this.e.cp_render(out.length)) { out.fill(0); return true; }
    for (let i=0;i<out.length;i++) out[i]=this.pcm[i]/32768;
    this.frames+=out.length;
    if(this.frames>=2048) { this.frames=0; this.port.postMessage({state:this.state()}); }
    return true;
  }
}
registerProcessor('compas',Compas);
