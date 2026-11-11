#include "piano.h"

typedef struct { uint16_t tick; uint8_t duration, note[4]; } Chord;
/* Four bytes per written note: the duration (ten bits) and the one-based
 * pitch index (six bits) share a word. tools/board-piano.py checks both fit. */
typedef struct { uint16_t tick, packed; } PianoNote;
static inline int note_duration(const PianoNote *n) { return n->packed & 1023; }
static inline int note_pitch(const PianoNote *n) { return n->packed >> 10; }
_Static_assert(sizeof(PianoNote)==4, "four bytes per written note");
typedef struct { const PianoNote *notes; uint16_t count; } NoteLane;
typedef struct { uint16_t tick; uint8_t gain; } PianoMark;
typedef struct {
    const uint16_t *bars;
    const uint32_t *increments;
    const Chord *hits;
    const PianoNote *canto;
    uint16_t end, hit_count, guide_count;
    uint8_t bar_count, beats, voices, form[4], repeat_bar;
    const NoteLane *lanes;
    const PianoMark *marks;
    uint8_t mark_count, pulse_ticks, tempo_ticks, bolero;
} PianoScore;
#include "piano.inc"
static const PianoScore *score;

enum { FRAME_CLOCK=44100*60, ATTACK=64 };
static struct {
    uint32_t phase, oscillator[4], click, guide_phase;
    int32_t tick;
    uint16_t hit, gate, age, guide_hit, guide_gate;
    uint8_t bar, note[4], click_age, guide_note, guide_age, guide_gain, percussion_edge, pitch_offset;
    uint8_t mark, gain;
#ifdef PIANO_VOICES
    struct {
        uint32_t phase;
        uint16_t hit, gate;
        uint8_t note, age;
    } voice[PIANO_VOICES];
#endif
} player;

static void advance_dynamics(void)
{
    while(player.mark<score->mark_count && score->marks[player.mark].tick<=player.tick)
        player.gain=score->marks[player.mark++].gain;
}

static void seek_dynamics(void)
{
    player.mark=0;
    player.gain=128;
    advance_dynamics();
}

void piano_transpose(int semitones)
{
    if(semitones>=-6 && semitones<=5) player.pitch_offset=(uint8_t)(semitones+6);
}

void piano_start(unsigned id,unsigned count_in)
{
    score=&scores[id-1u];
    player.phase=0;
    piano_transpose(0);
    player.tick=count_in ? -(int32_t)(score->pulse_ticks*score->beats) : 0;
    player.bar=0;
    seek_dynamics();
    player.percussion_edge=score->bolero;
    player.hit=0;
    player.gate=0;
#ifdef PIANO_VOICES
    if(score->lanes) {
        for(unsigned i=0;i<score->voices;++i) {
            player.voice[i].hit=0;
            player.voice[i].gate=0;
            player.voice[i].note=0;
        }
    } else
#endif
    {
        for(unsigned i=0;i<score->voices;++i) player.note[i]=0;
    }
    player.click_age=count_in ? 96 : 0;
    player.click=0;
    player.guide_hit=player.guide_gate=player.guide_gain=0;
}

unsigned piano_step(void)
{
    if(player.phase<FRAME_CLOCK) return PIANO_INSIDE;
    player.phase-=FRAME_CLOCK;
    ++player.tick;
    advance_dynamics();
    player.percussion_edge=score->bolero;
    if(player.tick==0) return PIANO_BAR; /* First written bar after the count-in. */
    if(player.tick==score->end) return PIANO_END;
    if(player.tick<0 && player.tick%(int32_t)score->pulse_ticks==0) {
        player.click=0;
        player.click_age=96;
    }
    unsigned next=player.bar+1u;
    if(next<score->bar_count && player.tick==score->bars[next]) {
        player.bar=(uint8_t)next;
        return PIANO_BAR;
    }
    return PIANO_INSIDE;
}

static int32_t triangle(uint32_t phase)
{
    int32_t value=(int32_t)(phase>>16);
    return value<32768 ? value*2-32768 : 98303-value*2;
}

