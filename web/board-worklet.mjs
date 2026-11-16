class CompasBoard extends AudioWorkletProcessor {
  constructor({processorOptions}) {
    super();
    this.renderer=new BoardAudio(processorOptions,sampleRate,
      (data,transfer)=>this.port.postMessage(data,transfer??[]));
    this.port.onmessage=({data})=>this.renderer.receive(data);
  }
  process(_inputs, outputs) {
    this.renderer.render(outputs[0]);
    return true;
  }
}
registerProcessor('compas-board',CompasBoard);
