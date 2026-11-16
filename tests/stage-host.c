/* Compile with -Iboard -Isrc -Ibuild and link board/panel.c. */
#include "panel.h"
#include <assert.h>
#include <stdio.h>

static uint16_t screen_pixels[240][240];
static unsigned win_x, win_y, win_w, win_h, cursor;
static void lcd_sync(void) {}
static void lcd_window(unsigned x,unsigned y,unsigned right,unsigned bottom)
{
    assert(x<=right && right<240 && y<=bottom && bottom<240);
    win_x=x; win_y=y; win_w=right-x+1; win_h=bottom-y+1; cursor=0;
}
static void lcd_data(const uint16_t *pixels,unsigned bytes)
{
    assert(!(bytes&1u) && cursor+bytes/2<=win_w*win_h);
    for(unsigned i=0;i<bytes/2;++i,++cursor) {
        uint16_t pixel=pixels[i];
        screen_pixels[win_y+cursor/win_w][win_x+cursor%win_w]=(uint16_t)(pixel>>8|pixel<<8);
    }
}
static void lcd_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *pixels)
{
    if(!w || !h) return;
    lcd_window(x,y,x+w-1,y+h-1);
    lcd_data(pixels,w*h*2);
}
static void lcd_fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t pixel)
{
    assert(x+w<=240 && y+h<=240);
    for(unsigned row=y;row<y+h;++row)
        for(unsigned col=x;col<x+w;++col) screen_pixels[row][col]=pixel;
}
unsigned board_rhythm_sound(unsigned i) { (void)i;return 2; }
const char *board_groove_title(unsigned i) { (void)i;return "CUMBIA COSTA"; }
const char *board_rhythm_title(unsigned i) { (void)i;return "CR CONGA"; }
/* GLO rows and the Bass preview read these; this harness has no board owner. */
unsigned board_song_meta(unsigned slot,unsigned *study,unsigned *bpm,unsigned *reverse)
{ (void)slot;*study=0;*bpm=100;*reverse=0;return 0; }
void cp_bass_preview(int8_t *out,unsigned acid,unsigned cut,unsigned bite)
{ (void)acid;(void)cut;(void)bite;for(unsigned i=0;i<64;++i) out[i]=0; }
#include "../board/stage.c"

static const char *const titles[]={
    "CUMBIA COAST","CUMBIA ELeCTRICA","BOLERO CANCIoN","SON MONTUNO","SALSA SON CLAVE",
    "MERENGUE","TEJANO CUMBIA","CONJUNTO POLKA","LA PALOMA","LA GOLONDRINA","CIELITO LINDO",
    "CDMX SONIDERA","SALSA 2-3","BOLERO TROVA","MERENHOUSE","LA BAMBA"
};
int board_song_exists(unsigned slot) { return slot<sizeof titles/sizeof *titles; }
unsigned board_song_is_score(unsigned slot) { return slot==8 || slot==9 || slot==10; }
const char *board_song_title(unsigned slot) { return board_song_exists(slot)?titles[slot]:"EMPTY"; }
unsigned board_submit(BoardCommand cmd) { (void)cmd; assert(0); return CP_OK; }
uint32_t board_ticks(void) { return 0; }
BoardView board_view(unsigned role) { (void)role; assert(0); return (BoardView){0}; }
void board_fx_view(uint8_t fx[FX_PARAMS]) { for(unsigned i=0;i<FX_PARAMS;++i) fx[i]=0; }