static void guide_begin(const PianoNote *event)
{
    player.guide_gate=event->tick+note_duration(event);
    player.guide_note=note_pitch(event);
    player.guide_phase=0x40000000u;
    player.guide_age=0;
}
static int32_t guide_mix(unsigned enabled)
{
    if(player.guide_hit<score->guide_count && player.tick==score->canto[player.guide_hit].tick)
        guide_begin(&score->canto[player.guide_hit++]);

    if(enabled && player.guide_gain<ATTACK) ++player.guide_gain;
    if(!enabled && player.guide_gain) --player.guide_gain;
    if(player.tick<0 || player.tick>=player.guide_gate) return 0;
    int32_t signal=triangle(player.guide_phase);
    player.guide_phase+=score->increments[player.guide_note+player.pitch_offset];
    unsigned envelope=player.guide_age<ATTACK ? player.guide_age++ : ATTACK;
    if(player.tick+1==player.guide_gate && player.phase>FRAME_CLOCK/2)
        envelope=(FRAME_CLOCK-player.phase)/(FRAME_CLOCK/2/ATTACK);
    return signal*(int32_t)(envelope*player.guide_gain)/65536;
}

#ifdef PIANO_VOICES
static void voice_begin(unsigned voice,const PianoNote *event)
{
    player.voice[voice].gate=event->tick+note_duration(event);
    player.voice[voice].note=note_pitch(event);
    player.voice[voice].phase=0x40000000u;
    player.voice[voice].age=0;
}

static int32_t voices_mix(void)
{
    int32_t sum=0;
    if(player.tick<0) return 0;
    for(unsigned i=0;i<score->voices;++i) {
        const NoteLane *lane=&score->lanes[i];
        unsigned hit=player.voice[i].hit;
        if(hit<lane->count && player.tick==lane->notes[hit].tick) {
            voice_begin(i,&lane->notes[hit]);
            player.voice[i].hit=(uint16_t)(hit+1);
        }
        unsigned gate=player.voice[i].gate;
        if((unsigned)player.tick>=gate) continue;
        unsigned age=player.voice[i].age;
        unsigned envelope=age<ATTACK ? age : ATTACK;
        if(age<ATTACK) ++player.voice[i].age;
        if((unsigned)player.tick+1u==gate && player.phase>FRAME_CLOCK/2)
            envelope=(FRAME_CLOCK-player.phase)/(FRAME_CLOCK/2/ATTACK);
        uint32_t phase=player.voice[i].phase;
        int32_t tone=triangle(phase);
        int32_t overtone=triangle(phase*2u+0xc0000000u);
        sum+=(tone*5+overtone)*(int32_t)envelope/2048;
        player.voice[i].phase=phase+score->increments[player.voice[i].note+player.pitch_offset];
    }
    if(score->voices>4) return sum/4;
    if(score->voices>2) return sum/2;
    return sum;
}

static void voices_jump(void)
{
    for(unsigned i=0;i<score->voices;++i) {
        const NoteLane *lane=&score->lanes[i];
        unsigned lo=0, hi=lane->count;
        while(lo<hi) {
            unsigned mid=(lo+hi)/2;
            if(lane->notes[mid].tick<player.tick) lo=mid+1;
            else hi=mid;
        }
        player.voice[i].hit=(uint16_t)lo;
        player.voice[i].gate=0;
        if(lo && lane->notes[lo-1].tick+note_duration(&lane->notes[lo-1])>player.tick)
            voice_begin(i,&lane->notes[lo-1]);
    }
}
#endif

static int32_t chords_mix(void)
{
    int32_t sum=0;
    if(player.tick>=0 && player.hit<score->hit_count &&
       player.tick==score->hits[player.hit].tick) {
        const Chord *hit=&score->hits[player.hit++];
        for(unsigned i=0;i<score->voices;++i) {
            player.note[i]=hit->note[i];
            player.oscillator[i]=0x40000000u;
        }
        player.gate=hit->tick+hit->duration;
        player.age=0;
    }
    if(player.tick<player.gate) {
        unsigned envelope=player.age<ATTACK ? player.age : ATTACK;
        if(player.tick+1==player.gate && player.phase>FRAME_CLOCK/2)
            envelope=(FRAME_CLOCK-player.phase)/(FRAME_CLOCK/2/ATTACK);
        for(unsigned i=0;i<score->voices;++i) {
            if(!player.note[i]) continue;
            uint32_t phase=player.oscillator[i];
            int32_t tone=triangle(phase);
            int32_t overtone=triangle(phase*2u+0xc0000000u);
            sum+=(tone*5+overtone)*((int32_t)envelope)/2048;
            player.oscillator[i]=phase+score->increments[player.note[i]+player.pitch_offset];
        }
        if(player.age<ATTACK) ++player.age;
    }
    if(score->voices>2) sum/=2;
    return sum;
}

