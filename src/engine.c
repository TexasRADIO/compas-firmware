#include "engine.h"
#ifdef COMPAS_BOARD
#include "board.h"
#endif

enum { N=0, LOW, SKIN, HAND, SHAKE, SCRAPE, KICK, BELL, STICK, BASS, SHELL, SNARE, PLUCK };
enum { BASE, LIFT, BREAK };
#ifdef COMPAS_BOARD
enum { ACID_VOICE=PLUCK+1 };
#ifdef COMPAS_HANAN
static uint8_t hanan_half_pending;
#endif
static int32_t ensemble_tilt(unsigned cut,unsigned bite)
{ return ((int32_t)cut-44)/10+((int32_t)bite-45)/20; }
static struct {
    uint32_t target, last_step;
    int32_t low, band;
    uint16_t age;
    uint8_t style, cut, bite;
    int8_t ensemble_tilt;
} bass;
void cp_bass_set(unsigned style, unsigned cut, unsigned bite)
{
    bass.style=(uint8_t)style;
    bass.cut=(uint8_t)cut;
    bass.bite=(uint8_t)bite;
    bass.ensemble_tilt=(int8_t)ensemble_tilt(cut,bite);
}
#endif
enum { NO_SECTION=3, NO_SOLO=6, NO_HIT=0xffff };
typedef struct { uint8_t sound, base[32], lift[32], fill[32]; } Part;
typedef struct { uint8_t steps, bpm, break_mask; Part part[CP_ROLES]; } Study;
#include "score.inc"
enum { STUDIES=sizeof library / sizeof *library };

typedef struct {
    uint32_t toggle[CP_ROLES], seed;
    uint8_t study, bpm, feel, muted, reverse, root, level[CP_ROLES];
} Project;

typedef struct {
    uint32_t a, b, noise, da, db;
    int32_t env, decay;
    uint16_t delay;
    uint8_t next, next_vel, type;
} Voice;

static struct {
    Project p;
    Voice voice[CP_ROLES];
    uint32_t sr, threshold, phase, phrase;
    uint8_t playing, primed, pos, section, pending, fill, solo;
#ifdef COMPAS_BOARD
    uint8_t lead_steps, lead_click;
#endif
    uint32_t fill_phrase;
    int16_t pcm[CP_MAX_BLOCK];
    uint8_t blob[CP_PROJECT_BYTES];
} s;

#ifdef COMPAS_BOARD
void cp_board_clave(unsigned direction) { s.p.reverse=(uint8_t)direction; }
void cp_board_root(unsigned root) { s.p.root=(uint8_t)root; }
void cp_board_section(unsigned section) { s.section=(uint8_t)section; s.pending=NO_SECTION; }
#endif

