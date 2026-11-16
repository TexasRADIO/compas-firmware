#define _POSIX_C_SOURCE 200809L
#include "panel.h"
#include "piano.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned pending, half, aux, ack, disabled, inject_late, inject_deadline, tick_calls;
static uint64_t elapsed[35000];
static uintptr_t stack_ceiling, stack_floor=UINTPTR_MAX;
static unsigned stack_active;

#define NO_PROFILE __attribute__((no_instrument_function))
void NO_PROFILE __cyg_profile_func_enter(void *function, void *caller)
{
    (void)function;
    (void)caller;
    if (!stack_active) return;
    uintptr_t frame=(uintptr_t)__builtin_frame_address(0);
    if (frame<stack_floor) stack_floor=frame;
}
void NO_PROFILE __cyg_profile_func_exit(void *function, void *caller)
{
    (void)function;
    (void)caller;
}
static uint64_t ns(void)
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
uint32_t board_ticks(void)
{
    if (inject_deadline && (++tick_calls%2u)==0) return BOARD_FRAMES*BOARD_TICKS_PER_FRAME;
    return 0; /* Target TIMER4 is modelled. Wall time is measured outside the full callback. */
}
uint32_t board_pending(void) { return pending; }
void board_ack_aux(uint32_t p) { aux|=p&0x70u; }
unsigned board_free_half(void) { return half; }
void board_ack_half(void) { ++ack; if (inject_late) half^=1; }
void board_disable_output(void) { ++disabled; }
uint32_t board_lock(void) { return 0; }
void board_unlock(uint32_t previous) { (void)previous; }

static void put(unsigned cmd,unsigned a,unsigned b)
{
    assert(board_submit((BoardCommand){cmd,a,b})==CP_OK);
}
static unsigned NO_PROFILE callback(void)
{
    pending=BOARD_HALF_IRQ;
    uintptr_t frame=(uintptr_t)__builtin_frame_address(0);
    stack_active=1;
    uint64_t before=ns();
    fm1_alnk0_irq();
    uint64_t delta=ns()-before;
    stack_active=0;
    if(frame>stack_ceiling) stack_ceiling=frame;
    half^=1;
    pending=0;
    return (unsigned)delta;
}
static void verify_panel(void)
{
    static const int32_t neutral[7]={0};
    Panel panel={0};
    char face[FACE_LINES][FACE_COLS+1];
    board_init();
    assert(board_view(0).master==0 && !board_view(0).playing);
    for(unsigned i=0;i<BOARD_WORDS;++i) assert(!board_dma[0][i] && !board_dma[1][i]);
    panel_input(&panel,1u<<12,0,neutral); callback();
    assert(board_view(0).playing);
    panel_input(&panel,1u<<10,0,neutral); callback();
    panel_face(&panel,board_view(0),face);
    assert(strstr(face[2],"FILL NEXT") && strstr(face[7],"TURN MASTER"));
    panel_input(&panel,1u<<12,0,neutral); callback();
    assert(!board_view(0).playing);
    panel_input(&panel,1u<<13,1u<<14,neutral); callback();
    panel_face(&panel,board_view(0),face);
    assert(strstr(face[4],"STAR CHIME"));
    panel_input(&panel,1u<<6,0,neutral); callback();
    assert(board_view(0).assigned[0]==15 && board_view(0).kit);
    panel_input(&panel,1u,0,neutral); callback();
    assert(!board_view(0).assigned[0]);
    for(unsigned i=0;i<8;++i) put(BOARD_RACK,i,110);
    assert(board_submit((BoardCommand){BOARD_RACK,14,0})==BOARD_QUEUE_FULL);
    put(CP_STOP,0,0); callback();
    assert(!board_view(0).playing && board_stats.rack_peak==1);
    pending=0x70; unsigned prior=ack; fm1_alnk0_irq();
    assert(aux==0x70 && ack==prior);

    put(BOARD_CUE,0,0);callback();
    panel_input(&panel,1u<<7,0,neutral);callback();
    panel_input(&panel,0,0,(const int32_t[7]){[4]=15});callback();
    assert(panel.page==PANEL_SET && panel.slot==15 && board_view(0).song==0);
    panel_input(&panel,0,1u<<15,neutral);callback();
    assert(board_view(0).song==15);
    panel_input(&panel,0,0,(const int32_t[7]){[4]=-15});callback();
    assert(panel.slot==0 && board_view(0).song==15);
    panel_input(&panel,1u<<12,0,neutral);callback();
    panel_input(&panel,0,1u,neutral);callback();
    PanelKeys show=panel_keys(&panel,board_view(0));
    assert(show.pads==0xffff && show.active==1u<<15 && show.queued==1u);
}
typedef struct { uint32_t offset, frames; } Clip;
#include "727-reference.inc"
extern const int16_t compas_pcm727[];

static void verify_master(void)
{
    Panel panel={0};
    board_init();
    panel_master(&panel,-1);
    panel_master(&panel,900);
    panel_master(&panel,911);
    callback();
    assert(board_view(0).master==0);
    assert(board_submit((BoardCommand){BOARD_MASTER,101,0})==CP_RANGE);
    panel_master(&panel,1023);
    callback();
    assert(board_view(0).master==100);

    /* The parked pot must leave an encoder adjustment alone. */
    put(BOARD_MASTER,60,0);
    panel_master(&panel,1018);
    callback();
    assert(board_view(0).master==60);

    /* STOP clears musical actions. Volume still reaches zero. */
    for(unsigned i=0;i<8;++i) put(BOARD_RACK,i,127);
    put(CP_STOP,0,0);
    panel_master(&panel,0);
    callback();
    assert(!board_view(0).master && !board_view(0).playing && !board_stats.rack_peak);

    put(BOARD_RACK,14,127);
    callback();
    panel_master(&panel,1023);
    unsigned completed=half;
    callback();
    const int16_t *clip=compas_pcm727+clips[14].offset;
    for(unsigned i=0;i<BOARD_FRAMES;++i) {
        int32_t full=clip[BOARD_FRAMES+i]*127/512;
        int32_t expected=full*(int32_t)(i+1)/BOARD_FRAMES*128;
        assert(board_dma[completed][2*i]==expected && board_dma[completed][2*i+1]==expected);
    }
    panel_master(&panel,0);
    completed=half;
    callback();
    for(unsigned i=0;i<BOARD_FRAMES;++i) {
        int32_t full=clip[2*BOARD_FRAMES+i]*127/512;
        int32_t expected=full*(int32_t)(BOARD_FRAMES-i-1)/BOARD_FRAMES*128;
        assert(board_dma[completed][2*i]==expected && board_dma[completed][2*i+1]==expected);
    }
    completed=half;
    callback();
    for(unsigned i=0;i<BOARD_WORDS;++i) assert(!board_dma[completed][i]);
}

static void verify_bank(void)
{
    board_init();
    put(BOARD_MASTER,100,0);
    callback();
    for(unsigned sound=0;sound<RACK_SOUNDS;++sound) {
        put(CP_STOP,0,0);
        callback();
        put(BOARD_RACK,sound,127);
        const Clip *clip=&clips[sound];
        for(unsigned offset=0;offset<clip->frames+BOARD_FRAMES;offset+=BOARD_FRAMES) {
            unsigned completed=half;
            callback();
            for(unsigned i=0;i<BOARD_FRAMES;++i) {
                const unsigned n=offset+i;
                int32_t expected=n<clip->frames ? compas_pcm727[clip->offset+n]*127/512*128 : 0;
                assert(board_dma[completed][2*i]==expected && board_dma[completed][2*i+1]==expected);
            }
        }
        /* Unity-gain sequencer path checks every bit before rack scaling. */
        put(BOARD_ASSIGN,0,sound+1); callback();
        assert(board_note727(0,0,1,128));
        for(unsigned i=0;i<clip->frames;++i)
            assert(board_sample727(0)==compas_pcm727[clip->offset+i]);
        assert(!board_playing727(0) && !board_sample727(0));
    }
}
static void write_block(FILE *file)
{
    unsigned completed=half;
    callback();
    for(unsigned j=0;j<BOARD_FRAMES;++j) {
        uint16_t sample=(uint16_t)(board_dma[completed][2*j]/128);
        fputc(sample&255u,file);
        fputc(sample>>8,file);
    }
}
static void audition(const char *filename)
{
    FILE *file=fopen(filename,"wb");
    assert(file);
    board_init();
    put(BOARD_MASTER,100,0);
    callback();
    const uint8_t studies[]={1,1,5,6,7};
    for(unsigned passage=0;passage<sizeof studies;++passage) {
        put(CP_STOP,0,0);
        write_block(file);
        put(CP_STUDY,studies[passage],0);
        put(BOARD_KIT,passage>0 && passage<4,0);
        put(CP_START,0,0);
        for(unsigned b=0;b<2755;++b) {
            if(b==1700) put(CP_FILL,0,0);
            write_block(file);
        }
    }
    put(CP_STOP,0,0);
    write_block(file);
    for(unsigned sound=0;sound<RACK_SOUNDS;++sound) {
        put(BOARD_RACK,sound,127);
        for(unsigned b=0;b<460;++b) write_block(file);
    }
    assert(!fclose(file));
}

static void speed_source(unsigned kind,unsigned rate)
{
    board_init();put(BOARD_MASTER,100,0);callback();
    if(kind<7) {
        put(BOARD_CUE,kind<6?kind+10:9,0);callback();
        put(CP_BPM,100,0);callback();
        if(kind<6) {
            put(BOARD_KIT,1,0);put(CP_FEEL,0,0);
            for(unsigned i=0;i<4;++i)put(BOARD_OVERLAY,i,0);
            callback();
        }
    }
    if(kind==7) {put(CP_BPM,100,0);callback();}
    put(BOARD_SPEED,rate,0);
    put(kind==7?BOARD_RACK:CP_START,kind==7?12:0,kind==7?110:0);
}
static void verify_speed(void)
{
    /* Black-box oracle: the entire normal-speed audio stream, independently
     * addressed at rational source times. Covers the sequencer, synthesized
     * bass, PCM/overlay tails, score playback and stopped finger drumming. */
    enum { BLOCKS=1000, FRAMES=BLOCKS*BOARD_FRAMES };
    static int16_t reference[FRAMES+BOARD_FRAMES];
    static const unsigned rates[]={60,75,78,100};
    for(unsigned kind=0;kind<8;++kind) {
        speed_source(kind,100);
        for(unsigned b=0;b<=BLOCKS;++b) {
            unsigned done=half;callback();
            for(unsigned f=0;f<BOARD_FRAMES;++f)reference[b*BOARD_FRAMES+f]=board_dma[done][f*2]/128;
        }
        for(unsigned r=0;r<sizeof rates/sizeof *rates;++r) {
            speed_source(kind,rates[r]);
            for(unsigned b=0;b<BLOCKS;++b) {
                unsigned done=half;callback();
                assert(board_view(0).speed==rates[r] && board_view(0).bpm==100);
                for(unsigned f=0;f<BOARD_FRAMES;++f) {
                    unsigned t=(b*BOARD_FRAMES+f)*rates[r],i=t/100,frac=t%100;
                    int expected=reference[i]+((int)reference[i+1]-reference[i])*(int)frac/100;
                    assert(board_dma[done][f*2]==expected*128);
                }
            }
            assert(!board_stats.rail_samples);
            put(CP_STOP,0,0);unsigned done=half;callback();
            for(unsigned f=0;f<BOARD_WORDS;++f)assert(!board_dma[done][f]);
        }
        if(kind)continue;
        speed_source(0,75);
        unsigned time=0,rate=75;
        for(unsigned b=0;b<BLOCKS;++b) {
            if(b%100==99) {rate=rates[(b/100)%4];put(BOARD_SPEED,rate,0);}
            unsigned done=half;callback();
            for(unsigned f=0;f<BOARD_FRAMES;++f,time+=rate) {
                unsigned i=time/100,frac=time%100;
                int expected=reference[i]+((int)reference[i+1]-reference[i])*(int)frac/100;
                assert(board_dma[done][f*2]==expected*128);
            }
        }
        put(BOARD_CUE,11,0);callback();assert(board_view(0).speed==rate);
    }
    board_init();put(BOARD_SPEED,59,0);callback();
    assert(board_view(0).result==CP_RANGE && board_view(0).speed==100);
    put(BOARD_SPEED,101,0);callback();assert(board_view(0).result==CP_RANGE);
    Panel panel={.page=PANEL_MIX};int32_t encoder[7]={[5]=-25};
    panel_input(&panel,0,0,encoder);callback();
    assert(board_view(0).speed==75);
    char face[FACE_LINES][FACE_COLS+1];panel_face(&panel,board_view(0),face);
    assert(strstr(face[6],"-05.0 ST"));
    encoder[5]=100;panel_input(&panel,0,0,encoder);callback();
    assert(board_view(0).speed==100);
}

static void verify_show(void)
{
    static const int32_t neutral[7]={0};
    Panel panel={0};
    board_init();
    panel_input(&panel,1u<<7,1u<<1,neutral); callback();
    assert(board_view(0).song==1 && board_view(0).kit && board_view(0).study==1);
    put(BOARD_REPEAT,1,0);
    put(CP_START,0,0);
    put(CP_BPM,180,0);
    callback();
    /* One phrase left before auto-advance: the horn reads LAST, not IN n. */
    assert(board_view(0).phrases_left==1 && board_view(0).next_song==SET_SLOTS);
    for(unsigned i=0;i<940;++i) callback();
    BoardView v=board_view(0);
    assert(v.song==2 && v.study==5 && v.playing && v.bpm==76);
    put(BOARD_CUE,5,0);
    put(BOARD_CUE,6,0);
    put(CP_BPM,180,0);
    callback();
    assert(board_view(0).next_song==6 && board_view(0).result==CP_OK);
    for(unsigned i=0;i<940;++i) callback();
    v=board_view(0);
    assert(v.song==6 && v.study==4 && v.bpm==105 && v.playing);
    put(CP_STOP,0,0); callback();
    put(BOARD_SAVE,15,0); callback();
    assert(!strcmp(board_song_title(15),"TEJANO CUMBIA"));
    assert(!strcmp(board_song_title(6),"TEJANO CUMBIA"));
    put(BOARD_CUE,1,0); callback();
    put(BOARD_CUE,15,0); callback();
    assert(board_view(0).song==15 && board_view(0).study==4 && !board_view(0).playing);
    put(BOARD_CUE,8,0); callback();
    put(BOARD_SAVE,15,0); callback();
    assert(!strcmp(board_song_title(15),"LA PALOMA"));
    put(BOARD_SAVE,15,0); callback(); /* In-place capture aliases its own title. */
    assert(!strcmp(board_song_title(15),"LA PALOMA"));
    board_init();
    put(BOARD_CUE,15,0); callback();
    assert(board_view(0).result==CP_OK && board_view(0).study==14); /* factory returns after session save */
}