int32_t piano_mix(unsigned bpm,unsigned guide)
{
#ifdef PIANO_VOICES
    int32_t sum=score->lanes ? voices_mix() : chords_mix();
#else
    int32_t sum=chords_mix();
#endif
    if(player.gain!=128) sum=sum*(int32_t)player.gain/128;
    if(player.click_age) {
        sum+=triangle(player.click)*(int32_t)player.click_age/1024;
        player.click+=107130452u; /* 1.1 kHz at 44.1 kHz */
        --player.click_age;
    }
    if(score->guide_count) sum+=guide_mix(guide);
    player.phase+=bpm*score->tempo_ticks;
    return sum;
}

void piano_jump(unsigned bar)
{
    player.bar=(uint8_t)bar;
    player.tick=score->bars[bar];
    seek_dynamics();
    player.percussion_edge=score->bolero;
    player.gate=0;
    unsigned lo=0, hi=score->hit_count;
    while(lo<hi) {
        unsigned mid=(lo+hi)/2;
        if(score->hits[mid].tick<player.tick) lo=mid+1;
        else hi=mid;
    }
    player.hit=(uint16_t)lo;
#ifdef PIANO_VOICES
    if(score->lanes) voices_jump();
#endif
    lo=0; hi=score->guide_count;
    while(lo<hi) {
        unsigned mid=(lo+hi)/2;
        if(score->canto[mid].tick<player.tick) lo=mid+1;
        else hi=mid;
    }
    player.guide_hit=(uint16_t)lo;
    player.guide_gate=0;
    if(lo && score->canto[lo-1].tick+note_duration(&score->canto[lo-1])>player.tick)
        guide_begin(&score->canto[lo-1]);
}

unsigned piano_percussion(void)
{
    if(!player.percussion_edge) return 0;
    player.percussion_edge=0;
    if(player.bar<score->form[1] || player.bar>=score->form[3]) return 0;
    unsigned tick=(unsigned)(player.tick-score->bars[player.bar]);
    if(tick%6u) return 0;
    static const uint8_t bolero[16]={
        PIANO_MARACA,0,PIANO_MARACA,PIANO_CONGA_MUTE,
        PIANO_MARACA_ACCENT,0,PIANO_MARACA,PIANO_CONGA_OPEN,
        PIANO_MARACA,0,PIANO_MARACA,0,
        PIANO_MARACA_ACCENT,0,PIANO_MARACA,0
    };
    return tick<96u ? bolero[tick/6u] : 0;
}

unsigned piano_bar(void) { return player.bar; }
unsigned piano_coda(void) { return score->form[3]; }
unsigned piano_part_bar(unsigned form) { return score->form[form-PIANO_FORM_INTRO]; }
unsigned piano_position(void)
{
    unsigned offset=player.tick<0 ? (unsigned)(player.tick+(int32_t)(score->pulse_ticks*score->beats)) :
        (unsigned)(player.tick-score->bars[player.bar]);
    return offset/(score->pulse_ticks/4u);
}
unsigned piano_form(void)
{
    if(player.tick<0) return PIANO_FORM_COUNT_IN;
    unsigned bar=player.bar;
    if(bar>=score->form[PIANO_FORM_CODA-PIANO_FORM_INTRO]) return PIANO_FORM_CODA;
    if(score->repeat_bar && bar>=score->repeat_bar) bar-=score->repeat_bar;
    unsigned section=PIANO_FORM_REFRAIN;
    while(section>PIANO_FORM_INTRO && bar<score->form[section-1]) --section;
    return section;
}
unsigned piano_beats(void) { return score->beats; }
unsigned piano_has_guide(void) { return score->guide_count!=0; }
unsigned piano_has_percussion(void) { return score->bolero; }
unsigned piano_can_hold(void)
{
    unsigned bar=player.bar;
    return bar>=3 && (bar&1u) && score->bars[bar]-score->bars[bar-2]==2u*score->pulse_ticks*score->beats;
}

#ifdef COMPAS_EMU_STORE
unsigned piano_id_valid(unsigned id, unsigned guide)
{
    if(!id) return !guide;
    return id<=sizeof scores/sizeof *scores && (!guide || scores[id-1].guide_count);
}
#endif