static const Study *study(void) { return &library[s.p.study]; }
#ifdef COMPAS_BOARD
void cp_board_start_count(void) { s.lead_steps=study()->steps/2u; }
unsigned cp_board_counting(void) { return s.lead_steps!=0; }
unsigned cp_board_nearest_step(void)
{
    unsigned next=s.pos+(s.phase>=s.threshold/2u);
    return next<study()->steps ? next : 0u;
}
#endif
static uint32_t hash(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu;
    return h ^ (h >> 16);
}
static uint8_t available(void) {
#ifdef COMPAS_BOARD
    if(board_drums_only()) return 0;
#endif
    uint8_t mask=0;
    for (uint32_t i=0;i<CP_ROLES;i++) if (study()->part[i].sound && s.p.level[i]) mask |= 1u<<i;
    return mask & (uint8_t)~s.p.muted & (s.solo==NO_SOLO ? 63u : (1u<<s.solo));
}
static void clear_voice(uint32_t i) {
#ifdef COMPAS_BOARD
    board_clear727(i);
    if(study()->part[i].sound==BASS) {
        bass.target=0;
        bass.low=bass.band=0;
        bass.last_step=0;
    }
#endif
    s.voice[i].env=0;
    s.voice[i].delay=NO_HIT;
}
static void reset(void) {
    s.playing=0; s.primed=1; s.pos=0; s.phase=0; s.phrase=0;
    s.pending=NO_SECTION; s.section=BASE; s.fill=0; s.solo=NO_SOLO;
#ifdef COMPAS_BOARD
    s.lead_steps=s.lead_click=0;
#endif
    for(uint32_t i=0;i<CP_ROLES;i++) clear_voice(i);
}
static void defaults(uint32_t id) {
    s.p.study=(uint8_t)id;
    s.p.bpm=study()->bpm; s.p.feel=30; s.p.muted=0; s.p.reverse=0; s.p.root=0;
    s.p.seed=0x434f4d50u;
    for(uint32_t i=0;i<CP_ROLES;i++) { s.p.level[i]=100; s.p.toggle[i]=0; }
    reset();
}
uint32_t cp_init(uint32_t rate) {
    if(rate!=44100 && rate!=48000) return CP_RANGE;
    s.sr=rate; s.threshold=rate*60;
    defaults(0); return CP_OK;
}
static uint8_t base_event(uint32_t r, uint32_t step, uint32_t section) {
    const Part *p=&study()->part[r];
    if(!p->sound || (section==BREAK && !(study()->break_mask&(1u<<r)))) return 0;
    uint8_t value=p->base[step];
    if (s.p.toggle[r] & (1u<<step)) value=value ? 0 : (p->sound==BASS ? 16 : 1);
    if(section==LIFT && p->lift[step]) return p->lift[step];
    return value;
}
static uint32_t source_step(uint32_t step) { return s.p.reverse ? (step+16u)%32u : step; }
uint32_t cp_step(uint32_t role, uint32_t step) {
    if(role>=CP_ROLES || step>=study()->steps) return 0;
    return base_event(role,source_step(step),BASE);
}
uint32_t cp_level(uint32_t role) { return role<CP_ROLES ? s.p.level[role] : 0; }