static void verify_conductor(void)
{
    static const int32_t neutral[7]={0}, next[7]={[6]=1};
    Panel panel={0};
    char face[FACE_LINES][FACE_COLS+1];
    board_init();
    put(BOARD_CUE,0,0); callback();
    panel_face(&panel,board_view(0),face);
    assert(strstr(face[1],"CUMBIA COAST") && strstr(face[5],"PLAY / PRESETS"));

    panel_input(&panel,0,0,next); callback();
    assert(board_view(0).song==1 && !board_view(0).playing);
    panel_input(&panel,1u<<12,0,neutral); callback();
    panel_input(&panel,0,0,next); callback();
    panel_input(&panel,0,0,next); callback();
    BoardView v=board_view(0);
    panel_face(&panel,v,face);
    assert(v.playing && v.song==1 && v.next_song==3 && strstr(face[4],"SON MONTUNO"));
    put(CP_BPM,180,0); callback();
    for(unsigned i=0;i<940;++i) callback();
    assert(board_view(0).playing && board_view(0).song==3);

    panel_input(&panel,0,1u<<PANEL_VARIATION_E4,neutral); callback();
    panel_face(&panel,board_view(0),face);
    assert(strstr(face[3],"NEXT PART") && strstr(face[4],"CORO"));
    put(CP_BPM,180,0); callback();
    for(unsigned i=0;i<940;++i) callback();
    assert(board_view(0).part==PART_VERSE_B);
    assert(board_groove_section(0,32,2)==0 && board_groove_section(16,32,2)==1);
    panel_input(&panel,0,1u<<PANEL_END_A4,neutral); callback();
    for(unsigned i=0;i<1200 && board_view(0).part!=PART_END;++i) callback();
    assert(board_view(0).part==PART_END && board_view(0).playing);
    for(unsigned i=0;i<1200 && board_view(0).playing;++i) callback();
    /* The ending stops the band and moves the show on to the next slot. */
    assert(!board_view(0).playing && board_view(0).song==4);

    put(BOARD_CUE,8,0); callback();
    panel_input(&panel,0,1u<<PANEL_CHORUS_F4,neutral); callback();
    put(CP_START,0,0); callback();
    assert(board_view(0).score_form==PIANO_FORM_REFRAIN);
    panel_input(&panel,0,1u<<PANEL_VERSE_D4,neutral); callback();
    assert(board_view(0).next_part==PART_VERSE_A);
    put(CP_BPM,180,0); callback();
    for(unsigned i=0;i<500 && board_view(0).next_part<PART_NONE;++i) callback();
    assert(board_view(0).playing && board_view(0).score_form==PIANO_FORM_VERSE &&
           board_view(0).next_part==PART_NONE);
}

static void verify_band_pads(void)
{
    static const int32_t neutral[7]={0};
    const uint32_t pads=127u | (255u<<PANEL_PAD_B4);
    Panel panel={0};
    char face[FACE_LINES][FACE_COLS+1];
    board_init();
    put(BOARD_CUE,8,0);
    put(BOARD_MASTER,70,0);
    callback();
    panel_input(&panel,1u<<12,1u<<PANEL_CHORUS_F4,neutral); callback();
    assert(board_view(0).playing && board_view(0).score_form==PIANO_FORM_REFRAIN);
    panel_input(&panel,1u<<3,pads,neutral); callback();
    BoardView v=board_view(0);
    panel_face(&panel,v,face);
    assert(v.playing && v.score_form==PIANO_FORM_REFRAIN && v.accent &&
           v.next_part==PART_NONE && strstr(face[7],"BPM"));
    assert(board_stats.rack_peak==RACK_VOICES && board_stats.choke_cuts==5);
    panel_input(&panel,0,pads,neutral); callback();
    assert(board_stats.choke_cuts==5);
}

static void verify_bass_and_overlays(void)
{
    static const int32_t neutral[7]={0}, select_acid[7]={[5]=1}, shape[7]={[2]=100,[3]=100};
    unsigned hashes[2]={0,0};
    for(unsigned mode=0;mode<2;++mode) {
        Panel panel={0};
        board_init();
        put(BOARD_CUE,3,0); /* Son montuno. */
        put(BOARD_MASTER,75,0);
        put(CP_SOLO,5,0);
        callback();
        panel_input(&panel,1u<<4,0,mode?select_acid:neutral); callback();
        if(mode) { panel_input(&panel,0,0,shape); callback(); }
        assert(panel.page==PANEL_BASS && board_view(0).bass_style==mode);
        assert(!mode || (board_view(0).bass_cut==100 && board_view(0).bass_bite==100));
        assert(cp_step(5,6)==23 && cp_step(5,12)==16 && !cp_step(5,0));
        put(CP_START,0,0); callback();
        unsigned peak=0;
        for(unsigned block=0;block<900;++block) {
            unsigned completed=half;
            callback();
            for(unsigned i=0;i<BOARD_WORDS;i+=2) {
                int sample=board_dma[completed][i]/128;
                unsigned level=(unsigned)(sample<0 ? -sample : sample);
                if(level>peak) peak=level;
                hashes[mode]=hashes[mode]*16777619u ^ (unsigned)sample;
            }
        }
        assert(peak>200 && !board_stats.rail_samples);
        panel_input(&panel,0,1u<<17,neutral); callback();
        assert(board_view(0).playing && (board_view(0).muted&32u));
    }
    assert(hashes[0]!=hashes[1]);

    Panel panel={0};
    board_init();
    put(BOARD_CUE,5,0); /* Merengue, a two-beat groove. */
    for(unsigned r=0;r<CP_ROLES;++r) put(CP_MUTE,r,0);
    put(BOARD_MASTER,75,0);
    callback();
    panel_input(&panel,1u<<5,1u<<PANEL_CONGA_CS4,neutral); callback();
    assert(panel.page==PANEL_PERC && board_view(0).overlays==1);
    put(CP_START,0,0); callback();
    for(unsigned i=0;i<420;++i) callback();
    assert(board_stats.rack_peak && board_view(0).audible && !board_stats.rail_samples);
    panel_input(&panel,0,0,neutral);
    panel_input(&panel,0,1u<<PANEL_CONGA_CS4,neutral); callback();
    assert(board_view(0).playing && !board_view(0).overlays && !board_view(0).audible);
}

static void verify_instrument_knobs(void)
{
    static const int32_t bass[7]={[0]=1,[2]=6,[3]=8,[4]=-21,[5]=1};
    static const int32_t perc[7]={[0]=1,[2]=-10,[3]=-20,[4]=-30,[5]=-40};
    static const int32_t rack[7]={[0]=1,[2]=3,[3]=-12,[4]=-14,[5]=-25};
    static const int32_t home[7]={[0]=1,[2]=3};
    static const int32_t back[7]={[0]=-1};
    Panel panel={0};
    board_init();
    put(BOARD_CUE,1,0);
    put(BOARD_MASTER,60,0);
    put(CP_START,0,0);
    callback();
    unsigned tempo=board_view(0).bpm;

    panel_input(&panel,0,0,bass); callback();
    BoardView v=board_view(0);
    assert(panel.page==PANEL_BASS && v.bass_style==BASS_ACID && v.bass_cut==50 &&
           v.bass_bite==53 && v.levels[5]==79);
    assert(v.playing && v.song==1 && v.bpm==tempo && v.master==60);
    panel_input(&panel,0,0,perc); callback();
    v=board_view(0);
    assert(panel.page==PANEL_PERC && v.overlay_levels[0]==90 && v.overlay_levels[1]==80 &&
           v.overlay_levels[2]==70 && v.overlay_levels[3]==60);
    assert(v.playing && v.song==1 && v.bpm==tempo && v.master==60 && v.levels[5]==79);
    PanelKeys map=panel_keys(&panel,v);
    assert(map.active==1u<<PANEL_VERSE_D4 && map.enabled==1u<<17);
    assert(panel_key_number(PANEL_VERSE_D4,map)==2 &&
           panel_key_number(PANEL_CONGA_CS4,map)==1 && !panel_key_number(0,map));
    v.next_part=PART_CHORUS;
    map=panel_keys(&panel,v);
    assert(map.queued==1u<<PANEL_CHORUS_F4);

    panel_input(&panel,0,0,rack); callback();
    v=board_view(0);
    assert(panel.page==PANEL_RACK && panel.rack_sound==3 && v.rack_strength==80 &&
           v.rack_accent==110 && v.rack_level==75);
    assert(v.playing && v.song==1 && v.bpm==tempo && v.master==60);
    panel_input(&panel,0,0,home); callback();
    assert(panel.page==PANEL_HOME && board_view(0).bpm==tempo+3);
    /* Band K4 is the PERC macro: all four lanes move together, clamped. */
    static const int32_t perc_macro[7]={[5]=-10}, perc_max[7]={[5]=100};
    panel_input(&panel,0,0,perc_macro); callback();
    v=board_view(0);
    assert(v.overlay_levels[0]==80 && v.overlay_levels[1]==70 &&
           v.overlay_levels[2]==60 && v.overlay_levels[3]==50 && v.master==60);
    panel_input(&panel,0,0,perc_max); callback();
    v=board_view(0);
    assert(v.overlay_levels[0]==100 && v.overlay_levels[3]==100);
    /* Band K3 is bass level; K4 no longer touches MASTER. */
    static const int32_t bass_level[7]={[4]=-9};
    panel_input(&panel,0,0,bass_level); callback();
    assert(board_view(0).levels[5]==70 && board_view(0).master==60);
    /* ALGORITHM is Energy on Band, Bass and Perc: queued for the phrase. */
    static const int32_t lift[7]={[1]=1}, more[7]={[1]=1};
    panel_input(&panel,0,0,lift); callback();
    v=board_view(0);
    assert(v.playing && (v.pending==1 || v.section==1));
    panel_input(&panel,0,0,more); callback();
    v=board_view(0);
    assert(v.pending==2 || v.section==2);
    panel_input(&panel,0,0,back); callback();
    assert(panel.page==PANEL_RACK && board_view(0).rack_level==75);
}

static void verify_live_cue_edges(void)
{
    static const int32_t neutral[7]={0};
    Panel panel={0};
    board_init();
    put(BOARD_CUE,0,0);
    put(CP_BPM,180,0);
    callback();
    panel_input(&panel,1u<<12,1u<<PANEL_VARIATION_E4,neutral); callback();
    assert(board_view(0).playing && board_view(0).part==PART_VERSE_B);
    for(unsigned i=0;i<1000 && board_view(0).position<28;++i) callback();
    assert(board_view(0).position>=28);
    panel_input(&panel,1u<<10,0,neutral); callback();
    assert(board_view(0).fill==1 && board_view(0).next_part==PART_NONE);
    panel_input(&panel,0,1u<<PANEL_CHORUS_F4,neutral); callback();
    assert(board_view(0).fill==1 && board_view(0).next_part==PART_CHORUS);
    for(unsigned i=0;i<150 && board_view(0).position>=28;++i) callback();
    assert(board_view(0).position<28 && board_view(0).part==PART_VERSE_B &&
           board_view(0).next_part==PART_CHORUS);
    for(unsigned i=0;i<1000 && board_view(0).next_part!=PART_NONE;++i) callback();
    assert(board_view(0).part==PART_CHORUS && !board_view(0).fill);

    put(CP_STOP,0,0); callback();
    put(BOARD_REPEAT,1,0);
    put(BOARD_PART,PART_INTRO,0);
    put(CP_START,0,0); callback();
    for(unsigned i=0;i<1000 && board_view(0).part==PART_INTRO;++i) callback();
    assert(board_view(0).song==0 && board_view(0).part==PART_VERSE_A);
    put(BOARD_PART,PART_END,0); callback();
    for(unsigned i=0;i<1000 && board_view(0).part!=PART_END;++i) callback();
    assert(board_view(0).song==0 && board_view(0).part==PART_END);
    for(unsigned i=0;i<1000 && board_view(0).playing;++i) callback();
    /* The Ending wins over the repeat count, then the show moves on. */
    assert(board_view(0).song==1 && !board_view(0).playing);

    put(BOARD_CUE,8,0);
    put(CP_BPM,180,0);
    put(CP_START,0,0);
    put(BOARD_CUE,1,0); callback();
    assert(board_view(0).next_song==1);
    put(BOARD_ENDING,0,0); callback();
    assert(board_view(0).next_song==SET_SLOTS && board_view(0).score_ending);
    for(unsigned i=0;i<550 && board_view(0).score_ending;++i) callback();
    assert(board_view(0).song==8 && board_view(0).score_form==PIANO_FORM_CODA);
}

static void verify_next_bar(void)
{
    static const int32_t enc[7]={0};
    Panel panel={0};
    board_init();
    put(BOARD_CUE,1,0);
    put(CP_BPM,180,0);
    put(BOARD_PART,PART_VERSE_A,0);
    put(CP_START,0,0); callback();
    const int32_t lift[7]={[1]=1}, base[7]={[1]=-1};
    panel_scan(&panel,0,0,lift,100);
    callback();
    assert(board_view(0).pending==1 && board_view(0).part==PART_VERSE_A);
    panel_scan(&panel,0,0,base,200);
    callback();
    assert(board_view(0).pending==3 && board_view(0).part==PART_VERSE_A);
    panel_scan(&panel,0,1u<<PANEL_CHORUS_F4,enc,1000);
    callback();
    assert(board_view(0).next_part==PART_CHORUS && !board_view(0).next_bar);
    panel_scan(&panel,0,0,enc,2401000);
    panel_scan(&panel,0,1u<<PANEL_CHORUS_F4,enc,4801000);
    callback();
    assert(board_view(0).next_part==PART_CHORUS && board_view(0).next_bar);
    for(unsigned i=0;i<510 && board_view(0).next_part<PART_NONE;++i) callback();
    BoardView v=board_view(0);
    assert(v.part==PART_CHORUS && v.position>=16 && v.position<20 && !v.next_bar);

    panel_scan(&panel,0,0,enc,9601000);
    panel_scan(&panel,0,1u<<PANEL_VERSE_D4,enc,12001000);
    callback();
    panel_scan(&panel,0,0,enc,14401000);
    panel_scan(&panel,0,1u<<PANEL_VERSE_D4,enc,24001000);
    callback();
    assert(board_view(0).next_part==PART_VERSE_A && !board_view(0).next_bar);
    panel_scan(&panel,0,0,enc,26401000);
    panel_scan(&panel,0,1u<<PANEL_VERSE_D4,enc,28801000);
    callback();
    assert(board_view(0).next_bar);
    put(BOARD_CUE,2,0); callback();
    assert(board_view(0).next_song==2 && board_view(0).next_part==PART_NONE && !board_view(0).next_bar);
    put(CP_STOP,0,0); callback();
    assert(!board_view(0).playing && board_view(0).next_song==SET_SLOTS);
}