static void screen(const Panel *p,BoardView v,PanelKeys keys,unsigned opening,unsigned cameo,unsigned now)
{
    stage_touch(now);
    stage_offer(p,v,keys,(RackFeedback){.hits=0},opening,cameo,now);
    while(stage_work()) {}
}
static void save(const char *dir,const char *name)
{
    char filename[1024];
    int n=snprintf(filename,sizeof filename,"%s/%s.ppm",dir,name);
    assert(n>0 && (unsigned)n<sizeof filename);
    FILE *out=fopen(filename,"wb");
    assert(out);
    fputs("P6\n240 240\n255\n",out);
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<240;++x) {
        unsigned c=screen_pixels[y][x];
        unsigned char pixel[]={ (unsigned char)((c>>11)*255/31),
            (unsigned char)(((c>>5)&63u)*255/63), (unsigned char)((c&31u)*255/31) };
        assert(fwrite(pixel,1,sizeof pixel,out)==sizeof pixel);
    }
    assert(!fclose(out));
}
int main(int argc,char **argv)
{
    assert(argc==2);
    Panel p={0};
    BoardView v={.clave=4,.key=12u<<4,.next_groove=BOARD_GROOVE_PRESETS,.speed=100,.rhythm=255,.next_rhythm=255,.playing=1,.study=1,.pending=3,.position=20,.bpm=101,.feel=33,
        .master=74,.song=1,.next_song=SET_SLOTS,.part=PART_VERSE_A,.next_part=PART_CHORUS,
        .overlays=3,.bass_cut=62,.bass_bite=40,.levels={[5]=78},
        .overlay_levels={90,85,80,75},.rack_strength=92,.rack_accent=124,.rack_level=90};
    splash();
    save(argv[1],"Splash");
    screen(&p,v,panel_keys(&p,v),1,0,1);
    save(argv[1],"Band-Live");
    v.speed=75;p.page=PANEL_MIX;
    screen(&p,v,panel_keys(&p,v),1,0,1);save(argv[1],"Mix-Rebajada");
    p.page=PANEL_HOME;
    screen(&p,v,panel_keys(&p,v),1,0,1);save(argv[1],"Band-Rebajada");
    v.speed=100;
    v.playing=0; v.song=0; v.study=0; v.next_part=PART_NONE;
    screen(&p,v,panel_keys(&p,v),1,1,2);
    save(argv[1],"Band-Ready");
    v.song=6; v.study=4; v.bpm=92;
    p.page=PANEL_BASS;
    screen(&p,v,panel_keys(&p,v),1,0,3);
    save(argv[1],"Bass");
    v.key=0x31;v.playing=1;
    screen(&p,v,panel_keys(&p,v),0,0,4);save(argv[1],"Bass-Key-Cue");
    uint16_t queued_key_pixels[240][240];
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<240;++x) queued_key_pixels[y][x]=screen_pixels[y][x];
    screen(&p,v,panel_keys(&p,v),1,0,4);
    for(unsigned y=81;y<184;++y) for(unsigned x=0;x<240;++x) assert(queued_key_pixels[y][x]==screen_pixels[y][x]);
    v.key=0xc3;v.playing=0;
    screen(&p,v,panel_keys(&p,v),0,0,5);save(argv[1],"Bass-Key-Landed");
    v.song=4; v.study=2; v.overlays=11; v.bpm=101;
    p.page=PANEL_PERC;
    screen(&p,v,panel_keys(&p,v),1,0,4);
    save(argv[1],"Percussion");
    v.clave=2;v.playing=1;
    screen(&p,v,panel_keys(&p,v),0,0,4);save(argv[1],"Percussion-Clave-Cue");
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<240;++x) queued_key_pixels[y][x]=screen_pixels[y][x];
    screen(&p,v,panel_keys(&p,v),1,0,4);
    for(unsigned y=81;y<188;++y) for(unsigned x=0;x<240;++x) assert(queued_key_pixels[y][x]==screen_pixels[y][x]);
    v.clave=5;v.playing=0;
    screen(&p,v,panel_keys(&p,v),0,0,5);save(argv[1],"Percussion-Clave-Landed");
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<240;++x) queued_key_pixels[y][x]=screen_pixels[y][x];
    screen(&p,v,panel_keys(&p,v),1,0,5);
    for(unsigned y=81;y<188;++y) for(unsigned x=0;x<240;++x) assert(queued_key_pixels[y][x]==screen_pixels[y][x]);
    for(unsigned rhythm=12;rhythm<=13;++rhythm) {
        v.rhythm=(uint8_t)rhythm;
        screen(&p,v,panel_keys(&p,v),0,0,5);
        save(argv[1],rhythm==12?"Percussion-Clave-Linked":"Percussion-Clave-Linked-32");
        for(unsigned y=0;y<240;++y) for(unsigned x=0;x<240;++x) queued_key_pixels[y][x]=screen_pixels[y][x];
        screen(&p,v,panel_keys(&p,v),1,0,5);
        for(unsigned y=81;y<188;++y) for(unsigned x=0;x<240;++x) assert(queued_key_pixels[y][x]==screen_pixels[y][x]);
    }
    v.rhythm=255;
    screen(&p,v,panel_keys(&p,v),0,0,5);
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<240;++x) queued_key_pixels[y][x]=screen_pixels[y][x];
    screen(&p,v,panel_keys(&p,v),1,0,5);
    for(unsigned y=81;y<188;++y) for(unsigned x=0;x<240;++x) assert(queued_key_pixels[y][x]==screen_pixels[y][x]);
    v.accent=1; p.page=PANEL_RACK; p.rack_sound=3;
    screen(&p,v,panel_keys(&p,v),1,0,5);
    save(argv[1],"727-Rack");
    unsigned changed=0;
    /* Pages v3: the pads show FX as the LOOP strip (rows 171-182), not a header label. */
    uint16_t header[12][240];
    for(unsigned y=0;y<12;++y) for(unsigned x=0;x<240;++x) header[y][x]=screen_pixels[171+y][x];
    v.rack_sequence=2;
    screen(&p,v,panel_keys(&p,v),0,0,6);
    for(unsigned y=0;y<12;++y) for(unsigned x=0;x<240;++x) changed+=header[y][x]!=screen_pixels[171+y][x];
    assert(changed); /* Changing FX from pads must repaint the mode indication. */
    save(argv[1],"727-Drum-Loop");
    p.rack_edit=1; v.rack_sequence=1; v.rack_sequence_sound=3;
    v.rack_steps=0x40804080u; v.rack_accents=0x40004000u; v.position=7;
    screen(&p,v,panel_keys(&p,v),1,0,6); save(argv[1],"727-Editor");
    p.rack_edit=0;
    v.song=1; v.next_song=4; p.page=PANEL_SET;
    screen(&p,v,panel_keys(&p,v),1,0,6);
    save(argv[1],"Set-List");

    p.slot=15;
    v.next_song=15;
    stage_touch(7);
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,7);
    assert(stage_work() && win_y==ORDER_TOP+15u*ORDER_HEIGHT && win_h==ORDER_HEIGHT);
    assert(screen_pixels[win_y][0]==PINK && screen_pixels[ORDER_TOP+ORDER_HEIGHT][0]==GOLD);
    while(stage_work()) {}
    assert(screen_pixels[ORDER_TOP+4u*ORDER_HEIGHT][0]==INK);
    save(argv[1],"Set-List-End-Cue");
    p.slot=0;
    screen(&p,v,panel_keys(&p,v),0,0,8);
    assert(screen_pixels[ORDER_TOP+15u*ORDER_HEIGHT][0]==PINK);
    v.next_song=SET_SLOTS;
    screen(&p,v,panel_keys(&p,v),0,0,9);
    assert(screen_pixels[ORDER_TOP+15u*ORDER_HEIGHT][0]==PLUM);
    v.result=BOARD_SAVED;
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,10);
    assert((jobs&(1u<<ORDER_ROW))==0); /* Flash confirmation only changes the banner. */
    while(stage_work()) {}
    v.result=0;

    p.page=PANEL_HOME;
    v.playing=1; v.song=0; v.next_song=1; v.position=29; v.next_part=PART_NONE;
    screen(&p,v,panel_keys(&p,v),1,0,1000000);
    v.song=1; v.next_song=SET_SLOTS; v.position=0;
    screen(&p,v,panel_keys(&p,v),0,0,2000000);
    save(argv[1],"A1-Song-Wipe");
    screen(&p,v,panel_keys(&p,v),0,0,16400000);
    save(argv[1],"A1-Take-It-Away");
    v.section=1; v.part=PART_NONE;
    screen(&p,v,panel_keys(&p,v),0,0,42000000);
    save(argv[1],"A2-Energy-Lift");
    v.section=2;
    screen(&p,v,panel_keys(&p,v),0,0,99000000);
    save(argv[1],"A3-Energy-Break");
    v.section=0;
    screen(&p,v,panel_keys(&p,v),0,0,155000000);
    save(argv[1],"A3-Back-In");
    v.next_part=PART_CHORUS; v.fill=2; v.position=28;
    screen(&p,v,panel_keys(&p,v),0,0,200000000);
    screen(&p,v,panel_keys(&p,v),0,0,210000000);
    save(argv[1],"A4-Fill-Sweep");
    v.next_part=PART_NONE; v.fill=0; v.position=0; v.part=PART_CHORUS;
    screen(&p,v,panel_keys(&p,v),0,0,214000000);
    save(argv[1],"A4-Fill-Landing");
    v.part=PART_END; v.position=24;
    screen(&p,v,panel_keys(&p,v),0,0,280000000);
    save(argv[1],"A5-Ending");
    v.playing=0; v.position=0; v.part=PART_VERSE_A;
    screen(&p,v,panel_keys(&p,v),0,0,310000000);
    save(argv[1],"A5-Blackout");
    screen(&p,v,panel_keys(&p,v),0,0,319600000);
    save(argv[1],"A5-Signoff");
    for(unsigned i=0;i<5;++i) assert(compas_animation_counts[i]>0);
    unsigned endings=compas_animation_counts[4];
    v.playing=1; v.part=PART_END; v.position=24;
    screen(&p,v,panel_keys(&p,v),0,0,370000000);
    ++p.stop_epoch;
    screen(&p,v,panel_keys(&p,v),0,0,371000000);
    v.playing=0; v.part=PART_VERSE_A; v.position=0;
    screen(&p,v,panel_keys(&p,v),0,0,371200000);
    assert(compas_animation_counts[4]==endings);
    /* The fill-roll subdivisions follow effective tempo even below 60 BPM. */
    motion=(StageMotion){0};v.playing=1;v.piano=0;v.fill=2;
    v.next_part=PART_CHORUS;v.bpm=100;v.speed=75;v.study=1;
    assert(motion_offer(v,v,&p,0,7,7,2100000).roll==1);
    assert(motion_offer(v,v,&p,0,7,7,2500000).roll==2);
    v.bpm=60;v.speed=60;
    assert(board_effective_bpm(v)==36);
    assert(motion_offer(v,v,&p,0,7,7,4500000).roll==1);
    assert(motion_offer(v,v,&p,0,7,7,5100000).roll==2);
    v=(BoardView){.speed=100,.rhythm=255,.next_rhythm=255,.song=9,.study=0,.piano=2,
        .bpm=84,.master=75,.pending=3,.next_song=SET_SLOTS,.next_part=PART_NONE,
        .next_groove=BOARD_GROOVE_PRESETS,.part=PART_NONE,.score_has_guide=1,
        .score_percussion=SCORE_PERC_AVAILABLE,.score_beats=4};
    p.page=PANEL_HOME;
    screen(&p,v,panel_keys(&p,v),1,0,1000000000); save(argv[1],"Score-Ready");
    v.playing=1;v.score_form=PIANO_FORM_VERSE;v.score_guide=1;v.score_hold=1;
    screen(&p,v,panel_keys(&p,v),0,0,1000000001); save(argv[1],"Score-Hold");
    v.score_hold=0;v.score_ending=1;v.next_part=PART_END;
    screen(&p,v,panel_keys(&p,v),0,0,1000000002); save(argv[1],"Score-Coda-Cued");
    v.key=0xc0;v.next_part=PART_NONE;v.score_ending=0;
    screen(&p,v,panel_keys(&p,v),1,0,1000000003);
    const unsigned shifts[]={0xa0,0xc0,0xa0,0xca};
    for(unsigned k=0;k<4;++k) {
        v.key=(uint8_t)shifts[k];
        screen(&p,v,panel_keys(&p,v),0,0,1000000003);
        if(k==0) save(argv[1],"Score-Transpose-Cued");
        if(k==3) save(argv[1],"Score-Transpose-Landed");
        for(unsigned y=196;y<238;++y) for(unsigned x=180;x<240;++x) queued_key_pixels[y][x]=screen_pixels[y][x];
        screen(&p,v,panel_keys(&p,v),1,0,1000000003);
        for(unsigned y=196;y<238;++y) for(unsigned x=180;x<240;++x) assert(queued_key_pixels[y][x]==screen_pixels[y][x]);
    }
    p.page=PANEL_BASS;
    screen(&p,v,panel_keys(&p,v),1,0,1000000003); save(argv[1],"Score-Bass");
    p.page=PANEL_PERC;
    screen(&p,v,panel_keys(&p,v),1,0,1000000004); save(argv[1],"Score-Percussion");
    /* Review fix 4: the score page shows the bolero as live fader caps; the
     * maraca cap (x 76-95) sits high and MINT while the bolero plays. */
    unsigned on_pixels=0;
    for(unsigned y=118;y<125;++y) for(unsigned x=76;x<96;++x) on_pixels+=screen_pixels[y][x]==MINT;
    assert(on_pixels);
    v.score_percussion|=SCORE_PERC_MUTED;
    stage_touch(1000000004);stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,1000000004);
    /* The mute moves the fader caps and caption in the body. (CLOCK also
     * repaints; R3 reviews whether it must.) */
    assert((jobs&(1u<<BODY)) && !(jobs&~((1u<<STATUS)|(1u<<CLOCK)|(1u<<BODY))));
    while(stage_work()) {}
    unsigned muted_pixels=0;
    for(unsigned y=118;y<125;++y) for(unsigned x=76;x<96;++x) assert(screen_pixels[y][x]!=MINT);
    for(unsigned y=143;y<150;++y) for(unsigned x=76;x<96;++x) muted_pixels+=screen_pixels[y][x]==DIM;
    assert(muted_pixels); /* Muted: the cap drops to the bottom and dims. */
    save(argv[1],"Score-Percussion-Muted");
    v.score_percussion=SCORE_PERC_AVAILABLE;
    /* A song sign changes every 50 ms. Coalesce its decorative frames until
     * the complete first image reaches glass, without losing the animation. */
    p.page=PANEL_HOME;v.song=8;v.next_song=9;v.playing=1;v.key=0xc0;
    screen(&p,v,panel_keys(&p,v),1,0,1100000000);
    v.song=9;v.next_song=SET_SLOTS;
    unsigned stamp=1100100000;
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,stamp);
    assert(motion.kind==MOTION_SONG);
    unsigned stripes=0;
    while(stage_work()) {
        assert(++stripes<40);
        stamp+=180000; /* 7.5 ms foreground passes overlap the 50 ms animation. */
        if(jobs) stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,stamp);
    }
    assert(pixels==57600 && motion.frame==0);
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,stamp+1200000);
    assert(motion.frame>0);
    while(stage_work()) {}
    /* PLAY can arrive before the stopped startup screen finishes. It owns a
     * fresh complete frame, rather than merging two transport states. */
    v.playing=0;stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},1,0,1200000000);
    assert(stage_work() && jobs && stripe_row);
    v.playing=1;stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,1200120000);
    assert(!stripe_row && pixels==0 && redraw_started==1200120000);
    while(stage_work()) {}
    assert(pixels==57600);
    /* A changed banner goes ahead of the pending body and footer.
     * Its rows count once, and the final image still matches. */
    touching=0;
    v.result=0;
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},1,0,1210000000);
    assert(stage_work() && (jobs&(1u<<BODY)) && (jobs&(1u<<FOOTER)));
    v.result=BOARD_SAVED;
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,1210120000);
    while(working_job==STATUS && (jobs&(1u<<STATUS))) assert(stage_work());
    assert(stage_work() && working_job==NOTICE && (jobs&(1u<<BODY)));
    while(stage_work()) {}
    assert(pixels==57600);
    for(unsigned y=184;y<196;++y) for(unsigned x=0;x<240;++x)
        queued_key_pixels[y][x]=screen_pixels[y][x];
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},1,0,1210120000);
    while(stage_work()) {}
    for(unsigned y=184;y<196;++y) for(unsigned x=0;x<240;++x)
        assert(queued_key_pixels[y][x]==screen_pixels[y][x]);

    /* If the banner changes after its priority transfer, it must repaint
     * without leaving the previous message on screen. */
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},1,0,1220000000);
    v.result=BOARD_EMPTY;
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,1220120000);
    assert(stage_work() && working_job==NOTICE);
    v.result=BOARD_SAVED;
    stage_offer(&p,v,panel_keys(&p,v),(RackFeedback){.hits=0},0,0,1220240000);
    assert(stage_work() && working_job==NOTICE);
    while(stage_work()) {}
    assert(pixels==60480);
    for(unsigned y=184;y<196;++y) for(unsigned x=0;x<240;++x)
        assert(queued_key_pixels[y][x]==screen_pixels[y][x]);
    v.result=0;
    v.song=8;v.piano=1;v.score_percussion=0;
    screen(&p,v,panel_keys(&p,v),1,0,1000000005); save(argv[1],"Score-No-Percussion");

    /* Score-specific performer labels must survive both screen surfaces. */
    v=(BoardView){.speed=100,.rhythm=255,.next_rhythm=255,.song=10,.piano=PIANO_SCORE_CIELITO,
        .bpm=108,.master=75,.pending=3,.next_song=SET_SLOTS,.next_part=PART_NONE,
        .part=PART_INTRO,.score_has_guide=1,.score_guide=1,.score_beats=3,.key=0xc0,.clave=4,
        .next_groove=BOARD_GROOVE_PRESETS};
    p.page=PANEL_HOME;
    char face[FACE_LINES][FACE_COLS+1];
    panel_face(&p,v,face);assert(face[3][2]=='P' && face[4][11]=='E');
    screen(&p,v,panel_keys(&p,v),1,0,1300000000);save(argv[1],"Cielito-Ready");
    v.playing=1;v.score_form=PIANO_FORM_VERSE;v.part=PART_VERSE_A;
    v.next_part=PART_END;v.score_ending=1;
    panel_face(&p,v,face);assert(face[4][0]=='E');
    screen(&p,v,panel_keys(&p,v),0,0,1324000000);
    screen(&p,v,panel_keys(&p,v),0,0,1348000000);save(argv[1],"Cielito-Ending-Cued");
    v.next_part=PART_NONE;v.score_form=PIANO_FORM_CODA;v.part=PART_END;v.score_bar=31;
    screen(&p,v,panel_keys(&p,v),0,0,1372000000);save(argv[1],"Cielito-Ending");

    /* A live key cue must cut the Lift stamp and repaint every marquee bulb. */
    p=(Panel){.stop_epoch=p.stop_epoch};
    v=(BoardView){.playing=1,.song=1,.study=1,.position=4,.bpm=101,.speed=100,
        .master=74,.key=0xc0,.clave=4,.pending=3,.part=PART_NONE,.next_part=PART_NONE,
        .next_song=SET_SLOTS,.next_groove=BOARD_GROOVE_PRESETS,
        .rhythm=BOARD_CUSTOM_RHYTHM,.next_rhythm=BOARD_CUSTOM_RHYTHM};
    screen(&p,v,panel_keys(&p,v),1,0,1400000000);
    uint16_t steady_marquee[7][240];
    for(unsigned y=0;y<7;++y) for(unsigned x=0;x<240;++x)
        steady_marquee[y][x]=screen_pixels[y][x];
    v.section=1;
    screen(&p,v,panel_keys(&p,v),0,0,1401200000);
    assert(motion.kind==MOTION_LIFT);
    assert(screen_pixels[3][2]!=steady_marquee[3][2]);
    save(argv[1],"Lift-Marquee");
    v.key=0x10;
    screen(&p,v,panel_keys(&p,v),0,0,1402400000);
    assert(motion.kind==MOTION_NONE);
    for(unsigned y=0;y<7;++y) for(unsigned x=0;x<240;++x)
        assert(steady_marquee[y][x]==screen_pixels[y][x]);
    save(argv[1],"Lift-Canceled-by-Key-Cue");

}