typedef struct { uint16_t hz, hz2, ms; uint8_t gain, human_delay; } Tone;
static const Tone tones[] = {
    {0,0,1,0,0}, {105,169,115,66,0}, {275,421,63,39,0}, {232,351,118,55,2},
    {263,911,50,35,2}, {389,2180,86,39,3}, {89,136,120,78,0},
    {541,823,122,39,1}, {1687,2113,23,40,0}, {0,0,175,78,0},
    {645,997,42,39,1}, {178,317,90,54,0}, {131,196,82,45,1}
};
static const uint16_t notes10[] = {654,693,734,778,824,873,925,980,1038,1100,1165,1235,1308,1386,1468,1556,1648,1746,1850,1960,2077,2200,2331};
static int32_t tri(uint32_t p) {
    int32_t x=(int32_t)(p >> 16);
    return x < 32768 ? x*2-32768 : 98303-x*2;
}
static uint32_t inc(uint32_t hz10) {
    /* 2^32 / (10 * rate), rounded; multiplication stays in uint32_t. */
    return hz10 * (s.sr==44100 ? 9739u : 8948u);
}
static void strike(uint32_t r, uint32_t event, uint32_t variation, uint32_t velocity) {
    Voice *v=&s.voice[r];
    uint32_t type=study()->part[r].sound;
    const Tone *t=&tones[type];
    uint32_t vel=event==2 ? 115u : 83u;
    if(type==BASS) vel=100u;
    int32_t adjust=(int32_t)(variation&15u)-7;
    int32_t length=(int32_t)t->ms;
    if(velocity) vel=velocity;
    else {
        vel=(uint32_t)((int32_t)vel+adjust*s.p.feel/65);
        length += ((int32_t)((variation>>8)&7u)-3)*s.p.feel/55;
    }
#ifdef COMPAS_BOARD
    if (board_note727(s.p.study,r,event,vel)) {
        v->env=-1;
        return;
    }
#endif
    if (event==2 && (type==HAND || type==SKIN || type==SCRAPE)) length+=16;
#ifdef COMPAS_BOARD
    if(type==BASS) {
        board_bass_note_on();
        const unsigned acid=bass.style==BASS_ACID;
        const uint32_t step=s.phrase*study()->steps+s.pos;
        const uint32_t target=inc(notes10[s.p.root+event-16u]);
        const unsigned slide=acid && bass.target && v->type==ACID_VOICE &&
                             step-bass.last_step<=2u && target!=bass.target;
        if(!slide) {
            v->a=v->b=0x40000000u;
            v->da=target;
        }
        if(!slide || v->env<=0) {
            bass.low=bass.band=0;
            bass.age=0;
        }
        bass.target=target;
        bass.last_step=step;
        v->db=target*2u;
        v->type=acid ? ACID_VOICE : BASS;
        v->env=32768;
        v->decay=acid ? 5 : 3;
        v->next_vel=(uint8_t)((vel+(acid && (s.pos&7u)==6u ? 12u : 0u))*70u/128u);
        return;
    }
#endif
    v->a=0x40000000u; v->b=0x40000000u; v->noise=hash(variation^0x97f44a03u)|1u;
    v->da=inc(type==BASS ? notes10[s.p.root+event-16u] : t->hz*10u);
    v->db=type==BASS ? v->da*2u : inc(t->hz2*10u + (event==2 ? 120u : 0u));
    if(type==PLUCK) {
        v->da=inc(2u*notes10[s.p.root]);
        v->db=inc(2u*notes10[s.p.root+7u]);
    }
    v->type=(uint8_t)type; v->env=32768;
    v->decay=1+32768/(length*(int32_t)(s.sr/1000));
    v->next_vel=(uint8_t)((vel*t->gain)/128u);
}
#ifdef COMPAS_BOARD
void cp_board_clear_backing(void) { for(unsigned r=0;r<CP_ROLES;++r) clear_voice(r); }
#endif
static void on_step(void) {
    const Study *st=study();
    uint8_t mask=available();
    unsigned section=s.section;
#ifdef COMPAS_HANAN
    unsigned kumbia_accent=0;
#endif
#ifdef COMPAS_BOARD
    section=board_groove_section(s.pos,st->steps,section);
#endif
    for(uint32_t r=0;r<CP_ROLES;r++) {
        if (!(mask&(1u<<r))) continue;
        const Part *p=&st->part[r];
        uint8_t hit=base_event(r,source_step(s.pos),section);
        /* In salsa the bell takes over from the shell at Lift. */
        if(s.p.study==2 && r==2 && section==LIFT) hit=0;
        if(s.fill==2 && p->fill[s.pos]) hit=p->fill[s.pos];
        if(!hit) continue;
#ifdef COMPAS_HANAN
        kumbia_accent|=hit==2;
#endif
        uint32_t variation=hash(s.p.seed ^ (s.phrase*0x9e3779b9u) ^ (r<<8) ^ s.pos);
        uint32_t dt=tones[p->sound].human_delay;
        if(dt) dt=dt*(variation%4u)*s.p.feel/300u;
        s.voice[r].next=hit;
        s.voice[r].delay=(uint16_t)(dt*s.sr*60u/(s.p.bpm*96u));
    }
#ifdef COMPAS_BOARD
    board_overlay_step(s.p.study,source_step(s.pos));
    board_sequence_step(s.pos);
#ifdef COMPAS_HANAN
    hanan_half_pending=1;
    if(board_hanan_mode) board_hanan_step((s.pos&15u)*2u,kumbia_accent);
#endif
    if(board_roll_keys && (board_roll_mode>=ROLL_SIXTEENTH ||
       (board_roll_mode==ROLL_EIGHTH && !(s.pos&1u)) ||
       (board_roll_mode==ROLL_TRIPLET && !(s.pos&3u))))
        board_roll_trigger();
#endif
}
uint32_t cp_command(uint32_t cmd, uint32_t a, uint32_t b) {
    if(!s.sr) return CP_RANGE;
    switch(cmd) {
    case CP_START:
        if(!s.playing) { s.playing=1; s.primed=1; }
        return CP_OK;
    case CP_STOP: reset(); return CP_OK;
    case CP_STUDY:
        if(s.playing) return CP_STOP_FIRST;
        if(a>=STUDIES) return CP_RANGE;
        defaults(a); return CP_OK;
    case CP_SECTION:
        if(a>BREAK) return CP_RANGE;
        if(!s.playing) { s.section=(uint8_t)a; return CP_OK; }
        if(s.fill==1) return CP_BUSY;
        /* Newest energy request replaces the pending one; asking for the
         * playing section cancels the change. */
        s.pending=(uint8_t)(a==s.section ? NO_SECTION : a); return CP_OK;
    case CP_FILL:
        if(!s.playing) return CP_STOPPED;
#ifdef COMPAS_BOARD
        if(s.lead_steps) return CP_BUSY;
#endif
        if(s.pending!=NO_SECTION || s.fill!=0) return CP_BUSY;
        s.fill=1; s.fill_phrase=s.phrase+(s.pos>=study()->steps-4u); return CP_OK;
    case CP_MUTE: case CP_SOLO: case CP_LEVEL: case CP_HIT: case CP_STEP:
        if(a>=CP_ROLES) return CP_RANGE;
        if(!study()->part[a].sound) return CP_NO_ROLE;
        if(cmd==CP_MUTE) s.p.muted ^= 1u<<a;
        if(cmd==CP_SOLO) s.solo=s.solo==a ? NO_SOLO : (uint8_t)a;
        if(cmd==CP_LEVEL) { if(b>100) return CP_RANGE; s.p.level[a]=(uint8_t)b; }
        if(cmd==CP_STEP) {
            if(b>=study()->steps) return CP_RANGE;
            if((s.p.study==2 || s.p.study==6) && a==0) return CP_RANGE; /* timeline is part of this study */
            s.p.toggle[a]^=1u<<source_step(b);
        }
        if(cmd==CP_HIT) {
            if(!s.playing) return CP_STOPPED;
            if(!(available()&(1u<<a))) return CP_NO_ROLE;
            if(b>127) return CP_RANGE;
            strike(a,study()->part[a].sound==BASS ? 16u : 1u,
                   hash(s.p.seed^s.phrase^s.phase^(a<<8)), b ? b : 100);
        }
        for(uint32_t i=0;i<CP_ROLES;i++) if(!(available()&(1u<<i))) clear_voice(i);
        return CP_OK;
    case CP_BPM:
        if(a<60 || a>180) return CP_RANGE;
        s.p.bpm=(uint8_t)a; return CP_OK;
    case CP_FEEL:
        if(a>100) return CP_RANGE;
        s.p.feel=(uint8_t)a; return CP_OK;
    case CP_ROOT:
        if(a>11 || !s.p.study) return CP_RANGE;
        if(s.playing) return CP_STOP_FIRST;
        s.p.root=(uint8_t)a; return CP_OK;
    case CP_CLAVE:
        if((s.p.study!=2 && s.p.study!=6) || a>1) return CP_RANGE;
        if(s.playing) return CP_STOP_FIRST;
        s.p.reverse=(uint8_t)a; return CP_OK;
    default: return CP_RANGE;
    }
}
#ifdef COMPAS_BOARD
static inline int32_t acid_wave(uint32_t phase,int32_t cutoff,int32_t damping,int32_t *low,int32_t *band)
{
    int32_t saw=(int32_t)(phase>>16)-32768;
    *low+=cutoff*(*band)/256;
    *band+=cutoff*(saw-*low-damping*(*band)/256)/256;
    int32_t signal=(*low*3+tri(phase))/5;
    return signal>32767?32767:signal<-32768?-32768:signal;
}