static void verify_score_cue_replacement(void)
{
    static const int32_t neutral[7]={0};
    Panel panel={0};
    board_init();
    put(BOARD_CUE,8,0);
    put(CP_BPM,180,0);
    put(CP_START,0,0);
    put(BOARD_ENDING,0,0);
    callback();
    assert(board_view(0).next_part==PART_END && board_view(0).score_ending);

    /* Browse to another song, then back to the current score. The old
     * coda request must not reappear when the pending song is canceled. */
    put(BOARD_CUE,1,0);
    callback();
    assert(board_view(0).next_song==1 && board_view(0).next_part==PART_NONE);
    assert(!board_view(0).score_ending && !panel_keys(&panel,board_view(0)).queued);
    put(BOARD_CUE,8,0);
    for(unsigned i=0;i<400;++i) callback();
    assert(board_view(0).next_song==SET_SLOTS && board_view(0).score_form==PIANO_FORM_INTRO);

    /* The white coda key and SEQ share the cue, and a Hold event arriving
     * in the same scan cannot make the coda loop. */
    put(BOARD_HOLD,0,0);
    callback();
    assert(board_view(0).score_hold);
    panel_input(&panel,0,(1u<<PANEL_END_A4)|(1u<<panel_layer_keys[1]),neutral);
    callback();
    BoardView v=board_view(0);
    PanelKeys keys=panel_keys(&panel,v);
    assert(v.score_ending && v.next_part==PART_END && !v.score_hold);
    assert(keys.queued==(1u<<PANEL_END_A4) && !(keys.upper&(1u<<panel_layer_keys[1])));
    for(unsigned i=0;i<240 && board_view(0).score_ending;++i) callback();
    assert(board_view(0).score_form==PIANO_FORM_CODA && !board_view(0).score_hold);

    put(CP_STOP,0,0);
    callback();
    put(CP_START,0,0);
    put(BOARD_ENDING,0,0);
    for(unsigned i=0;i<230;++i) callback(); /* Count-in is about 230 blocks. */
    assert(board_view(0).score_form==PIANO_FORM_CODA && !board_view(0).score_ending);
    put(CP_STOP,0,0);
    callback();
    assert(!board_view(0).playing && board_view(0).next_part==PART_NONE);
}

static void verify_piano(void)
{
    static const int32_t neutral[7]={0};
    Panel panel={0};
    char face[FACE_LINES][FACE_COLS+1];
    board_init();
    panel_input(&panel,1u<<7,1u<<8,neutral);
    callback();
    assert(board_view(0).piano && board_view(0).song==8 && !board_view(0).playing);
    put(BOARD_MASTER,100,0);
    const int32_t tempo[7]={[2]=92};
    panel_input(&panel,0,0,tempo);
    callback();
    assert(board_view(0).bpm==180);
    put(CP_START,0,0);
    callback();
    panel_face(&panel,board_view(0),face);
    assert(strstr(face[2],"COUNT IN") && strstr(face[6],"GUIDE OFF"));
    /* FX no longer doubles as GUIDE; black key 1 is GUIDE on a score. */
    panel_input(&panel,1u<<2,0,neutral);
    assert(!board_view(0).score_guide && panel.page==PANEL_MIX);
    panel_input(&panel,1u<<8,0,neutral);
    PanelKeys score_map=panel_keys(&panel,board_view(0));
    assert((score_map.upper&(1u<<PANEL_CONGA_CS4)) && !(score_map.enabled&(1u<<PANEL_CONGA_CS4)));
    panel_input(&panel,0,1u<<PANEL_CONGA_CS4,neutral); callback();
    assert(board_view(0).score_guide && board_view(0).overlays==0);
    score_map=panel_keys(&panel,board_view(0));
    assert(score_map.enabled&(1u<<PANEL_CONGA_CS4));
    panel_input(&panel,0,0,neutral);

    /* 48 ticks of count-in, 12 ticks of pickup and 3828 of printed music.
     * At 180 BPM, one score tick takes exactly 612.5 frames. */
    unsigned blocks=2, peak=0, last_bar=0;
    while(board_view(0).playing && blocks<19000) {
        unsigned completed=half;
        callback();
        ++blocks;
        BoardView v=board_view(0);
        /* At the score's end the show moves on to the next slot. */
        if(!v.playing) break;
        assert(v.score_bar>=last_bar);
        last_bar=v.score_bar;
        for(unsigned i=0;i<BOARD_FRAMES;++i) {
            int32_t sample=board_dma[completed][2*i]/128;
            unsigned magnitude=(unsigned)(sample<0 ? -sample : sample);
            if(magnitude>peak) peak=magnitude;
        }
    }
    assert(blocks==18605 && last_bar==80 && peak>4000 && !board_stats.rail_samples);
    assert(!board_view(0).playing && board_view(0).song!=8);
    put(BOARD_CUE,8,0); put(CP_BPM,180,0); callback();
    assert(board_view(0).song==8);

    put(CP_START,0,0);
    put(BOARD_HOLD,0,0);
    callback();
    unsigned maximum_bar=0;
    for(unsigned i=0;i<2500;++i) {
        callback();
        unsigned bar=board_view(0).score_bar;
        if(bar>maximum_bar) maximum_bar=bar;
    }
    assert(maximum_bar==2 && board_view(0).score_hold);
    put(BOARD_HOLD,0,0);
    for(unsigned i=0;i<500;++i) callback();
    assert(!board_view(0).score_hold && board_view(0).score_bar>2);
    put(BOARD_HOLD,0,0);
    panel_input(&panel,1u<<11,0,neutral);
    for(unsigned i=0;i<270;++i) callback();
    assert(board_view(0).score_form==PIANO_FORM_CODA && !board_view(0).score_hold);
    panel_face(&panel,board_view(0),face);
    assert(strstr(face[5],"CODA PLAYING"));
    put(BOARD_CUE,1,0);
    for(unsigned i=0;i<270;++i) callback();
    assert(board_view(0).song==1 && !board_view(0).piano && board_view(0).playing);
    put(CP_BPM,180,0);
    put(BOARD_CUE,8,0);
    for(unsigned i=0;i<940;++i) callback();
    assert(board_view(0).song==8 && board_view(0).piano && board_view(0).playing);
    assert(board_view(0).score_form==PIANO_FORM_INTRO);
    put(CP_STOP,0,0); callback();
    assert(!board_view(0).playing && board_view(0).piano);
    put(BOARD_SAVE,15,0); callback();
    put(BOARD_CUE,0,0); callback();
    put(BOARD_CUE,15,0); callback();
    assert(board_view(0).song==15 && board_view(0).piano && !board_view(0).playing);
    panel_input(&panel,0,1u<<PANEL_CONGA_CS4,neutral); callback();
    assert(board_view(0).score_guide);
    panel_input(&panel,0,0,neutral);
    put(BOARD_SAVE,15,0); callback();
    put(BOARD_CUE,8,0); callback();
    assert(!board_view(0).score_guide);
    put(BOARD_CUE,15,0); callback();
    assert(board_view(0).score_guide);
}

