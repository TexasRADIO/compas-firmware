/* Decorative frames use TIMER4. The beat index is published by the audio owner. */
enum { MOTION_NONE, MOTION_SONG, MOTION_LIFT, MOTION_BACK, MOTION_FILL, MOTION_CLOSE };
typedef struct {
    uint32_t at, beat_at;
    uint8_t kind, previous_song, frame, ending, roll, break_mode, phase, stop_requested;
} StageMotion;

static StageMotion motion;
volatile uint32_t compas_animation_counts[5];

static unsigned written_or_groove_ending(BoardView v)
{
    return v.playing && (v.piano ? v.score_form==4 : v.part==PART_END);
}

static StageMotion motion_offer(BoardView v,BoardView old,const Panel *p,unsigned opening,
                                unsigned beat,unsigned last_beat,unsigned now)
{
    StageMotion next=motion;
    unsigned new_cue=(v.next_song<SET_SLOTS && v.next_song!=old.next_song) ||
        (v.next_groove<BOARD_GROOVE_PRESETS && v.next_groove!=old.next_groove) ||
        (v.next_part<PART_NONE && (v.next_part!=old.next_part || v.next_bar!=old.next_bar)) ||
        (v.pending<3 && v.pending!=old.pending) ||
        (board_next_root(v)<12 && board_next_root(v)!=board_next_root(old)) ||
        (board_next_clave(v)<2 && board_next_clave(v)!=board_next_clave(old)) ||
        (v.next_rhythm<BOARD_RHYTHM_PATTERNS && v.next_rhythm!=old.next_rhythm);
    if(beat!=last_beat || opening) next.beat_at=now;
    if(p->stop_epoch!=model.panel.stop_epoch) next.stop_requested=1;
    if(opening || new_cue || next.stop_requested) next.kind=MOTION_NONE;
    unsigned energy=board_part_energy(v.part,v.section), prior=board_part_energy(old.part,old.section);
    unsigned ending=written_or_groove_ending(v);
    next.break_mode=v.playing && !v.piano && !ending && energy==2;
    next.ending=(uint8_t)ending;
    next.roll=0;
    next.phase=(uint8_t)(beat*2u);
    if(!opening && !new_cue && !next.stop_requested) {
        unsigned type=MOTION_NONE, count=5;
        if(v.playing && v.song!=old.song && old.playing && old.next_song==v.song) {
            type=MOTION_SONG; count=0; next.previous_song=old.song;
        } else if(!v.playing && motion.ending) {
            type=MOTION_CLOSE; count=4;
        } else if(v.playing && !v.piano && old.next_part<PART_NONE && v.next_part==PART_NONE &&
                  old.fill && !v.fill && v.part==old.next_part) {
            type=MOTION_FILL; count=3;
        } else if(v.playing && !v.piano && !ending && energy!=prior && v.song==old.song) {
            if(prior==2) { type=MOTION_BACK; count=2; }
            else if(energy==1) { type=MOTION_LIFT; count=1; }
            else if(energy==2) { ++compas_animation_counts[2]; }
        }
        if(type) { next.kind=(uint8_t)type; next.at=now; ++compas_animation_counts[count]; }
    }
    unsigned elapsed=now-next.at;
    unsigned tempo=max(1,board_effective_bpm(v));
    next.frame=(uint8_t)min(255u,elapsed/1200000u);
    if(next.kind==MOTION_SONG && elapsed>=36000000u) next.kind=MOTION_NONE;
    if(next.kind==MOTION_LIFT && elapsed>=4u*(1440000000u/tempo)) next.kind=MOTION_NONE;
    if((next.kind==MOTION_BACK || next.kind==MOTION_FILL) && elapsed>=3600000u) next.kind=MOTION_NONE;
    if(next.kind==MOTION_CLOSE && (v.playing || elapsed>=48000000u)) next.kind=MOTION_NONE;
    if(next.kind==MOTION_LIFT && next.frame>=6 && now-next.beat_at>=720000000u/tempo)
        ++next.phase;
    if(v.playing && !v.piano && v.fill==2 && v.next_part<PART_NONE &&
       beat+1u==panel_beats(v.study)) {
        unsigned eighth=180000000u/tempo;
        next.roll=(uint8_t)min(8u,1u+(now-next.beat_at)/eighth);
    }
    if(!v.playing) next.stop_requested=0;
    return next;
}

static unsigned motion_lights(const StageMotion *m,unsigned beat,unsigned i,unsigned normal)
{
    if(m->kind==MOTION_CLOSE) return m->frame<8 ? INK : GOLD;
    if(m->ending && (i<beat*2u || i>=30u-beat*2u)) return INK;
    if(m->break_mode) return (i&7u)==0 && (beat&1u)==0 ? CYAN : INK;
    if(m->kind==MOTION_BACK) return CREAM;
    if(m->kind==MOTION_LIFT) {
        if(m->frame<2) return GOLD;
        return i==(m->phase*2u)%30u ? CREAM : normal;
    }
    return normal;
}

static uint16_t motion_cone(const StageMotion *m,unsigned beat,unsigned i,unsigned normal)
{
    static const uint16_t roll[]={ORANGE,PINK,GOLD,CYAN,MINT,LILAC,PINK,GOLD};
    if(m->kind==MOTION_CLOSE) return m->frame<8 ? INK : normal;
    if(m->kind==MOTION_BACK) return CREAM;
    if(m->kind==MOTION_FILL) return GOLD;
    if(m->break_mode) return i==beat ? CYAN : LINE;
    if(m->roll && i<m->roll) return roll[i];
    if(m->kind==MOTION_LIFT && m->frame<6)
        return m->frame<2 || (i&1u)==(m->frame/2u&1u) ? GOLD : LINE;
    return (uint16_t)normal;
}