static inline int32_t ensemble_wave(uint32_t phase,uint32_t overtone,int32_t brightness)
{
    return (tri(phase)*12+tri(overtone)*brightness)/20;
}

static int32_t ensemble_brightness(int32_t envelope,int32_t tilt)
{
    int32_t bright=(envelope>>12)+tilt;
    return bright<0?0:bright>12?12:bright;
}

void cp_bass_preview(int8_t *out,unsigned acid,unsigned cut,unsigned bite)
{
    int32_t low=0, band=0;
    int32_t brightness=ensemble_brightness(28672,ensemble_tilt(cut,bite));
    /* One reference cycle at 172 Hz, following one filter settling cycle. */
    for(unsigned i=0; i<512u; ++i) {
        uint32_t phase=i<<24;
        int32_t value=acid?acid_wave(phase,38+(int32_t)cut/2,178-(int32_t)bite,&low,&band):
            ensemble_wave(phase,phase*2u,brightness);
        if(i>=256u && !(i&3u)) out[(i-256u)/4u]=(int8_t)(value/256);
    }
}

static int32_t bass_sample(Voice *v)
{
    int32_t signal;
    if(v->type==ACID_VOICE) {
        /* Slew pitch only on close, changed notes. A damped state-variable
         * filter opens at the attack and settles toward the cutoff setting. */
        int32_t distance=(int32_t)bass.target-(int32_t)v->da;
        v->da=(uint32_t)((int32_t)v->da+distance/128);
        v->a+=v->da;
        int32_t cutoff=10+bass.cut/2+(v->env>>10);
        int32_t damping=178-bass.bite;
        signal=acid_wave(v->a,cutoff,damping,&bass.low,&bass.band);
    } else {
        v->a+=v->da;
        v->b+=v->db;
        int32_t bright=ensemble_brightness(v->env,bass.ensemble_tilt);
        signal=ensemble_wave(v->a,v->b,bright);
    }
    if(bass.age<64) signal=signal*bass.age++/64;
    int32_t amplitude=v->env;
    v->env-=v->decay+(amplitude>>11);
    return signal*amplitude/32768;
}
#endif
static int32_t sample(Voice *v) {
#ifdef COMPAS_BOARD
    if(v->type==BASS || v->type==ACID_VOICE) return bass_sample(v);
#endif
    v->a+=v->da; v->b+=v->db;
    v->noise^=v->noise<<13; v->noise^=v->noise>>17; v->noise^=v->noise<<5;
    int32_t a=tri(v->a), b=tri(v->b), n=(int32_t)(v->noise>>16)-32768;
    int32_t signal;
    switch(v->type) {
    case KICK: v->da-=v->da>>9; signal=(a*5+b)/7; break;
    case LOW: signal=(a*5+b+n)/8; break;
    case SKIN: case HAND: signal=(a*4+b*2+n)/8; break;
    case SHAKE: signal=(n*5)/8; break;
    case SCRAPE: signal=(n*(b>=0 ? 6 : 2))/8; break;
    case BELL: signal=(a+b+(a>=0 ? 12000 : -12000))/3; break;
    case STICK: signal=(a*4+b*2+n)/8; break;
    case SHELL: signal=(a+b*2+n*2)/6; break;
    case SNARE: signal=(a+n*5)/7; break;
    case PLUCK: signal=(a*3+b*2+n)/8; break;
    default: signal=(a*6+b)/8; break;
    }
    int32_t envelope=v->env;
    v->env-=v->decay + (v->env>>10);
    return (signal*envelope)/32768;
}
uint32_t cp_render(uint32_t count) {
    if(count>CP_MAX_BLOCK || !s.sr) return CP_RANGE;
    uint32_t f=0;
#ifdef COMPAS_BOARD
    /* Run the same sample clock for one bar before the first backing onset. */
    for(; f<count && s.lead_steps; ++f) {
        if(s.primed || s.phase>=s.threshold) {
            if(s.primed) s.primed=0;
            else {
                s.phase-=s.threshold;
                if(++s.pos==s.lead_steps) {
                    const uint32_t remainder=s.phase;
                    s.lead_steps=s.lead_click=s.pos=0;
                    board_song_boundary(0);
                    s.phase=remainder;
                    s.primed=1;
                    break;
                }
            }
            if(!(s.pos&3u)) s.lead_click=96;
        }
        int32_t mix=board_rack_sample();
        if(s.lead_click) {
            unsigned wave=((96u-s.lead_click)*9u)&63u;
            if(wave>31) wave=63u-wave;
            mix+=((int32_t)wave-16)*s.lead_click*(s.pos ? 1 : 2);
            --s.lead_click;
        }
        if(mix>32767 || mix<-32768) board_mix_clip();
        s.pcm[f]=(int16_t)(mix>32767 ? 32767 : mix<-32768 ? -32768 : mix);
        s.phase+=s.p.bpm*4u;
    }
#endif
    uint8_t mask=available();
    for(;f<count;f++) {
#ifdef COMPAS_BOARD
        if(board_piano_selected) {
            if(board_piano_frame(&s.pcm[f])) continue;
            mask=available();
        }
#endif
        if(!s.playing) {
#ifdef COMPAS_BOARD
            s.pcm[f]=(int16_t)board_rack_sample();
#else
            s.pcm[f]=0;
#endif
            continue;
        }
        if(s.primed || s.phase>=s.threshold) {
            if(s.primed) s.primed=0;
            else {
                s.phase-=s.threshold;
                if(++s.pos==study()->steps) {
                    s.pos=0; s.phrase++;
                    if(s.fill==2) s.fill=0;
                    if(s.pending!=NO_SECTION) { s.section=s.pending; s.pending=NO_SECTION; }
#ifdef COMPAS_BOARD
                    uint32_t remainder=s.phase;
                    if(board_song_boundary(1)) {
                        if(!s.playing) {
                            s.pcm[f]=(int16_t)board_rack_sample();
                            continue;
                        }
                        s.phase=remainder;
                        s.primed=0;
                        mask=available();
                        if(board_piano_selected && board_piano_frame(&s.pcm[f])) continue;
                    }
#endif
                }
#ifdef COMPAS_BOARD
                else if(s.pos==study()->steps/2u) board_groove_bar();
#endif
            }
            if(s.fill==1 && s.phrase==s.fill_phrase && s.pos==study()->steps-4u) s.fill=2;
            on_step();
        }
#ifdef COMPAS_BOARD
#ifdef COMPAS_HANAN
        if(hanan_half_pending && s.phase>=s.threshold/2u) {
            hanan_half_pending=0;
            if(board_hanan_mode) board_hanan_step((s.pos&15u)*2u+1u,0);
        }
#endif
        if(board_roll_keys && board_roll_mode>=ROLL_TRIPLET) {
            unsigned substep=s.pos&3u;
            unsigned split=board_roll_mode==ROLL_THIRTYSECOND?s.threshold/2u:
                board_roll_mode==ROLL_TRIPLET && substep==1u?s.threshold/3u:
                board_roll_mode==ROLL_TRIPLET && substep==2u?2u*(s.threshold/3u):0u;
            if(split && s.phase>=split && s.phase<split+s.p.bpm*4u) board_roll_trigger();
        }
#endif
        int32_t mix=0;
        for(uint32_t r=0;r<CP_ROLES;r++) {
            if(!(mask&(1u<<r))) continue;
            Voice *v=&s.voice[r];
            if(v->delay!=NO_HIT) {
                if(v->delay==0) {
                    v->delay=NO_HIT;
                    strike(r,v->next,hash(s.p.seed^s.phrase^(r<<8)^s.pos),0);
                } else v->delay--;
            }
#ifndef COMPAS_BOARD
            if(v->env>0) mix += (sample(v)*(int32_t)v->next_vel*(int32_t)s.p.level[r])/25600;
#endif
        }
#ifdef COMPAS_BOARD
        /* Resolve all same-frame triggers before reading any shared voice. */
        for(uint32_t r=0;r<CP_ROLES;r++) {
            if(!(mask&(1u<<r))) continue;
            Voice *v=&s.voice[r];
            if(v->env<0) mix += board_sample727(r)*(int32_t)s.p.level[r]/100;
            else if(v->env>0) mix += (sample(v)*(int32_t)v->next_vel*(int32_t)s.p.level[r])/25600;
        }
        if(mix>32767 || mix<-32768) board_mix_clip();
        mix=(mix>32767 ? 32767 : mix < -32768 ? -32768 : mix)/2 + board_rack_sample();
        if(mix>32767 || mix<-32768) board_mix_clip();
#endif
        s.phase+=s.p.bpm*4u;
        s.pcm[f]=(int16_t)(mix>32767 ? 32767 : mix < -32768 ? -32768 : mix);
    }
    return CP_OK;
}
int16_t *cp_pcm(void) { return s.pcm; }
uint8_t *cp_blob(void) { return s.blob; }
uint32_t cp_state_bytes(void) { return sizeof s; }
uint32_t cp_status(uint32_t key) {
    switch(key) {
    case CP_STEP_COUNT:return study()->steps;
    case CP_PLAYING:return s.playing; case CP_ACTIVE:return s.section; case CP_PENDING:return s.pending;
    case CP_FILL_STATE:return s.fill; case CP_POSITION:return s.pos; case CP_STUDY_ID:return s.p.study;
    case CP_TEMPO:return s.p.bpm; case CP_FEEL_VALUE:return s.p.feel; case CP_MUTES:return s.p.muted;
    case CP_SOLO_ROLE:return s.solo; case CP_REVERSE:return s.p.reverse;
    case CP_AUDIBLE: {
        uint32_t mask=s.section==BREAK ? study()->break_mask : 63u;
        if(s.p.study==2 && s.section==LIFT) mask &= ~(1u<<2);
        for(uint32_t r=0;r<CP_ROLES;r++)
            if(s.voice[r].env>0 || s.voice[r].delay!=NO_HIT
#ifdef COMPAS_BOARD
               || board_playing727(r)
#endif
            ) mask|=1u<<r;
        return available() & mask;
    }
    case CP_SAMPLE_RATE:return s.sr; case CP_PHRASE:return s.phrase; case CP_ROOT_NOTE:return s.p.root; default:return 0;
    }
}
static uint32_t get32(uint32_t i) {
    uint32_t n=0; for(uint32_t j=0;j<4;j++) n|=(uint32_t)s.blob[i+j]<<(j*8);
    return n;
}
static void put32(uint32_t i,uint32_t n) {
    for(uint32_t j=0;j<4;j++) s.blob[i+j]=(uint8_t)(n>>(j*8));
}
static uint32_t crc(void) {
    uint32_t n=~0u;
    for(uint32_t i=0;i<CP_PROJECT_BYTES-4;i++){
        n^=s.blob[i]; for(uint32_t bit=0;bit<8;bit++) n=(n>>1)^((n&1u)?0xedb88320u:0u);
    }
    return ~n;
}
uint32_t cp_snapshot(void) {
    for(uint32_t i=0;i<CP_PROJECT_BYTES;i++) s.blob[i]=0;
    put32(0,0x31535043u); s.blob[4]=1; s.blob[5]=1; /* version and library revision */
    s.blob[6]=s.p.study; s.blob[7]=s.p.bpm; s.blob[8]=s.p.feel; s.blob[9]=s.p.muted;
    s.blob[10]=s.p.reverse;
    put32(11,s.p.seed);
    for(uint32_t r=0;r<CP_ROLES;r++){
        s.blob[15+r]=s.p.level[r]; put32(21+r*4,s.p.toggle[r]);
    }
    s.blob[45]=s.p.root;
    put32(CP_PROJECT_BYTES-4,crc()); return CP_PROJECT_BYTES;
}
uint32_t cp_restore(uint32_t bytes) {
    if(s.playing) return CP_STOP_FIRST;
    if(bytes!=CP_PROJECT_BYTES || get32(0)!=0x31535043u || s.blob[4]!=1 || s.blob[5]!=1 ||
       s.blob[6]>=STUDIES || s.blob[7]<60 || s.blob[7]>180 || s.blob[8]>100 ||
       s.blob[9]>63 || s.blob[10]>((s.blob[6]==2 || s.blob[6]==6) ? 1 : 0) ||
       get32(CP_PROJECT_BYTES-4)!=crc()) return CP_BAD_PROJECT;
    if(s.blob[45]>(s.blob[6] ? 11 : 0)) return CP_BAD_PROJECT;
    for(uint32_t i=46;i<51;i++) if(s.blob[i]) return CP_BAD_PROJECT;
    const Study *st=&library[s.blob[6]];
    for(uint32_t r=0;r<CP_ROLES;r++) {
        uint32_t bits=get32(21+r*4);
        if(s.blob[15+r]>100 || (st->steps<32 && bits>>st->steps) ||
           (bits && (!st->part[r].sound || ((s.blob[6]==2 || s.blob[6]==6) && r==0))) ||
           (!st->part[r].sound && (s.blob[9]&(1u<<r)))) return CP_BAD_PROJECT;
    }
    s.p.study=s.blob[6]; s.p.bpm=s.blob[7]; s.p.feel=s.blob[8]; s.p.muted=s.blob[9];
    s.p.reverse=s.blob[10]; s.p.root=s.blob[45]; s.p.seed=get32(11);
    for(uint32_t r=0;r<CP_ROLES;r++) { s.p.level[r]=s.blob[15+r]; s.p.toggle[r]=get32(21+r*4); }
    reset(); return CP_OK;
}
