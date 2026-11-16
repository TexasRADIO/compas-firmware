// The optional Kumbia snapshot adds the four kit controls after its mode.
const kitFromSnapshot=bytes=>bytes.length===136 ? Array.from(bytes.subarray(99,103)) : undefined;

class BoardAudio {
  constructor({module, preparedSet, preparedRhythms, standbySong, startupHanan, kitSettings}, rate, publish) {
    this.rate=rate;
    this.publish=publish;
    this.module=module;
    this.master=0;
    this.kitSettings=kitSettings;
    this.use(this.newBoard(preparedSet,preparedRhythms,startupHanan));
    if(standbySong) this.board.bench_cue(standbySong);
    this.frames=0;
    this.dirty=true;
  }
  receive(m) {
    if(m.saveShow) {
      this.pendingShow=true;
    } else if (m.rhythmAction) {
      this.pendingRhythm=m;
    } else if (m.loadSet) {
      this.pendingSet={payload:m.loadSet,rhythms:m.loadRhythms};
    } else if(m.cueSong!==undefined) {
      const cueResult=this.board.bench_cue(m.cueSong);
      if(cueResult) this.publish({cueResult});
    } else if(m.master!==undefined) {
      this.master=m.master;
      this.board.bench_master(this.master);
    } else {
      if(m.encoder!==undefined) this.enc[m.encoder]=m.delta;
      this.board.bench_input(m.buttons,m.keys,m.time);
    }
    this.dirty=true;
  }
  newBoard(payload,rhythms,hanan=false) {
    const board=new WebAssembly.Instance(this.module).exports;
    if (payload) {
      if (payload.length!==board.bench_set_size()) throw Error('Invalid prepared set length.');
      new Uint8Array(board.memory.buffer,board.bench_set_buffer(),payload.length).set(payload);
    }
    if(rhythms) {
      if(rhythms.length!==board.bench_rhythm_slots_size()) throw Error('Invalid show rhythms.');
      new Uint8Array(board.memory.buffer,board.bench_rhythm_slots_buffer(),rhythms.length).set(rhythms);
    }
    if (!board.bench_boot(Number(Boolean(payload))|(rhythms?2:0)|(hanan?4:0))) throw Error('Invalid prepared set.');
    if(this.kitSettings) board.bench_kit_restore?.(...this.kitSettings);
    return board;
  }
  use(board) {
    this.board=board;
    this.enc=new Int32Array(board.memory.buffer,board.bench_encoders(),7);
    this.sourceIndex=256;
    this.phase=0;
    this.primed=false;
    board.bench_master(this.master);
  }
  loadPending() {
    const {payload,rhythms}=this.pendingSet;
    this.pendingSet=undefined;
    let loadResult='playing';
    if (!this.board.bench_playing()) {
      try {
        this.use(this.newBoard(payload,rhythms));
        loadResult='loaded';
      } catch {
        loadResult='invalid';
      }
    }
    this.publish({loadResult});
    return loadResult==='loaded';
  }
  advanceSource() {
    this.left=this.nextLeft;
    this.right=this.nextRight;
    if(this.sourceIndex===256) {
      this.sourcePCM=new Int32Array(this.board.memory.buffer,this.board.bench_audio(),256);
      this.sourceIndex=0;
    }
    this.nextLeft=this.sourcePCM[this.sourceIndex++];
    this.nextRight=this.sourcePCM[this.sourceIndex++];
  }
  renderResampled(out) {
    if(!this.primed) {
      this.advanceSource();
      this.advanceSource();
      this.primed=true;
    }
    for(let i=0;i<out[0].length;++i) {
      const fraction=this.phase/this.rate;
      out[0][i]=(this.left+(this.nextLeft-this.left)*fraction)/4194304;
      out[1][i]=(this.right+(this.nextRight-this.right)*fraction)/4194304;
      this.phase+=44100;
      while(this.phase>=this.rate) {
        this.phase-=this.rate;
        this.advanceSource();
      }
    }
  }
  render(out) {
    if(this.rate!==44100) this.renderResampled(out);
    else for(let offset=0;offset<out[0].length;offset+=128) {
      const pcm=new Int32Array(this.board.memory.buffer,this.board.bench_audio(),256);
      for(let i=0;i<Math.min(128,out[0].length-offset);i++) {
        out[0][offset+i]=pcm[2*i]/4194304;
        out[1][offset+i]=pcm[2*i+1]/4194304;
      }
    }
    if (this.pendingSet) {
      // The old owner has serviced any preceding transport commands above.
      // Reject during playback and publish the new renderer only after boot.
      if (this.loadPending() && this.rate===44100) this.board.bench_audio();
      this.dirty=true;
    }
    if(this.pendingShow) {
      this.pendingShow=false;
      const showResult=this.board.bench_show_export();
      let showSet,showRhythms;
      if(!showResult) {
        showSet=new Uint8Array(this.board.memory.buffer,this.board.bench_set_buffer(),this.board.bench_set_size()).slice();
        showRhythms=new Uint8Array(this.board.memory.buffer,this.board.bench_rhythm_slots_buffer(),this.board.bench_rhythm_slots_size()).slice();
      }
      this.publish({showResult,showSet,showRhythms},showSet?[showSet.buffer,showRhythms.buffer]:[]);
      this.dirty=true;
    }
    if(this.pendingRhythm) {
      const request=this.pendingRhythm;
      this.pendingRhythm=undefined;
      const bytes=new Uint8Array(this.board.memory.buffer,this.board.bench_rhythm_buffer(),this.board.bench_rhythm_size());
      let rhythmResult=5, rhythmPayload;
      if(request.rhythmAction==='export') {
        rhythmResult=this.board.bench_rhythm_export();
        if(!rhythmResult) rhythmPayload=bytes.slice();
      } else if(request.rhythmAction==='import' && request.rhythmPayload?.length===bytes.length) {
        bytes.set(request.rhythmPayload);
        rhythmResult=this.board.bench_rhythm_import();
      }
      this.publish({rhythmResult,rhythmAction:request.rhythmAction,rhythmPayload},rhythmPayload?[rhythmPayload.buffer]:[]);
      this.dirty=true;
    }
    this.frames+=out[0].length;
    if(this.dirty || this.frames>=15*128) {
      this.dirty=false;
      this.frames=0;
      if (this.board.bench_pull_saved_set()) {
        const savedSet=new Uint8Array(this.board.memory.buffer,this.board.bench_set_buffer(),
                                     this.board.bench_set_size()).slice();
        this.publish({savedSet},[savedSet.buffer]);
      }
      this.board.bench_read_view();
      const state=new Uint8Array(this.board.memory.buffer,this.board.bench_snapshot(),
                                 this.board.bench_snapshot_size()).slice();
      this.kitSettings=kitFromSnapshot(state);
      this.publish(state,[state.buffer]);
    }
  }
}