static void next_score_bar(void)
{
    unsigned bar=board_view(0).score_bar;
    for(unsigned i=0;i<2000;++i) {
        callback();
        if(board_view(0).score_bar!=bar) return;
    }
    assert(!"score did not reach next bar");
}
static void verify_score_transpose(void)
{
    const int32_t up[7]={[5]=1}, down[7]={[5]=-1}, high[7]={[5]=100}, low[7]={[5]=-100};
    Panel p={.page=PANEL_HOME};
    board_init();put(BOARD_CUE,9,0);callback();
    panel_input(&p,0,0,down);callback();assert(board_root(board_view(0))==11);
    panel_input(&p,0,0,up);callback();assert(board_root(board_view(0))==0);
    panel_input(&p,0,0,high);callback();assert(board_root(board_view(0))==5);
    panel_input(&p,0,0,low);callback();assert(board_root(board_view(0))==6);
    put(CP_ROOT,0,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
    /* Count-in is pitched independently; cue is owned by the first written bar. */
    panel_input(&p,0,0,down);callback();
    assert(board_root(board_view(0))==0 && board_next_root(board_view(0))==11);
    panel_input(&p,0,0,up);callback();assert(board_next_root(board_view(0))==12);
    panel_input(&p,0,0,down);callback();panel_input(&p,0,0,down);callback();
    assert(board_next_root(board_view(0))==10);
    next_score_bar();assert(board_root(board_view(0))==10 && board_next_root(board_view(0))==12);
    put(BOARD_HOLD,0,0);put(CP_ROOT,3,0);callback();next_score_bar();
    assert(board_root(board_view(0))==3 && board_view(0).score_hold);
    put(CP_ROOT,4,0);put(BOARD_PART,PART_CHORUS,0);callback();
    for(unsigned i=0;i<2000 && board_view(0).next_part<PART_NONE;++i) callback();
    assert(board_root(board_view(0))==4 && board_next_root(board_view(0))==12);
    put(CP_ROOT,5,0);callback();put(CP_STOP,0,0);callback();
    assert(board_root(board_view(0))==4 && board_next_root(board_view(0))==12);
    put(BOARD_SAVE,15,0);callback();put(BOARD_CUE,8,0);callback();assert(board_root(board_view(0))==0);
    put(BOARD_CUE,15,0);callback();assert(board_root(board_view(0))==4);
    put(CP_START,0,0);put(CP_ROOT,7,0);put(BOARD_CUE,8,0);callback();
    for(unsigned i=0;i<2000 && board_view(0).song==15;++i) callback();
    assert(board_view(0).song==8 && board_root(board_view(0))==0 && board_next_root(board_view(0))==12);
    put(CP_STOP,0,0);callback();
}

static void verify_score_controls(void)
{
    _Static_assert(sizeof(BoardView)==76 && __builtin_offsetof(BoardView,piano)==31 &&
        __builtin_offsetof(BoardView,score_hold)==34 && __builtin_offsetof(BoardView,score_ending)==35 &&
        __builtin_offsetof(BoardView,score_guide)==36 && __builtin_offsetof(BoardView,score_percussion)==73,
        "strict stage probe view layout");
    const int32_t up[7]={[3]=1,[4]=1}, down[7]={[3]=-1,[4]=-1};
    const int32_t all[7]={[2]=17,[3]=17,[4]=17,[5]=17};
    Panel p={0};
    char face[FACE_LINES][FACE_COLS+1];
    board_init(); put(BOARD_CUE,9,0); callback();
    assert(board_view(0).score_percussion);
    /* Encoders set ON/OFF; HOLD cannot arm while stopped or override CODA. */
    panel_input(&p,0,0,up); callback();
    assert(board_view(0).score_guide && !board_view(0).score_hold);
    put(CP_START,0,0); callback();
    panel_input(&p,0,0,up); callback();
    assert(board_view(0).score_guide && board_view(0).score_hold);
    panel_input(&p,0,0,up); callback();
    assert(board_view(0).score_guide && board_view(0).score_hold);
    panel_input(&p,0,0,down); callback();
    assert(!board_view(0).score_guide && !board_view(0).score_hold);
    put(BOARD_ENDING,0,0); callback();
    panel_input(&p,0,0,up); callback();
    assert(board_view(0).score_ending && !board_view(0).score_hold);
    BoardView before=board_view(0);
    p.page=PANEL_BASS;
    panel_input(&p,0,0,all); callback();
    BoardView after=board_view(0);
    assert(after.bass_cut==before.bass_cut && after.bass_bite==before.bass_bite &&
           after.bass_style==before.bass_style && after.levels[5]==before.levels[5]);
    put(CP_STOP,0,0); callback();
    panel_face(&p,board_view(0),face);
    assert(strstr(face[3],"SCORE ACCOMPANIMENT") && strstr(face[4],"NO SEPARATE BASS"));
    p.page=PANEL_PERC;
    panel_input(&p,0,0,all); callback();
    after=board_view(0);
    assert(!memcmp(before.overlay_levels,after.overlay_levels,4));
    panel_face(&p,after,face);
    assert(strstr(face[3],"BOLERO BACKING") && strstr(face[4],"CONGA + MARACA"));
    unsigned bolero_key=1u<<panel_layer_keys[2];
    assert(panel_keys(&p,after).enabled&bolero_key);
    panel_input(&p,0,bolero_key,all);callback();
    assert((panel_keys(&p,board_view(0)).upper&bolero_key) &&
           !(panel_keys(&p,board_view(0)).enabled&bolero_key));
    panel_face(&p,board_view(0),face);
    assert(strstr(face[3],"BOLERO MUTED"));
    put(BOARD_CUE,8,0); callback();
    assert(!board_view(0).score_percussion);
    panel_face(&p,board_view(0),face);
    assert(strstr(face[3],"NO AUTO RHYTHM") && !(panel_keys(&p,board_view(0)).upper&bolero_key));
    put(BOARD_CUE,9,0);callback();
    assert(board_score_perc_on(board_view(0)));
    put(BOARD_CUE,0,0); callback();
    before=board_view(0); p.page=PANEL_BASS;
    panel_input(&p,0,0,all); callback(); after=board_view(0);
    assert(after.bass_cut==before.bass_cut && after.bass_bite==before.bass_bite &&
           after.bass_style==before.bass_style && after.levels[5]==before.levels[5]);
    panel_face(&p,after,face);
    assert(strstr(face[3],"DRUM ENSEMBLE"));
}

static void verify_golondrina(void)
{
    board_init();
    put(BOARD_MASTER,100,0);
    put(BOARD_CUE,9,0);
    put(CP_BPM,180,0);
    put(CP_START,0,0);
    callback();
    BoardView v=board_view(0);
    assert(v.piano==2 && v.score_beats==4 && v.score_has_guide && !v.score_guide && v.score_form==PIANO_FORM_COUNT_IN);
    unsigned blocks=1, beats=0;
    while(v.score_form==PIANO_FORM_COUNT_IN && blocks<500) {
        beats|=1u<<(v.position/4u);
        callback(); ++blocks; v=board_view(0);
    }
    assert(beats==15 && blocks==460 && v.score_form==PIANO_FORM_INTRO && !board_stats.rack_peak);
    put(BOARD_GUIDE,0,0); callback(); ++blocks; v=board_view(0);
    assert(v.score_guide);
    unsigned final_bar=0;
    while(v.playing && blocks<19000) {
        callback(); ++blocks; v=board_view(0);
        if(v.piano) {
            assert(v.score_bar>=final_bar);
            final_bar=v.score_bar;
        }
    }
    /* 96 count-in ticks + 3810 unfolded score ticks at 612.5 frames per tick. */
    assert(blocks==(2392425u/128u)+1u && final_bar==40 && !board_stats.rail_samples);
    assert(board_stats.rack_peak);
    callback();
    for(unsigned i=0;i<BOARD_WORDS;++i) assert(board_dma[0][i]==0 && board_dma[1][i]==0);
    /* The score's end moved the show on; return to La Golondrina. */
    assert(board_view(0).song!=9);
    put(BOARD_CUE,9,0); put(CP_BPM,180,0); callback();

    put(CP_START,0,0);
    put(BOARD_CUE,8,0);
    for(unsigned i=0;i<930;++i) callback();
    assert(board_view(0).song==8 && board_view(0).piano==1 && board_view(0).score_beats==2);
    put(BOARD_CUE,9,0);
    for(unsigned i=0;i<700 && board_view(0).song==8;++i) callback();
    assert(board_view(0).song==9 && board_view(0).piano==2 && board_view(0).score_beats==4);
    put(BOARD_GUIDE,0,0); callback();
    assert(board_view(0).score_guide);
    put(BOARD_GUIDE,0,0); callback();
    assert(!board_view(0).score_guide);
    put(BOARD_ENDING,0,0);
    for(unsigned i=0;i<1000 && board_view(0).score_form!=PIANO_FORM_CODA;++i) callback();
    assert(board_view(0).score_form==PIANO_FORM_CODA && board_view(0).score_bar==37);

    board_init();put(BOARD_CUE,9,0);put(BOARD_MASTER,100,0);put(CP_BPM,180,0);
    put(BOARD_SCORE_PERC,0,0);put(BOARD_PART,PART_VERSE_A,0);put(CP_START,0,0);
    unsigned audible=0;
    for(unsigned i=0;i<10;++i) {
        unsigned done=half;callback();
        for(unsigned j=0;j<BOARD_WORDS;++j) audible|=board_dma[done][j]!=0;
    }
    BoardView muted=board_view(0);
    assert(audible && muted.playing && muted.score_form==PIANO_FORM_VERSE && !board_stats.rack_peak);
    put(BOARD_SCORE_PERC,0,0);callback();
    assert(board_score_perc_on(board_view(0)) && !board_stats.rack_peak);
    for(unsigned i=0;i<150;++i) callback();
    assert(board_stats.rack_peak && board_view(0).score_bar==muted.score_bar);
    put(BOARD_SCORE_PERC,0,0);callback();
    for(unsigned i=0;i<180;++i) callback();
    board_stats.rack_peak=0;
    put(BOARD_RACK,0,90);callback();
    assert(board_stats.rack_peak && board_view(0).playing && !board_score_perc_on(board_view(0)));
    put(CP_STOP,0,0);callback();
    assert(!board_score_perc_on(board_view(0)));
}

static void piano_audition(const char *filename,unsigned slot,unsigned guide)
{
    FILE *file=fopen(filename,"wb");
    assert(file);
    board_init();
    put(BOARD_MASTER,100,0);
    put(BOARD_CUE,slot,0);
    if(guide) put(BOARD_GUIDE,0,0);
    put(CP_START,0,0);
    do { write_block(file); } while(board_view(0).playing);
    assert(!fclose(file));
}

static void verify_choke(void)
{
    static const unsigned pairs[][2]={{0,1},{2,3},{7,8},{9,10},{11,12}};
    board_init();
    put(BOARD_MASTER,100,0);
    callback();
    for(unsigned pair=0;pair<5;++pair) {
        for(unsigned order=0;order<2;++order) {
            put(CP_STOP,0,0); callback();
            unsigned first=pairs[pair][order], second=pairs[pair][1-order];
            put(BOARD_RACK,first,127); callback();
            put(BOARD_RACK,second,127);
            unsigned completed=half;
            callback();
            for(unsigned i=0;i<BOARD_FRAMES;++i) {
                int32_t expected=compas_pcm727[clips[second].offset+i]*127/512*128;
                assert(board_dma[completed][2*i]==expected);
            }
        }
    }
    put(CP_STOP,0,0); callback();
    put(BOARD_ASSIGN,0,1); put(CP_START,0,0); callback();
    assert(board_playing727(0));
    put(BOARD_RACK,1,127); callback();
    assert(!board_playing727(0));
    put(CP_STOP,0,0); callback();
    put(BOARD_RACK,13,127); put(BOARD_RACK,14,127);
    unsigned completed=half;
    callback();
    for(unsigned i=0;i<BOARD_FRAMES;++i) {
        int32_t expected=compas_pcm727[clips[13].offset+i]*127/512;
        expected+=compas_pcm727[clips[14].offset+i]*127/512;
        assert(board_dma[completed][2*i]==expected*128);
    }
}

static void verify_rack_accents(void)
{
    const unsigned sound=6; /* Low timbale has the largest reference correction. */
    const unsigned control=124, level=76;
    const unsigned gain=((control*level/100u)*accent_gain_q12[sound]+2048u)>>12;
    board_init();
    put(BOARD_MASTER,100,0);
    put(BOARD_RACK_CONTROL,RACK_LEVEL,level);
    put(BOARD_ACCENT,1,0);
    put(BOARD_SEQ_STEP,sound,0);
    callback();

    for(unsigned route=0;route<3;++route) {
        put(CP_STOP,0,0);
        callback();
        if(route==0) put(BOARD_RACK,sound,0);
        else if(route==1) board_sequence_step(0);
        else put(BOARD_RACK,sound,control);
        unsigned completed=half;
        callback();
        unsigned expected_gain=route==2 ? control*level/100u : gain;
        for(unsigned i=0;i<BOARD_FRAMES;++i) {
            int32_t expected=compas_pcm727[clips[sound].offset+i]*(int32_t)expected_gain/512*128;
            assert(board_dma[completed][2*i]==expected);
        }
    }
}

static void verify_regional(void)
{
    /* Exercise every appended factory slot through the real callback and kit
     * routes, then a phrase-boundary change between all six styles. */
    static const unsigned ids[]={10,11,9,12,13,14};
    for(unsigned slot=10;slot<16;++slot) {
        board_init();
        put(BOARD_CUE,slot,0); callback();
        BoardView v=board_view(0);
        assert(v.song==slot && v.study==ids[slot-10] && !v.piano && !v.feel);
        put(BOARD_MASTER,80,0);
        put(BOARD_KIT,1,0); callback();
        put(BOARD_ASSIGN,5,1); callback();
        assert(board_view(0).result==CP_NO_ROLE); /* bass stays a bass */
        for(unsigned lane=0;lane<4;++lane) { put(BOARD_OVERLAY,lane,0); callback(); }
        put(CP_START,0,0); callback();
        for(unsigned section=0;section<3;++section) {
            put(CP_SECTION,section,0); callback();
            if(section==1) { put(CP_FILL,0,0); callback(); }
            unsigned peak=0;
            for(unsigned block=0;block<2400;++block) {
                unsigned done=half; callback();
                for(unsigned frame=0;frame<BOARD_FRAMES;++frame) {
                    int32_t n=board_dma[done][2*frame]/128;
                    unsigned mag=(unsigned)(n<0?-n:n);
                    if(mag>peak) peak=mag;
                }
            }
            assert(peak>100 && !board_stats.rail_samples);
            assert(!board_view(0).fault && board_view(0).section==section);
        }
        unsigned next=slot==15?10:slot+1;
        put(BOARD_CUE,next,0); callback();
        for(unsigned block=0;block<2400 && board_view(0).song!=next;++block) callback();
        assert(board_view(0).song==next && board_view(0).playing);
        put(CP_STOP,0,0); callback();
        assert(board_view(0).next_song==SET_SLOTS);
    }
}

static void verify_rack_editor(void)
{
    static const int32_t neutral[7]={0}, next_sound[7]={[2]=1};
    for(unsigned sound=0;sound<RACK_SOUNDS;++sound) {
        board_init();
        put(CP_STUDY,1,0); put(BOARD_MASTER,100,0); callback();
        put(BOARD_SEQ_SELECT,sound,0); put(BOARD_SEQ_STEP,sound,0); callback();
        assert(board_view(0).rack_steps==1 && board_view(0).rack_sequence);
        put(BOARD_SEQ_ENABLE,2,0); put(CP_START,0,0);
        unsigned done=half; callback();
        for(unsigned frame=0;frame<BOARD_FRAMES;++frame) {
            int32_t expected=compas_pcm727[clips[sound].offset+frame]*92/512*128;
            assert(board_dma[done][2*frame]==expected); /* truly percussion only */
        }
        put(CP_STOP,0,0); callback();
        assert(board_view(0).rack_steps==1); /* STOP preserves the edit */
        put(BOARD_ACCENT,1,0); put(BOARD_SEQ_STEP,sound,0); callback();
        assert(board_view(0).rack_steps==1 && board_view(0).rack_accents==1);
        put(BOARD_SEQ_STEP,sound,0); callback();
        assert(board_view(0).rack_steps==1 && !board_view(0).rack_accents);
        put(BOARD_ACCENT,0,0); put(BOARD_SEQ_STEP,sound,0); callback();
        assert(!board_view(0).rack_steps);
    }
    board_init();
    put(BOARD_CUE,1,0); callback();
    uint8_t original[CP_PROJECT_BYTES];cp_snapshot();memcpy(original,cp_blob(),sizeof original);
    Panel panel={.page=PANEL_RACK};
    panel_input(&panel,1u<<11,0,neutral);callback();
    assert(panel.rack_edit && panel.page==PANEL_RACK);
    panel_input(&panel,0,1u<<3,neutral);callback();
    assert(board_view(0).rack_steps==8);
    panel_input(&panel,0,0,next_sound);callback();
    assert(panel.rack_sound==1 && !board_view(0).rack_steps);
    panel_input(&panel,1u<<1,0,neutral);callback();
    panel_input(&panel,0,1u<<15,neutral);callback();
    assert(board_view(0).rack_steps==0x80000000u && panel.bar==16);
    PanelKeys map=panel_keys(&panel,board_view(0));
    assert(map.pads==0xffffu && map.enabled==0x8000u);
    panel_input(&panel,1u<<2,0,neutral);callback();
    assert(board_view(0).rack_sequence==2);
    panel_input(&panel,1u<<11,0,neutral);callback();
    assert(!panel.rack_edit && board_view(0).rack_sequence==2);
    cp_snapshot();assert(!memcmp(original,cp_blob(),sizeof original));
    put(BOARD_CUE,7,0);callback(); /* 2/4 phrase has only sixteen steps */
    assert(!board_view(0).rack_sequence && !board_rhythm_only);
    put(BOARD_SEQ_STEP,1,16);callback();assert(board_view(0).result==CP_RANGE);
    put(BOARD_SEQ_SELECT,15,0);callback();assert(board_view(0).result==CP_RANGE);
    put(BOARD_CUE,8,0);callback();
    put(BOARD_SEQ_STEP,1,0);callback();assert(board_view(0).result==CP_RANGE);
    put(BOARD_SEQ_ENABLE,2,0);callback();assert(board_view(0).result==CP_RANGE);

    board_init();put(BOARD_CUE,1,0);put(BOARD_MASTER,100,0);callback();
    for(unsigned sound=0;sound<RACK_SOUNDS;++sound)
        for(unsigned step=0;step<32;step+=2) {put(BOARD_SEQ_STEP,sound,step);callback();}
    for(unsigned lane=0;lane<4;++lane) {put(BOARD_OVERLAY,lane,0);callback();}
    put(BOARD_KIT,1,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
    for(unsigned i=0;i<2500;++i)callback();
    assert(board_stats.rack_peak==10 && !board_stats.rail_samples && !board_view(0).fault);
    put(BOARD_SEQ_ENABLE,0,0);callback();
    put(CP_STOP,0,0);callback();
    assert(!board_view(0).playing && !board_rhythm_only);
}

static void next_board_phrase(void)
{
    unsigned previous=board_view(0).position;
    for(unsigned guard=0;guard<2200;++guard) {
        callback();
        BoardView v=board_view(0);
        if(v.position<previous || !v.playing) return;
        previous=v.position;
    }
    assert(!"phrase did not arrive");
}

static void verify_groove_count_in(void)
{
    static const int32_t count_knob[7]={[5]=1};
    Panel panel={.page=PANEL_SET};
    board_init();
    assert(!board_count_enabled(board_view(0)));
    panel_input(&panel,0,0,count_knob);
    callback();
    assert(board_count_enabled(board_view(0)));
    put(BOARD_SAVE,0,0); callback();
    put(BOARD_CUE,1,0); callback();
    assert(!board_count_enabled(board_view(0)));
    put(BOARD_CUE,0,0); callback();
    assert(board_count_enabled(board_view(0)));

    put(CP_BPM,120,0); put(BOARD_REPEAT,1,0); put(CP_START,0,0); callback();
    assert(board_count_active(board_view(0)) && !board_view(0).phrases_left);
    assert(cp_pcm()[0]!=0 && cp_pcm()[100]==0);
    for(unsigned f=BOARD_FRAMES; f<4u*22050u; ++f) {
        assert(cp_render(1)==CP_OK);
        if(f%22050u==0) assert(cp_pcm()[0]!=0);
        if(f%22050u>=96) assert(cp_pcm()[0]==0);
    }
    assert(cp_board_counting());
    put(BOARD_PART,PART_CHORUS,0); callback();
    BoardView v=board_view(0);
    assert(!board_count_active(v) && v.part==PART_CHORUS && v.phrases_left==1);
    assert(cp_status(CP_PHRASE)==0 && v.position==0);

    put(CP_STOP,0,0); callback();
    put(CP_START,0,0); callback();
    assert(board_count_active(board_view(0)));
    put(CP_STOP,0,0); callback();
    assert(!board_view(0).playing && !board_count_active(board_view(0)));
    for(unsigned i=0;i<BOARD_FRAMES;++i) assert(!cp_pcm()[i]);

    put(BOARD_GROOVE_CUE,3,0); callback(); /* Conjunto counts in two. */
    put(CP_BPM,120,0); put(BOARD_COUNT_IN,1,0); put(CP_START,0,0); callback();
    for(unsigned f=BOARD_FRAMES; f<2u*22050u; ++f) {
        assert(cp_render(1)==CP_OK);
        if(f%22050u==0) assert(cp_pcm()[0]!=0);
        if(f%22050u>=96) assert(cp_pcm()[0]==0);
    }
    assert(cp_board_counting());
    callback();
    assert(!board_count_active(board_view(0)) && board_view(0).playing);
}
static void verify_rack_roll(void)
{
    const unsigned counts[]={0,2,3,4,8};
    for(unsigned mode=ROLL_EIGHTH;mode<=ROLL_THIRTYSECOND;++mode) {
        board_init();
        put(CP_STUDY,1,0); put(CP_BPM,120,0);
        put(BOARD_ROLL,mode,0); put(BOARD_ROLL_KEYS,1,0); put(CP_START,0,0);
        unsigned previous=0, events=0;
        for(unsigned block=0; block<172; ++block) {
            callback();
            RackFeedback event=board_rack_feedback();
            if(event.serial!=previous && event.manual) {
                unsigned expected=(events*22050u+counts[mode]-1u)/counts[mode]/BOARD_FRAMES;
                assert(event.manual==1 && block==expected);
                ++events;
            }
            previous=event.serial;
        }
        assert(events==counts[mode]);
        put(CP_STOP,0,0); callback();
        assert(!board_roll_keys);
    }
}
static void verify_rack_record(void)
{
    board_init();
    put(CP_STUDY,1,0); put(CP_BPM,120,0); put(BOARD_REC,4,0); put(CP_START,0,0);
    for(unsigned i=0;i<30;++i) callback();
    assert(board_recording(board_view(0)) && board_view(0).position==0);
    put(BOARD_ACCENT,1,0); put(BOARD_RACK,3,0); callback();
    BoardView v=board_view(0);
    assert(v.rack_sequence==1 && v.rack_sequence_sound==3 && v.rack_steps==2 && v.rack_accents==2);
    RackFeedback event=board_rack_feedback();
    assert(event.manual==(1u<<3));
    while(board_view(0).position==0) callback();
    assert(board_rack_feedback().serial==event.serial);
    for(unsigned i=0;i<1500 && board_rack_feedback().serial==event.serial;++i) callback();
    assert(board_view(0).position==1);
    assert(board_rack_feedback().hits==(1u<<3) && !board_rack_feedback().manual);
    put(CP_STOP,0,0); callback();
    assert(!board_recording(board_view(0)) && board_view(0).rack_steps==2);
}
static void verify_drum_loop_transport(void)
{
    const int32_t neutral[7]={0};
    char face[FACE_LINES][FACE_COLS+1];
    for(unsigned resume=0;resume<2;++resume) {
        board_init();put(BOARD_CUE,1,0);put(CP_BPM,180,0);
        put(BOARD_REPEAT,2,0);put(BOARD_RHYTHM_CUE,13,0);callback();
        put(BOARD_SEQ_ENABLE,2,0);put(CP_START,0,0);callback();
        for(unsigned phrase=0;phrase<5;++phrase) {
            next_board_phrase();
            assert(board_view(0).song==1 && board_view(0).playing);
            assert(board_view(0).phrases==2 && board_view(0).phrases_left==0 && board_view(0).rhythm==13);
        }
        put(BOARD_RHYTHM_CUE,11,0);callback();next_board_phrase();
        assert(board_view(0).song==1 && board_view(0).rhythm==11);
        /* Neither elapsed loop phrases nor browsing consumes song repeats. */
        put(BOARD_SEQ_ENABLE,resume,0);callback();
        next_board_phrase();assert(board_view(0).song==1 && board_view(0).phrases_left==1);
        next_board_phrase();assert(board_view(0).song==2 && board_view(0).playing);
    }
    board_init();put(BOARD_CUE,1,0);put(CP_BPM,180,0);put(BOARD_REPEAT,1,0);
    put(BOARD_RHYTHM_CUE,13,0);put(BOARD_SEQ_ENABLE,2,0);callback();
    put(BOARD_SAVE,4,0);callback();put(BOARD_CUE,4,0);put(CP_START,0,0);callback();
    for(unsigned i=0;i<3;++i) {next_board_phrase();assert(board_view(0).song==4);}
    put(BOARD_CUE,3,0);callback();next_board_phrase();
    assert(board_view(0).song==3 && board_view(0).playing && !board_rhythm_only);
    /* An explicit conductor Ending still ends the loop; STOP still cancels. */
    put(CP_BPM,180,0);put(BOARD_RHYTHM_CUE,13,0);put(BOARD_SEQ_ENABLE,2,0);
    put(BOARD_PART,PART_END,0);callback();next_board_phrase();
    assert(board_view(0).part==PART_END && board_view(0).playing);
    next_board_phrase();assert(!board_view(0).playing);
    /* After an ending the show moves on: the next occupied slot is NOW. */
    assert(board_view(0).song==4);
    put(CP_START,0,0);callback();put(BOARD_RHYTHM_CUE,8,0);callback();
    put(CP_STOP,0,0);callback();assert(board_view(0).next_rhythm==BOARD_CUSTOM_RHYTHM);

    Panel p={.page=PANEL_RACK};
    for(unsigned mode=0;mode<3;++mode) {
        panel_input(&p,1u<<2,0,neutral);callback();
        assert(p.page==PANEL_RACK && !p.rack_edit && board_view(0).rack_sequence==mode);
        panel_face(&p,board_view(0),face);
        assert(strstr(face[6],mode==2?"DRUM LOOP":mode?"LAYER ON":"LAYER OFF"));
    }
    panel_input(&p,1u<<11,0,neutral);callback();
    panel_input(&p,1u<<2,0,neutral);callback();
    assert(p.rack_edit && board_view(0).rack_sequence==0);
}

static void verify_live_clave(void)
{
    _Static_assert(sizeof(BoardView)==76,"Clave uses existing view padding");
    _Static_assert(__builtin_offsetof(BoardView,clave)==39,"Existing view offsets stay fixed");
    _Static_assert(__builtin_offsetof(BoardView,steps)==40,"Step view ABI");
    const int32_t neutral[7]={0};Panel p={.page=PANEL_PERC};
    for(unsigned study=2;study<=6;study+=4) {
        board_init();put(BOARD_GROOVE_CUE,study,0);callback();
        assert(board_clave(board_view(0))==0 && board_next_clave(board_view(0))==2);
        unsigned events[CP_ROLES][32];
        for(unsigned r=0;r<CP_ROLES;++r) for(unsigned n=0;n<32;++n) events[r][n]=cp_step(r,n);
        panel_input(&p,1u<<1,0,neutral);callback();
        assert(board_clave(board_view(0))==1 && board_next_clave(board_view(0))==2);
        for(unsigned r=0;r<CP_ROLES;++r) for(unsigned n=0;n<32;++n)
            assert(cp_step(r,n)==events[r][(n+16u)%32u]);
        put(BOARD_SEQ_SELECT,3,0);put(BOARD_SEQ_STEP,3,0);put(BOARD_SEQ_STEP,3,22);callback();
        uint32_t rack=board_view(0).rack_steps;
        put(CP_BPM,180,0);put(CP_START,0,0);callback();
        panel_input(&p,1u<<0,0,neutral);callback();
        assert(board_clave(board_view(0))==1 && board_next_clave(board_view(0))==0);
        panel_input(&p,1u<<1,0,neutral);callback();assert(board_next_clave(board_view(0))==2);
        panel_input(&p,(1u<<0)|(1u<<1),0,neutral);callback();assert(board_next_clave(board_view(0))==2);
        put(CP_CLAVE,0,0);put(CP_ROOT,7,0);put(BOARD_PART,PART_CHORUS,0);callback();
        next_board_phrase();
        assert(board_clave(board_view(0))==0 && board_next_clave(board_view(0))==2);
        assert(board_root(board_view(0))==7 && board_view(0).part==PART_CHORUS);
        assert(board_view(0).rack_steps==rack); /* No implicit rewrite of the user's grid. */
        put(CP_CLAVE,1,0);callback();put(CP_STOP,0,0);callback();
        assert(board_clave(board_view(0))==0 && board_next_clave(board_view(0))==2);
        put(CP_CLAVE,1,0);put(BOARD_SAVE,4,0);callback();
        put(BOARD_GROOVE_CUE,1,0);callback();put(BOARD_CUE,4,0);callback();
        assert(board_clave(board_view(0))==1 && board_view(0).study==study);
        cp_snapshot();assert(cp_blob()[10]==1);
        put(CP_START,0,0);put(CP_CLAVE,0,0);put(BOARD_CUE,1,0);callback();next_board_phrase();
        assert(board_view(0).study==1 && board_clave(board_view(0))==0 && board_next_clave(board_view(0))==2);
    }
    board_init();put(BOARD_GROOVE_CUE,2,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
    while(board_view(0).position<28) callback();
    put(CP_FILL,0,0);put(CP_CLAVE,1,0);callback();
    next_board_phrase();assert(board_clave(board_view(0))==0 && board_next_clave(board_view(0))==1);
    next_board_phrase();assert(board_clave(board_view(0))==1 && board_next_clave(board_view(0))==2);
    put(CP_STOP,0,0);callback();put(CP_CLAVE,2,0);callback();assert(board_view(0).result==CP_RANGE);
    put(BOARD_GROOVE_CUE,1,0);callback();put(CP_CLAVE,1,0);callback();assert(board_view(0).result==CP_RANGE);
    put(BOARD_CUE,9,0);callback();put(CP_CLAVE,1,0);callback();assert(board_view(0).result==CP_RANGE);
    /* An explicit direction change holds automatic set advance at that boundary. */
    board_init();put(BOARD_CUE,4,0);put(BOARD_REPEAT,1,0);put(CP_START,0,0);callback();
    put(CP_CLAVE,1,0);callback();next_board_phrase();
    assert(board_view(0).song==4 && board_clave(board_view(0))==1);
    next_board_phrase();assert(board_view(0).song==5 && board_next_clave(board_view(0))==2);
}

static void verify_live_key(void)
{
    const int32_t neutral[7]={0};Panel p={.page=PANEL_BASS};
    board_init();put(BOARD_GROOVE_CUE,1,0);callback();
    assert(board_root(board_view(0))==0 && board_next_root(board_view(0))==12);
    panel_input(&p,1u<<1,0,neutral);callback();
    assert(board_root(board_view(0))==1); /* Stopped change is immediate. */
    put(CP_BPM,180,0);put(CP_START,0,0);callback();
    panel_input(&p,1u<<1,0,neutral);callback();
    panel_input(&p,1u<<1,0,neutral);callback();
    assert(board_root(board_view(0))==1 && board_next_root(board_view(0))==3);
    next_board_phrase();assert(board_root(board_view(0))==3 && board_next_root(board_view(0))==12);
    put(CP_ROOT,7,0);callback();put(CP_ROOT,3,0);callback();
    assert(board_next_root(board_view(0))==12);
    put(CP_ROOT,5,0);put(BOARD_PART,PART_CHORUS,0);callback();next_board_phrase();
    assert(board_root(board_view(0))==5 && board_view(0).part==PART_CHORUS);
    put(CP_ROOT,7,0);callback();put(CP_STOP,0,0);callback();
    assert(board_root(board_view(0))==5 && board_next_root(board_view(0))==12);
    put(BOARD_SAVE,4,0);callback();put(BOARD_GROOVE_CUE,7,0);callback();
    assert(board_root(board_view(0))==0);
    put(BOARD_CUE,4,0);callback();assert(board_root(board_view(0))==5);
    put(CP_START,0,0);callback();put(CP_ROOT,7,0);put(BOARD_CUE,1,0);callback();next_board_phrase();
    assert(board_root(board_view(0))==0 && board_next_root(board_view(0))==12);
    put(CP_STOP,0,0);callback();put(CP_ROOT,11,0);callback();
    panel_input(&p,1u<<1,0,neutral);callback();assert(board_root(board_view(0))==11);
    put(CP_ROOT,0,0);callback();panel_input(&p,1u<<0,0,neutral);callback();
    assert(board_root(board_view(0))==0);
    put(CP_ROOT,12,0);callback();assert(board_view(0).result==CP_RANGE);
    put(BOARD_CUE,9,0);callback();panel_input(&p,1u<<1,0,neutral);callback();
    assert(board_root(board_view(0))==0);put(CP_ROOT,1,0);callback();assert(board_view(0).result==CP_OK);
    put(BOARD_GROOVE_CUE,0,0);callback();put(CP_ROOT,1,0);callback();assert(board_view(0).result==CP_RANGE);
    board_init();put(BOARD_GROOVE_CUE,1,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
    while(board_view(0).position<28) callback();
    put(CP_FILL,0,0);put(CP_ROOT,7,0);callback();
    next_board_phrase();assert(board_root(board_view(0))==0 && board_next_root(board_view(0))==7);
    next_board_phrase();assert(board_root(board_view(0))==7 && board_next_root(board_view(0))==12);
    /* The project-root change affects pitched output, not percussion PCM. */
    uint32_t checks[3][2];
    for(unsigned bass=0;bass<3;++bass) for(unsigned key=0;key<2;++key) {
        board_init();put(BOARD_GROOVE_CUE,bass==2?3:1,0);put(BOARD_MASTER,100,0);
        put(CP_ROOT,key?7:0,0);put(bass?CP_SOLO:CP_MUTE,bass==2?3:5,0);put(CP_START,0,0);callback();
        uint32_t hash=2166136261u;
        for(unsigned n=0;n<600;++n) {
            unsigned done=half;callback();
            for(unsigned j=0;j<BOARD_WORDS;++j) hash=(hash^(uint32_t)board_dma[done][j])*16777619u;
        }
        checks[bass][key]=hash;
    }
    assert(checks[0][0]==checks[0][1] && checks[1][0]!=checks[1][1] && checks[2][0]!=checks[2][1]);
}

static void verify_groove_bank(void)
{
    _Static_assert(sizeof(BoardView)==76,"Bank cue uses existing view padding");
    char titles[SET_SLOTS][19];unsigned scores[SET_SLOTS];
    board_init();put(BOARD_CUE,9,0);callback();
    for(unsigned i=0;i<SET_SLOTS;++i) {strcpy(titles[i],board_song_title(i));scores[i]=board_song_is_score(i);}
    for(unsigned groove=0;groove<BOARD_GROOVE_PRESETS;++groove) {
        put(BOARD_GROOVE_CUE,groove,0);callback();
        BoardView v=board_view(0);
        assert(v.study==groove && !v.piano && v.song==SET_SLOTS && !v.phrases);
        assert(v.next_groove==BOARD_GROOVE_PRESETS && !v.rack_sequence && !v.feel);
        for(unsigned i=0;i<SET_SLOTS;++i) {assert(!strcmp(titles[i],board_song_title(i)));assert(scores[i]==board_song_is_score(i));}
        assert(!strcmp(panel_title(v),board_groove_title(groove)));
        Panel panel={0};
        unsigned bass=(groove>0 && groove<23) || groove==26 || groove==27 || groove==28;
        if(groove==26) {
            const uint32_t guitar=(1u<<0)|(1u<<1)|(1u<<3)|(1u<<4)|(1u<<6)|
                (1u<<8)|(1u<<10)|(1u<<11)|(1u<<13)|(1u<<14);
            assert(!strcmp(board_groove_title(groove),"BOLERO TROVA"));
            assert(cp_status(CP_STEP_COUNT)==16);
            for(unsigned step=0;step<16;++step) {
                assert(!!cp_step(3,step)==((guitar>>step)&1u));
                assert(!cp_step(0,step) && !cp_step(1,step) &&
                       !cp_step(2,step) && !cp_step(4,step));
            }
        }
        if(groove==27) {
            assert(!strcmp(board_groove_title(groove),"MERENHOUSE"));
            assert(v.bpm==128 && cp_status(CP_STEP_COUNT)==32);
            for(unsigned step=0;step<32;++step)
                assert(!!cp_step(1,step)==(step%4==0));
        }
        if(groove==28) {
            static const uint8_t roots[]={16,21,23,23};
            assert(!strcmp(board_groove_title(groove),"BAMBA JAROCHA"));
            assert(v.bpm==136 && cp_status(CP_STEP_COUNT)==16);
            for(unsigned step=0;step<16;++step)
                assert(cp_step(5,step)==(step%4 ? 0u : roots[step/4]));
        }
        assert(panel_has_bass(v)==bass);
        assert(!!(panel_keys(&panel,v).upper&(1u<<panel_layer_keys[4]))==bass);
        put(BOARD_MASTER,100,0);put(CP_START,0,0);callback();
        unsigned peak=0;
        for(unsigned n=0;n<1800;++n) {
            unsigned done=half;callback();
            for(unsigned i=0;i<BOARD_WORDS;++i) peak|=board_dma[done][i]!=0;
        }
        assert(peak && !board_stats.rail_samples && board_view(0).song==SET_SLOTS);
        put(CP_STOP,0,0);callback();
    }
    /* Newest cue wins; returning to the active preset cancels. */
    put(BOARD_GROOVE_CUE,1,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
    put(BOARD_GROOVE_CUE,6,0);put(BOARD_GROOVE_CUE,7,0);callback();
    assert(board_view(0).next_groove==7);
    put(BOARD_GROOVE_CUE,1,0);callback();assert(board_view(0).next_groove==BOARD_GROOVE_PRESETS);
    put(BOARD_GROOVE_CUE,7,0);callback();next_board_phrase();
    assert(board_view(0).study==7 && board_view(0).playing && cp_status(CP_STEP_COUNT)==16);
    put(BOARD_GROOVE_CUE,5,0);callback();put(CP_STOP,0,0);callback();
    assert(board_view(0).next_groove==BOARD_GROOVE_PRESETS && board_view(0).study==7);
    /* Saving a preset is explicit and retains its title, meter and tempo. */
    put(BOARD_SAVE,2,0);callback();
    assert(!strcmp(board_song_title(2),"MERENGUE"));
    put(BOARD_CUE,1,0);callback();put(BOARD_CUE,2,0);callback();
    assert(board_view(0).study==7 && board_view(0).song==2 && !board_view(0).piano);
    /* A running written score yields at its next written bar. */
    put(BOARD_CUE,9,0);put(CP_START,0,0);callback();
    put(BOARD_GROOVE_CUE,10,0);callback();
    for(unsigned guard=0;guard<2500 && board_view(0).piano;++guard) callback();
    assert(!board_view(0).piano && board_view(0).study==10 && board_view(0).playing);
    put(BOARD_GROOVE_CUE,11,0);put(BOARD_CUE,1,0);callback();
    assert(board_view(0).next_groove==BOARD_GROOVE_PRESETS && board_view(0).next_song==1);
    put(CP_STOP,0,0);callback();
    put(BOARD_GROOVE_CUE,BOARD_GROOVE_PRESETS,0);callback();assert(board_view(0).result==CP_RANGE);
    /* PRESETS on pads is the complete bank; SEQ exposes percussion excerpts. */
    put(BOARD_GROOVE_CUE,0,0);callback();
    Panel p={.page=PANEL_RACK};const int32_t next[7]={[6]=1},energy[7]={[1]=1},neutral[7]={0};
    panel_input(&p,0,0,next);callback();assert(board_view(0).study==1 && !p.rack_edit);
    panel_input(&p,0,0,energy);callback();assert(board_view(0).section==1);
    panel_input(&p,1u<<11,0,neutral);callback();
    panel_input(&p,0,0,next);callback();assert(p.rack_edit && board_view(0).rhythm==0);
}

static int compare(const void *a,const void *b)
{
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;
    return x>y ? 1 : x<y ? -1 : 0;
}
static void fix_rhythm_crc(uint8_t *bytes)
{
    uint32_t crc=~0u;
    for(unsigned i=0;i<136;++i) {
        crc^=bytes[i];for(unsigned b=0;b<8;++b) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    crc=~crc;for(unsigned i=0;i<4;++i) bytes[136+i]=(uint8_t)(crc>>(8*i));
}
static void verify_portable_rhythm(void)
{
    uint8_t saved[BOARD_RHYTHM_BYTES],again[BOARD_RHYTHM_BYTES],bad[BOARD_RHYTHM_BYTES];
    static int32_t expected[1600][BOARD_WORDS];
    board_init();put(CP_STUDY,1,0);callback();
    for(unsigned s=0;s<RACK_SOUNDS;++s) {
        put(BOARD_ACCENT,0,0);put(BOARD_SEQ_STEP,s,s);callback();
        put(BOARD_ACCENT,1,0);put(BOARD_SEQ_STEP,s,31-s);callback();
    }
    put(CP_BPM,113,0);put(CP_FEEL,28,0);put(BOARD_SEQ_ENABLE,2,0);callback();
    put(BOARD_RACK_CONTROL,RACK_HIT_STRENGTH,83);put(BOARD_RACK_CONTROL,RACK_ACCENT_STRENGTH,119);
    put(BOARD_RACK_CONTROL,RACK_LEVEL,76);callback();
    assert(board_rhythm_export(saved,sizeof saved)==CP_OK);
    assert(!memcmp(saved,"C7R1",4) && saved[4]==32 && saved[5]==113 && saved[6]==28);
    put(BOARD_MASTER,100,0);callback();put(CP_START,0,0);
    for(unsigned b=0;b<1600;++b) {unsigned done=half;callback();memcpy(expected[b],board_dma[done],sizeof expected[b]);}
    assert(board_rhythm_export(again,sizeof again)==CP_OK && !memcmp(saved,again,sizeof saved));
    assert(board_rhythm_import(saved,sizeof saved)==CP_STOP_FIRST);
    put(CP_STOP,0,0);callback();
    /* A new owner contains none of the session state. File restores exact audio. */
    board_init();put(CP_STUDY,1,0);callback();
    assert(board_rhythm_import(saved,sizeof saved)==CP_OK);
    assert(board_rhythm_export(again,sizeof again)==CP_OK && !memcmp(saved,again,sizeof saved));
    put(BOARD_MASTER,100,0);callback();put(CP_START,0,0);
    for(unsigned b=0;b<1600;++b) {unsigned done=half;callback();assert(!memcmp(expected[b],board_dma[done],sizeof expected[b]));}
    put(CP_STOP,0,0);callback();
    /* Every single-bit corruption and every truncated length is rejected atomically. */
    for(unsigned i=0;i<sizeof saved;++i) for(unsigned bit=0;bit<8;++bit) {
        memcpy(bad,saved,sizeof bad);bad[i]^=1u<<bit;
        assert(board_rhythm_import(bad,sizeof bad)==CP_BAD_PROJECT);
    }
    for(unsigned n=0;n<sizeof saved;++n) assert(board_rhythm_import(saved,n)==CP_BAD_PROJECT);
    const unsigned positions[]={4,5,6,7,8,9,10,11,12};
    const uint8_t values[]={24,59,101,128,128,101,3,15,1};
    for(unsigned i=0;i<sizeof positions/sizeof *positions;++i) {
        memcpy(bad,saved,sizeof bad);bad[positions[i]]=values[i];fix_rhythm_crc(bad);
        assert(board_rhythm_import(bad,sizeof bad)==CP_BAD_PROJECT);
    }
    memcpy(bad,saved,sizeof bad);bad[20]|=2;fix_rhythm_crc(bad);
    assert(board_rhythm_import(bad,sizeof bad)==CP_BAD_PROJECT); /* orphan accent */
    assert(board_rhythm_export(again,sizeof again)==CP_OK && !memcmp(saved,again,sizeof saved));
    put(CP_BPM,120,0);assert(board_rhythm_import(saved,sizeof saved)==CP_BUSY);callback();
    put(CP_STUDY,7,0);callback(); /* 2/4 merengue */
    assert(cp_status(CP_STEP_COUNT)==16);
    assert(board_rhythm_import(saved,sizeof saved)==CP_RANGE);
    assert(board_rhythm_export(again,sizeof again)==CP_OK && again[4]==16);
    assert(board_rhythm_import(again,sizeof again)==CP_OK);
    memcpy(bad,again,sizeof bad);bad[19]|=0x80;fix_rhythm_crc(bad);
    assert(board_rhythm_import(bad,sizeof bad)==CP_BAD_PROJECT);
    put(BOARD_CUE,9,0);callback();assert(board_view(0).piano);
    assert(board_rhythm_import(saved,sizeof saved)==CP_NO_ROLE);
    assert(board_rhythm_export(again,sizeof again)==CP_NO_ROLE);
}

static void verify_rhythm_slots(void)
{
    static uint8_t bank[BOARD_RHYTHM_SLOTS_BYTES],again[BOARD_RHYTHM_SLOTS_BYTES],bad[BOARD_RHYTHM_SLOTS_BYTES];
    uint8_t first[140],second[140],check[140];
    static int32_t expected[1600][BOARD_WORDS];
    board_init();put(BOARD_CUE,1,0);put(BOARD_RHYTHM_CUE,3,0);callback();
    put(BOARD_SEQ_STEP,0,0);put(BOARD_SEQ_ENABLE,1,0);callback();
    put(BOARD_BASS_STYLE,BASS_ACID,0);put(BOARD_BASS_SHAPE,67,38);callback();
    put(BOARD_OVERLAY,0,0);put(BOARD_OVERLAY_LEVEL,0,73);put(BOARD_PART,PART_VERSE_B,0);callback();
    put(BOARD_SAVE,1,0);callback();assert(board_view(0).result==BOARD_SESSION);
    assert(board_rhythm_export(first,sizeof first)==CP_OK);
    put(BOARD_CUE,10,0);put(BOARD_RHYTHM_CUE,4,0);callback();
    put(BOARD_SEQ_ENABLE,1,0);put(BOARD_BASS_STYLE,BASS_ENSEMBLE,0);callback();
    put(BOARD_SAVE,10,0);callback();
    assert(board_rhythm_export(second,sizeof second)==CP_OK);
    assert(memcmp(first,second,140));
    assert(board_rhythm_slots_export(bank,sizeof bank)==CP_OK);
    assert(bank[BOARD_RHYTHM_SLOT_BYTES+140]==3); /* Edited source keeps its origin, not an exact-copy claim. */
    put(BOARD_CUE,1,0);callback();
    assert(board_rhythm_export(check,sizeof check)==CP_OK && !memcmp(first,check,140));
    BoardView v=board_view(0);
    assert(v.bass_style==BASS_ACID && v.bass_cut==67 && v.bass_bite==38 &&
           (v.overlays&1) && v.overlay_levels[0]==73 && v.part==PART_VERSE_B);
    put(BOARD_MASTER,100,0);put(CP_START,0,0);
    for(unsigned b=0;b<1600;++b) {unsigned done=half;callback();memcpy(expected[b],board_dma[done],sizeof expected[b]);}
    assert(board_rhythm_slots_export(again,sizeof again)==CP_STOP_FIRST);
    assert(board_rhythm_slots_load(bank,sizeof bank)==CP_STOP_FIRST);
    put(CP_STOP,0,0);callback();
    board_init();assert(board_rhythm_slots_load(bank,sizeof bank)==CP_OK);
    put(BOARD_CUE,1,0);put(BOARD_MASTER,100,0);put(CP_START,0,0);
    for(unsigned b=0;b<1600;++b) {unsigned done=half;callback();assert(!memcmp(expected[b],board_dma[done],sizeof expected[b]));}
    put(CP_STOP,0,0);callback();
    put(BOARD_CUE,10,0);callback();
    assert(board_rhythm_export(check,sizeof check)==CP_OK && !memcmp(second,check,140));
    assert(board_view(0).bass_style==BASS_ENSEMBLE);
    /* Invalid final slot must not partially replace an earlier valid record. */
    memcpy(bad,bank,sizeof bank);bad[BOARD_RHYTHM_SLOT_BYTES+141]=0;bad[sizeof bad-1]=1;
    assert(board_rhythm_slots_load(bad,sizeof bad)==CP_BAD_PROJECT);
    assert(board_rhythm_slots_export(again,sizeof again)==CP_OK && !memcmp(bank,again,sizeof bank));
    const unsigned fields[]={4,5,6,140,141,142,143,144,145,148,149,150};
    for(unsigned i=0;i<sizeof fields/sizeof *fields;++i) {
        memcpy(bad,bank,sizeof bank);uint8_t *r=bad+BOARD_RHYTHM_SLOT_BYTES;
        r[fields[i]]=254;fix_rhythm_crc(r);
        assert(board_rhythm_slots_load(bad,sizeof bad)==CP_BAD_PROJECT);
    }
    memcpy(bad,bank,sizeof bank);memcpy(bad+8*BOARD_RHYTHM_SLOT_BYTES,bank+BOARD_RHYTHM_SLOT_BYTES,BOARD_RHYTHM_SLOT_BYTES);
    assert(board_rhythm_slots_load(bad,sizeof bad)==CP_BAD_PROJECT); /* Score cannot acquire a groove rhythm. */
    for(unsigned n=0;n<sizeof bank;++n) assert(board_rhythm_slots_load(bank,n)==CP_BAD_PROJECT);
    assert(board_rhythm_slots_export(again,sizeof again)==CP_OK && !memcmp(bank,again,sizeof bank));
    put(BOARD_CUE,1,0);callback();
    /* A plain slot has its own sound even when it follows a saved 727
     * arrangement. Switching between parts or stopping keeps local edits. */
    put(BOARD_MASTER,75,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
    put(BOARD_CUE,4,0);callback();
    v=board_view(0);
    assert(v.song==1 && v.next_song==4 && v.bass_style==BASS_ACID && v.overlays==1);
    next_board_phrase();
    v=board_view(0);
    assert(v.playing && v.song==4 && v.master==75 &&
           v.bass_style==BASS_ENSEMBLE && v.bass_cut==44 && v.bass_bite==45 &&
           !v.overlays && !v.rack_sequence);
    for(unsigned lane=0;lane<4;++lane) assert(v.overlay_levels[lane]==100);
    assert(board_rhythm_export(check,sizeof check)==CP_OK);
    assert(!memcmp(check+16,first+16,120)); /* Disabled grid can still be reused. */

    put(BOARD_BASS_STYLE,BASS_ACID,0);put(BOARD_OVERLAY,2,0);callback();
    put(BOARD_PART,PART_CHORUS,0);callback();next_board_phrase();
    v=board_view(0);
    assert(v.part==PART_CHORUS && v.bass_style==BASS_ACID && v.overlays==4);
    put(CP_STOP,0,0);callback();
    assert(board_view(0).bass_style==BASS_ACID && board_view(0).overlays==4);

    /* The concert can pass through a written score without carrying
     * over the prior song's bass or optional percussion. */
    put(BOARD_CUE,8,0);callback();
    v=board_view(0);
    assert(v.piano && v.bass_style==BASS_ENSEMBLE && !v.overlays);
    put(BOARD_CUE,1,0);callback();
    assert(board_rhythm_export(check,sizeof check)==CP_OK && !memcmp(first,check,140));
    v=board_view(0);
    assert(v.bass_style==BASS_ACID && v.bass_cut==67 && v.bass_bite==38 &&
           v.overlays==1 && v.overlay_levels[0]==73 && v.part==PART_VERSE_B);
}

static void verify_afro_cuban_bank(void)
{
    /* Independent masks read from Rendon pp.16-17: 16 eighth-note strokes,
     * preserving the low-drum answers and rotating whole bars for 3-2. */
    static const uint32_t expected[6][5]={
        {0,0,0x05550555u,0x50005000u,0},
        {0,0,0x04150555u,0x50005000u,0x01400000u},
        {0,0,0x05550415u,0x50005000u,0x00000140u},
        {0x45554555u,0x10001000u,0,0,0},
        {0x45554555u,0x10001000u,0x04150555u,0x50005000u,0x01400000u},
        {0x45554555u,0x10001000u,0x05550415u,0x50005000u,0x00000140u}
    };
    board_init();put(BOARD_CUE,1,0);put(BOARD_MASTER,100,0);callback();
    uint8_t file[BOARD_RHYTHM_BYTES],bank[BOARD_RHYTHM_SLOTS_BYTES],restored[BOARD_RHYTHM_BYTES];
    uint8_t project[CP_PROJECT_BYTES];cp_snapshot();memcpy(project,cp_blob(),sizeof project);
    for(unsigned index=8;index<14;++index) {
        put(BOARD_RHYTHM_CUE,index,0);callback();
        assert(board_view(0).rhythm==index && board_view(0).song==1);
        assert(board_rhythm_export(file,sizeof file)==CP_OK);
        for(unsigned voice=0;voice<15;++voice) {
            uint32_t hit=0,accent=0;
            for(unsigned b=0;b<4;++b) {
                hit|=(uint32_t)file[16+voice*8+b]<<(8*b);
                accent|=(uint32_t)file[20+voice*8+b]<<(8*b);
            }
            assert(hit==(voice<5?expected[index-8][voice]:0));
            uint32_t accents=voice==2 && index!=11?0x00100010u:
                voice==1 || voice==3 || voice==4 ? hit : 0;
            assert(accent==accents);
        }
        cp_snapshot();assert(!memcmp(project,cp_blob(),sizeof project));
        put(BOARD_SEQ_ENABLE,2,0);put(CP_START,0,0);callback();
        unsigned peak=0;
        for(unsigned b=0;b<1800;++b) {
            unsigned done=half;callback();
            for(unsigned f=0;f<BOARD_WORDS;++f) if(board_dma[done][f]) peak=1;
        }
        assert(peak && !board_stats.rail_samples);
        put(CP_STOP,0,0);callback();
    }
    put(BOARD_SAVE,1,0);callback();
    assert(board_rhythm_export(file,sizeof file)==CP_OK);
    assert(board_rhythm_slots_export(bank,sizeof bank)==CP_OK && bank[BOARD_RHYTHM_SLOT_BYTES+140]==13);
    board_init();assert(board_rhythm_slots_load(bank,sizeof bank)==CP_OK);
    put(BOARD_CUE,1,0);callback();
    assert(board_rhythm_export(restored,sizeof restored)==CP_OK && !memcmp(file,restored,sizeof file));
    assert(board_view(0).rhythm==13);
    /* The 2-3 cell stays anchored to the song's two-bar grid at a bar cue. */
    put(BOARD_RHYTHM_CUE,11,0);put(CP_START,0,0);callback();
    while(board_view(0).position<1) callback();
    put(BOARD_RHYTHM_CUE,9,0);callback();
    assert(board_view(0).next_rhythm==9);
    unsigned guard=2000;
    while(board_view(0).rhythm!=9 && guard--) callback();
    assert(guard && board_view(0).position==16);
    assert(board_rhythm_export(file,sizeof file)==CP_OK);
    uint32_t low=0;for(unsigned b=0;b<4;++b) low|=(uint32_t)file[16+4*8+b]<<(8*b);
    assert(low==0x01400000u);
    put(CP_STOP,0,0);callback();
    put(BOARD_RHYTHM_CUE,BOARD_RHYTHM_PATTERNS,0);callback();
    assert(board_view(0).result==CP_RANGE);
}

static void verify_linked_clave(void)
{
    uint8_t original[BOARD_RHYTHM_BYTES], rotated[BOARD_RHYTHM_BYTES], after[BOARD_RHYTHM_BYTES];
    const int32_t neutral[7]={0};Panel panel={.page=PANEL_PERC};char face[8][21];
    for(unsigned study=2;study<=6;study+=4) for(unsigned family=9;family<=12;family+=3) {
        board_init();put(BOARD_GROOVE_CUE,study,0);put(BOARD_RHYTHM_CUE,family,0);callback();
        assert(board_clave(board_view(0))==1 && board_clave_linked(board_view(0)));
        panel_face(&panel,board_view(0),face);assert(strstr(face[6],"BAND+727"));
        assert(board_rhythm_export(original,sizeof original)==CP_OK);
        put(BOARD_SEQ_SELECT,4,0);put(BOARD_RACK_CONTROL,RACK_LEVEL,64);put(BOARD_SEQ_ENABLE,0,0);callback();
        panel_input(&panel,1u<<0,0,neutral);callback();
        assert(board_view(0).rhythm==family+1 && board_clave(board_view(0))==0);
        assert(!board_view(0).rack_sequence && board_view(0).rack_sequence_sound==4 && board_view(0).rack_level==64);
        assert(board_rhythm_export(rotated,sizeof rotated)==CP_OK);
        for(unsigned i=16;i<136;i+=4) {
            assert(rotated[i]==original[i+2] && rotated[i+1]==original[i+3]);
            assert(rotated[i+2]==original[i] && rotated[i+3]==original[i+1]);
        }
        put(BOARD_SEQ_ENABLE,1,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
        while(board_view(0).position<2) callback();
        put(CP_CLAVE,1,0);callback();assert(board_view(0).rhythm==family+1);
        put(CP_CLAVE,0,0);callback();assert(board_next_clave(board_view(0))==2);
        put(CP_CLAVE,1,0);callback();
        while(board_view(0).position<16) callback();
        assert(board_view(0).rhythm==family+1 && board_clave(board_view(0))==0);
        next_board_phrase();assert(board_view(0).rhythm==family && board_clave(board_view(0))==1);
        /* A paired preset cues the backing and waits for the complete phrase. */
        put(BOARD_RHYTHM_CUE,family+1,0);callback();
        assert(board_next_clave(board_view(0))==0);
        put(CP_CLAVE,1,0);callback(); // Cancel back to the current pair member.
        assert(board_view(0).next_rhythm==255 && board_next_clave(board_view(0))==2);
        put(CP_CLAVE,0,0);put(BOARD_RHYTHM_CUE,family,0);callback();
        assert(board_next_clave(board_view(0))==2); // Later preset selection wins.
        put(BOARD_RHYTHM_CUE,family+1,0);callback();
        while(board_view(0).position<16) callback();assert(board_view(0).rhythm==family);
        next_board_phrase();assert(board_view(0).rhythm==family+1 && board_clave(board_view(0))==0);
        put(CP_CLAVE,1,0);put(CP_STOP,0,0);callback();
        assert(board_view(0).rhythm==family+1 && board_next_clave(board_view(0))==2);
        put(BOARD_SAVE,4,0);callback();put(BOARD_GROOVE_CUE,1,0);callback();put(BOARD_CUE,4,0);callback();
        assert(board_view(0).rhythm==family+1 && board_clave(board_view(0))==0);
        put(CP_START,0,0);put(BOARD_RHYTHM_CUE,family,0);callback();
        put(BOARD_SEQ_ENABLE,0,0);callback();
        assert(board_next_clave(board_view(0))==2 && board_view(0).next_rhythm==255);
        put(BOARD_RHYTHM_CUE,family,0);put(BOARD_SEQ_STEP,3,1);callback();
        assert(board_next_clave(board_view(0))==2 && board_view(0).next_rhythm==255);
        put(CP_STOP,0,0);callback();put(BOARD_RHYTHM_CUE,family+1,0);callback();
        /* Once edited, a grid retains the musician's exact transport positions. */
        put(BOARD_SEQ_STEP,3,1);callback();assert(board_rhythm_export(original,sizeof original)==CP_OK);
        assert(!board_clave_linked(board_view(0)));
        panel_face(&panel,board_view(0),face);assert(strstr(face[6],"BAND ONLY"));
        put(CP_CLAVE,1,0);callback();assert(board_rhythm_export(after,sizeof after)==CP_OK);
        assert(!memcmp(original,after,sizeof original));
    }
    /* Late fill, explicit song ownership, and automatic advance exclusion. */
    board_init();put(BOARD_GROOVE_CUE,6,0);put(BOARD_RHYTHM_CUE,12,0);put(CP_BPM,180,0);put(CP_START,0,0);callback();
    while(board_view(0).position<30) callback();
    put(CP_FILL,0,0);put(BOARD_RHYTHM_CUE,13,0);callback();
    next_board_phrase();assert(board_view(0).rhythm==12);
    next_board_phrase();assert(board_view(0).rhythm==13 && board_clave(board_view(0))==0);
    put(BOARD_RHYTHM_CUE,12,0);put(BOARD_CUE,1,0);callback();next_board_phrase();
    assert(board_view(0).study==1 && board_view(0).rhythm==255 && board_next_clave(board_view(0))==2);
    board_init();put(BOARD_CUE,4,0);put(BOARD_REPEAT,1,0);put(BOARD_RHYTHM_CUE,9,0);put(CP_START,0,0);callback();
    put(BOARD_RHYTHM_CUE,12,0);callback();assert(board_next_clave(board_view(0))==2);
    next_board_phrase();assert(board_view(0).song==4 && board_view(0).rhythm==12);
    next_board_phrase();assert(board_view(0).song==5);
    board_init();put(BOARD_GROOVE_CUE,6,0);put(BOARD_RHYTHM_CUE,9,0);put(CP_START,0,0);callback();
    put(BOARD_RHYTHM_CUE,13,0);put(CP_CLAVE,1,0);callback();
    assert(board_view(0).next_rhythm==12);next_board_phrase();
    assert(board_view(0).rhythm==12 && board_clave(board_view(0))==1);
}

static void verify_saved_rhythm_identity(void)
{
    uint8_t original[BOARD_RHYTHM_BYTES];
    for(unsigned preset=0;preset<BOARD_RHYTHM_PATTERNS;++preset) {
        board_init();put(BOARD_CUE,1,0);put(BOARD_RHYTHM_CUE,preset,0);callback();
        assert(board_rhythm_export(original,sizeof original)==CP_OK);
        put(BOARD_SAVE,1,0);callback();
        put(BOARD_CUE,10,0);callback();put(BOARD_CUE,1,0);callback();
        assert(board_view(0).rhythm==preset);
        /* A changed hit cannot use its parent's exact-preset label. */
        put(BOARD_SEQ_STEP,0,31);put(BOARD_SAVE,1,0);callback();
        put(BOARD_CUE,10,0);callback();put(BOARD_CUE,1,0);callback();
        assert(board_view(0).rhythm==BOARD_CUSTOM_RHYTHM);
        /* Accent-only edits also invalidate the exact-preset name. */
        put(BOARD_RHYTHM_CUE,preset,0);callback();
        unsigned voice=0,step=0;
        while(voice<15) {
            uint32_t hits=0;for(unsigned b=0;b<4;++b) hits|=(uint32_t)original[16+voice*8+b]<<(8*b);
            if(hits) {while(!(hits&(1u<<step))) ++step;break;} ++voice;
        }
        assert(voice<15);
        put(BOARD_ACCENT,1,0);put(BOARD_SEQ_STEP,voice,step);put(BOARD_SAVE,1,0);callback();
        put(BOARD_CUE,10,0);callback();put(BOARD_CUE,1,0);callback();
        assert(board_view(0).rhythm==BOARD_CUSTOM_RHYTHM);
        /* Matching bytes without source metadata remain a user rhythm. */
        assert(board_rhythm_import(original,sizeof original)==CP_OK);
        put(BOARD_SAVE,1,0);callback();
        put(BOARD_CUE,10,0);callback();put(BOARD_CUE,1,0);callback();
        assert(board_view(0).rhythm==BOARD_CUSTOM_RHYTHM);
    }
}

static void verify_source_conga_bank(void)
{
    /* CongaPlace Merengue 2, read independently from both printed bars:
     * bar 1 S . . B S . O(low) .; bar 2 O O . B S . O .
     * The variation fills only the third eighth of bar 2 with an open tone. */
    const uint32_t hits[4][3]={{0x01400141u,0x10050000u,0x00001000u},
                              {0x01400141u,0x10150000u,0x00001000u},
                              {0x01550155u,0x10001000u,0x44004400u},
                              {0x01550555u,0x10005000u,0x44000000u}};
    /* Rhythm Notes bolero: every eighth; low-high-low at
     * printed eighths 6/7/8 = beat 3&, beat 4, beat 4&. The spacious
     * form replaces bar one's low-high-low with touch/high/high. */
    uint8_t file[BOARD_RHYTHM_BYTES],again[BOARD_RHYTHM_BYTES],slots[BOARD_RHYTHM_SLOTS_BYTES];
    for(unsigned n=0;n<4;++n) {
        board_init();put(BOARD_CUE,1,0);put(BOARD_MASTER,100,0);callback();
        put(BOARD_RHYTHM_CUE,14+n,0);callback();
        assert(board_view(0).rhythm==14+n && board_view(0).rack_sequence==1);
        assert(board_rhythm_export(file,sizeof file)==CP_OK);
        for(unsigned voice=0;voice<15;++voice) {
            uint32_t hit=0,accent=0;
            for(unsigned b=0;b<4;++b) {
                hit|=(uint32_t)file[16+voice*8+b]<<(8*b);
                accent|=(uint32_t)file[20+voice*8+b]<<(8*b);
            }
            assert(hit==(voice>=2 && voice<=4?hits[n][voice-2]:0));
            assert(accent==(voice==2?(n<2?0x01000101u:0x00100010u):hit));
        }
        put(BOARD_SEQ_ENABLE,2,0);put(CP_START,0,0);callback();
        unsigned audible=0;
        for(unsigned block=0;block<1800;++block) {
            unsigned done=half;callback();
            for(unsigned f=0;f<BOARD_WORDS;++f) audible|=board_dma[done][f]!=0;
        }
        assert(audible && !board_stats.rail_samples);
        put(CP_STOP,0,0);callback();put(BOARD_SAVE,1,0);callback();
        assert(board_rhythm_export(file,sizeof file)==CP_OK);
        assert(board_rhythm_slots_export(slots,sizeof slots)==CP_OK);
        assert(slots[BOARD_RHYTHM_SLOT_BYTES+140]==14+n);
        board_init();assert(board_rhythm_slots_load(slots,sizeof slots)==CP_OK);
        put(BOARD_CUE,1,0);callback();
        assert(board_rhythm_export(again,sizeof again)==CP_OK && !memcmp(file,again,sizeof file));
        /* A recalled conga part must open on its saved populated lane,
         * whether entered through REC or SELECT, not an empty bongo lane. */
        Panel p={.page=PANEL_HOME};int32_t enc[7]={0};
        panel_scan(&p,1u<<13,0,enc,100);callback();
        assert(p.rack_sound==2);
        panel_scan(&p,1u<<11,0,enc,200);callback();
        assert(p.rack_edit && board_view(0).rack_sequence_sound==2);
        assert(board_view(0).rack_steps==hits[n][0]);
        panel_scan(&p,1u<<8,0,enc,300);callback();
        p.rack_sound=0;enc[0]=3;
        panel_scan(&p,0,0,enc,400);callback();
        assert(p.page==PANEL_RACK && p.rack_sound==2);

    }
}

static void verify_roland_step_write(void)
{
    /* Roland TR-727 Owner's Manual, printed pp. 12–13, Fig. 2. */
    const uint32_t expected[RACK_SOUNDS]={
        [0]=0x14241424u, [3]=0x0a190a19u, [4]=0xc1c2c1c2u, [7]=0x11111111u
    };
    uint8_t file[BOARD_RHYTHM_BYTES];
    board_init();put(BOARD_CUE,1,0);callback();
    put(BOARD_RHYTHM_CUE,BOARD_ROLAND_LESSON,0);callback();
    assert(board_view(0).rhythm==BOARD_ROLAND_LESSON);
    assert(!strcmp(board_rhythm_title(BOARD_ROLAND_LESSON),"ROLAND STEP WRITE"));
    assert(board_rhythm_export(file,sizeof file)==CP_OK);
    for(unsigned voice=0;voice<RACK_SOUNDS;++voice) {
        for(unsigned byte=0;byte<4;++byte) {
            assert(file[16+voice*8+byte]==(uint8_t)(expected[voice]>>(8*byte)));
            assert(file[20+voice*8+byte]==0);
        }
    }
}

static void verify_colombian_727(void)
{
    /* Four written cut-time bars at eight steps per bar.
     * Check both tambora bars and the guache attack across their ties. */
    const uint32_t patterns[3][RACK_SOUNDS]={
        {[1]=0x44444444u,[2]=0x19191919u,[3]=0x42424242u,[4]=0x40004000u,
         [5]=0x1ddd1dddu,[10]=0xddddddddu,[13]=0x44444445u},
        {[1]=0x44444444u,[2]=0x19191919u,[3]=0x66666666u,[4]=0x40004000u,
         [5]=0x1ddd1dddu,[10]=0xddddddddu,[13]=0x44444445u},
        {[1]=0x66666666u,[2]=0x11111111u,[3]=0x66666666u,[4]=0x40004000u,
         [5]=0x2d562d56u,[10]=0xddddddddu,[13]=0x44444445u}
    };
    uint8_t file[BOARD_RHYTHM_BYTES];
    for(unsigned n=0;n<3;++n) {
        board_init();put(BOARD_GROOVE_CUE,23+n,0);callback();
        put(BOARD_RHYTHM_CUE,BOARD_COLOMBIAN_RHYTHMS+n,0);callback();
        assert(board_view(0).rhythm==BOARD_COLOMBIAN_RHYTHMS+n);
        assert(board_rhythm_export(file,sizeof file)==CP_OK);
        for(unsigned voice=0;voice<RACK_SOUNDS;++voice) {
            uint32_t accents=voice==1?0x44444444u:voice==3?patterns[n][voice]:0;
            for(unsigned byte=0;byte<4;++byte) {
                assert(file[16+voice*8+byte]==(uint8_t)(patterns[n][voice]>>(8*byte)));
                assert(file[20+voice*8+byte]==(uint8_t)(accents>>(8*byte)));
            }
        }
    }
}

static void verify_conga_bank(void)
{
    board_init();put(BOARD_CUE,1,0);callback();
    uint8_t project[CP_PROJECT_BYTES],after[CP_PROJECT_BYTES],file[BOARD_RHYTHM_BYTES];
    cp_snapshot();memcpy(project,cp_blob(),sizeof project);
    Panel p={.page=PANEL_RACK,.rack_edit=1};int32_t enc[7]={[6]=1};
    panel_scan(&p,0,0,enc,100);callback();
    assert(p.rack_edit && p.rack_sound==2 && board_view(0).rhythm==0);
    assert(board_rhythm_export(file,sizeof file)==CP_OK);
    /* Manual p.16 literal grid, one-based source steps repeated in bar two. */
    const uint32_t expected[]={0x04100410u,0x00010001u,0x50005000u};
    for(unsigned voice=0;voice<15;++voice) {
        uint32_t bits=0;for(unsigned b=0;b<4;++b) bits|=(uint32_t)file[16+voice*8+b]<<(8*b);
        assert(bits==(voice>=2 && voice<=4?expected[voice-2]:0));
    }
    for(unsigned index=0;index<BOARD_CONGA_PATTERNS;++index) {
        put(BOARD_RHYTHM_CUE,index,0);callback();
        assert(board_view(0).song==1 && board_view(0).rhythm==index && board_view(0).rack_sequence==1);
        assert(board_rhythm_export(file,sizeof file)==CP_OK);
        for(unsigned voice=0;voice<15;++voice) {
            assert(file[16+voice*8]==file[18+voice*8] && file[17+voice*8]==file[19+voice*8]);
            if(voice<2 || voice>4) for(unsigned b=0;b<8;++b) assert(!file[16+voice*8+b]);
        }
    }
    cp_snapshot();memcpy(after,cp_blob(),sizeof after);assert(!memcmp(project,after,sizeof project));
    put(BOARD_RHYTHM_CUE,0,0);put(CP_START,0,0);callback();
    while(board_view(0).position<1) callback();
    put(BOARD_RHYTHM_CUE,1,0);callback();assert(board_view(0).next_rhythm==1);
    put(BOARD_RHYTHM_CUE,0,0);callback();assert(board_view(0).next_rhythm==255);
    put(BOARD_RHYTHM_CUE,1,0);put(BOARD_RHYTHM_CUE,2,0);callback();
    assert(board_view(0).next_rhythm==2 && board_view(0).rhythm==0);
    unsigned guard=2000;
    while(board_view(0).rhythm==0 && guard--) { assert(board_view(0).position<16);callback(); }
    assert(guard && board_view(0).rhythm==2 && board_view(0).position==16);
    put(BOARD_RHYTHM_CUE,3,0);put(CP_STOP,0,0);callback();
    assert(board_view(0).rhythm==2 && board_view(0).next_rhythm==255);
    put(BOARD_SEQ_STEP,3,0);callback();assert(board_view(0).rhythm==255);
    put(BOARD_RHYTHM_CUE,0,0);callback();put(BOARD_CUE,0,0);callback();
    assert(board_view(0).rhythm==255 && !board_view(0).rack_sequence);
    put(CP_STUDY,7,0);callback();put(BOARD_RHYTHM_CUE,0,0);callback();
    assert(board_view(0).result==CP_NO_ROLE && board_view(0).rhythm==255);
}

int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--audition")) {
        audition(argv[2]);
        return 0;
    }
    if(argc==3 && (!strcmp(argv[1],"--piano") || !strcmp(argv[1],"--guide") ||
                   !strcmp(argv[1],"--golondrina") || !strcmp(argv[1],"--golondrina-guide"))) {
        unsigned golondrina=!strncmp(argv[1],"--golondrina",12);
        piano_audition(argv[2],golondrina?9:8,!strcmp(argv[1],"--guide") ||
                       !strcmp(argv[1],"--golondrina-guide"));
        return 0;
    }
    const unsigned blocks=sizeof elapsed/sizeof *elapsed;
    FILE *output=argc>1?fopen(argv[1],"wb"):0;
    if(argc>1) assert(output);
    verify_panel();
    verify_master();
    verify_bank();
    verify_choke();
    verify_rack_accents();
    verify_speed();
    verify_show();
    verify_groove_count_in();
    verify_rack_roll();
    verify_rack_record();
    verify_conductor();
    verify_live_cue_edges();
    verify_next_bar();
    verify_band_pads();
    verify_bass_and_overlays();
    verify_instrument_knobs();
    verify_score_controls();
    verify_score_transpose();
    verify_score_cue_replacement();
    verify_piano();
    verify_golondrina();
    verify_regional();
    verify_rack_editor();
    verify_drum_loop_transport();
    verify_groove_bank();
    verify_live_clave();
    verify_live_key();
    verify_portable_rhythm();
    verify_conga_bank();
    verify_afro_cuban_bank();
    verify_saved_rhythm_identity();
    verify_linked_clave();
    verify_source_conga_bank();
    verify_roland_step_write();
    verify_colombian_727();
    verify_rhythm_slots();
    board_init();
    unsigned peak=0, nonzero=0;
    uint32_t random=77;
    const unsigned rack_order[]={14,13,6,5,12,4,1,0,2,3,7,8,9,10,11};
    for(unsigned i=0;i<blocks;++i) {
        unsigned phase=i%7000u;
        if(!phase) put(CP_STOP,0,0);
        else if(phase==1) {
            put(CP_STUDY,i/7000u,0); put(BOARD_KIT,1,0); put(BOARD_MASTER,100,0);
            if(i>=7000u) {
                put(BOARD_ACCENT,1,0);
                put(BOARD_RACK_CONTROL,RACK_ACCENT_STRENGTH,127);
            }
        }
        else if(phase==2) {
            if(i==2) {put(BOARD_ASSIGN,0,15); put(BOARD_ASSIGN,1,14);}
            put(CP_START,0,0); put(CP_BPM,180,0); put(CP_FEEL,100,0);
        }
        else if(phase==3) {
            if(i==3) {
                for(unsigned r=0;r<8;++r) put(BOARD_RACK,rack_order[r],127);
            } else {
                const unsigned groups[]={0,2,4,5,6,7,9,11};
                for(unsigned r=0;r<8;++r) put(BOARD_RACK,groups[r],127);
            }
        } else if (phase==2700) put(CP_SECTION,1,0);
        else if (phase==4050) put(CP_FILL,0,0);
        else if (phase==5200) put(CP_SECTION,2,0);
        else if (phase>5 && phase<6000 && phase%11u==0) {
            for(unsigned hit=0;hit<8;++hit) {
                random=random*1664525u+1013904223u;
                put(BOARD_RACK,rack_order[(random>>16)%15],i>=7000u ? 0 : 127);
            }
        } else if (phase>5 && phase%251u==0) {
            put(CP_HIT,(phase/251u)%6u,0);
        }
        unsigned completed=half;
        elapsed[i]=callback();
        for(unsigned j=0;j<BOARD_FRAMES;++j) {
            int32_t left=board_dma[completed][2*j],right=board_dma[completed][2*j+1];
            assert(left==right && left%128==0);
            unsigned magnitude=(unsigned)(left<0 ? -left : left)/128u;
            if(magnitude) ++nonzero;
            if(magnitude>peak) peak=magnitude;
            if(output) {
                uint16_t v=(uint16_t)(left/128);
                fputc(v&255u,output);
                fputc(v>>8,output);
            }
        }
    }
    if(output) assert(!fclose(output));
    assert(board_stats.callbacks==blocks && board_stats.rack_peak==10 && board_stats.sequence_peak>=2);
    assert(board_stats.total_peak==10 && !board_stats.rail_samples && peak>2000 && nonzero>100000);
    assert(!board_stats.deadline_misses && !board_stats.late_halves && !disabled);
    qsort(elapsed,blocks,sizeof *elapsed,compare);
    printf("{\"blocks\":%u,\"frames\":%u,\"p50_ns\":%" PRIu64 ",\"p99_ns\":%" PRIu64
           ",\"p999_ns\":%" PRIu64 ",\"max_ns\":%" PRIu64 ",\"rack_peak\":%u,\"sequence_pcm_peak\":%u"
           ",\"total_pcm_voices_peak\":%u,\"choke_cuts\":%u,\"peak_pcm\":%u,\"rails\":%u"
           ",\"host_stack_probe_bytes\":%" PRIuPTR,
           blocks,blocks*BOARD_FRAMES,elapsed[blocks/2],elapsed[blocks*99/100],
           elapsed[blocks*999/1000],elapsed[blocks-1],board_stats.rack_peak,board_stats.sequence_peak,
           board_stats.total_peak,board_stats.choke_cuts,peak,board_stats.rail_samples,
           stack_floor!=UINTPTR_MAX ? stack_ceiling-stack_floor : 0);
    inject_late=1; callback();
    assert(disabled==1 && board_stats.late_halves==1 && board_view(0).fault);
    assert(board_submit((BoardCommand){CP_START,0,0})==BOARD_FAULT);
    for(unsigned i=0;i<BOARD_WORDS;++i) assert(!board_dma[0][i] && !board_dma[1][i]);
    board_init(); inject_late=0; inject_deadline=1; callback();
    assert(disabled==2 && board_stats.deadline_misses==1 && board_view(0).fault);
    puts(",\"late_fault_closed\":true,\"deadline_fault_closed\":true}");
    return 0;
}
