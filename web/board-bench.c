/* The browser runs the board owner and panel in its AudioWorklet. A second
 * instance composites display snapshots on the main thread. */
#include "panel.h"

typedef struct { Panel panel; BoardView view; PanelKeys keys; RackFeedback rack; } BenchView;
static BenchView snapshot;
static Panel panel;
static uint32_t ticks;
static uint16_t glass[240*240];
static unsigned left, top, width, offset;

static void lcd_sync(void) {}
static void lcd_window(unsigned x,unsigned y,unsigned right,unsigned bottom)
{
    (void)bottom;
    left=x; top=y; width=right-x+1u; offset=0;
}
static void lcd_data(const uint16_t *pixels,unsigned bytes)
{
    for(unsigned i=0;i<bytes/2u;++i,++offset) {
        uint16_t p=pixels[i];
        glass[(top+offset/width)*240u+left+offset%width]=(uint16_t)(p>>8|p<<8);
    }
}
static void lcd_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *pixels)
{
    lcd_window(x,y,x+w-1u,y+h-1u);
    lcd_data(pixels,w*h*2u);
}
static void lcd_fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t pixel)
{
    for(unsigned i=0;i<h;++i)
        for(unsigned j=0;j<w;++j) glass[(y+i)*240u+x+j]=pixel;
}
#include "../board/stage.c"

void *memcpy(void *dest,const void *source,__SIZE_TYPE__ bytes)
{
    uint8_t *out=dest;
    const uint8_t *in=source;
    for(__SIZE_TYPE__ i=0;i<bytes;++i) out[i]=in[i];
    return dest;
}
void *memset(void *dest,int value,__SIZE_TYPE__ bytes)
{
    uint8_t *out=dest;
    for(__SIZE_TYPE__ i=0;i<bytes;++i) out[i]=(uint8_t)value;
    return dest;
}
uint32_t board_ticks(void) { return ticks; }
uint32_t board_pending(void) { return BOARD_HALF_IRQ; }
void board_ack_aux(uint32_t pending) { (void)pending; }
unsigned board_free_half(void) { return 0; }
void board_ack_half(void) {}
void board_disable_output(void) {}
uint32_t board_lock(void) { return 0; }
void board_unlock(uint32_t previous) { (void)previous; }

static int32_t enc[7];
static uint8_t prepared_set[BOARD_SET_BYTES];
static uint8_t prepared_rhythms[BOARD_RHYTHM_SLOTS_BYTES];
uint8_t *bench_rhythm_slots_buffer(void) { return prepared_rhythms; }
unsigned bench_rhythm_slots_size(void) { return sizeof prepared_rhythms; }
static uint8_t portable_rhythm[BOARD_RHYTHM_BYTES];
uint8_t *bench_rhythm_buffer(void) { return portable_rhythm; }
unsigned bench_rhythm_size(void) { return sizeof portable_rhythm; }
unsigned bench_rhythm_export(void) { return board_rhythm_export(portable_rhythm,sizeof portable_rhythm); }
unsigned bench_rhythm_import(void)
{
    unsigned code=board_rhythm_import(portable_rhythm,sizeof portable_rhythm);
    if(!code) {
        panel.page=PANEL_RACK;panel.rack_edit=1;panel.bar=0;
        panel.rack_sound=portable_rhythm[11];
    }
    return code;
}
int32_t *bench_encoders(void) { return enc; }
uint8_t *bench_set_buffer(void) { return prepared_set; }
unsigned bench_set_size(void) { return sizeof prepared_set; }
unsigned bench_playing(void) { return board_view(panel.role).playing; }
unsigned bench_pull_saved_set(void) { return board_store_pull(prepared_set); }
unsigned bench_update_set_display(void) { return board_store_load(prepared_set); }
unsigned bench_boot(unsigned prepared)
{
    board_init();
#ifdef COMPAS_HANAN
    if(prepared&4u) panel.page=PANEL_HANAN;
#endif
    if((prepared&1) && !board_store_load(prepared_set)) return 0;
    if((prepared&2) && board_rhythm_slots_load(prepared_rhythms,sizeof prepared_rhythms)) return 0;
    return board_submit((BoardCommand){BOARD_CUE,0,0})==CP_OK;
}
#ifdef COMPAS_HANAN
int bench_kit_restore(unsigned drive,unsigned decay,unsigned pitch,unsigned clock)
{
    if(drive>100 || decay<5 || decay>100 || pitch>24 || clock>KUMBIA_CLOCK_MAX) return 0;
    const uint8_t settings[KUMBIA_SETTINGS]={drive,decay,pitch,clock};
    return board_kumbia_restore(settings);
}
#endif
const char *bench_song_title(unsigned slot)
{
    return board_song_exists(slot)?board_song_title(slot):0;
}
unsigned bench_cue(unsigned slot)
{
    if(!board_song_exists(slot)) return BOARD_EMPTY;
    unsigned result=board_submit((BoardCommand){BOARD_CUE,slot,0});
    if(!result) panel.slot=(uint8_t)slot;
    return result;
}
/* The browser player follows the page language; 0 English, 1 Spanish. */
void bench_lang(unsigned lang) { panel_lang=(uint8_t)(lang<2u?lang:0u); }
void bench_input(uint32_t buttons,uint32_t keys,uint32_t time)
{
    panel_scan(&panel,buttons,keys,enc,time);
    for(unsigned i=0;i<7;++i) enc[i]=0;
}
/* Compás 1.2: output samples that hit the codec rail since boot */
uint32_t bench_rails(void)
{
    return board_stats.rail_samples;
}
/* Compás 1.2: blocks where the CPU guard skipped ROOM or ECHO */
uint32_t bench_fx_shed(void)
{
    return board_stats.fx_shed_blocks;
}

void bench_master(unsigned percent)
{
    board_submit((BoardCommand){BOARD_MASTER,percent,0});
}
int32_t *bench_audio(void)
{
    ticks+=BOARD_FRAMES*BOARD_TICKS_PER_FRAME;
    fm1_alnk0_irq();
    return board_dma[0];
}
unsigned bench_show_export(void)
{
    BoardView v=board_view(0);
    if(v.playing) return CP_STOP_FIRST;
    if(v.song>=SET_SLOTS) return CP_BAD_PROJECT;
    unsigned code=board_submit((BoardCommand){BOARD_SAVE,v.song,0});
    if(code) return code;
    bench_audio();
    if(board_view(0).result!=BOARD_SESSION) return CP_BUSY;
    if(!board_store_snapshot(prepared_set)) return CP_BUSY;
    return board_rhythm_slots_export(prepared_rhythms,sizeof prepared_rhythms);
}
BenchView *bench_snapshot(void) { return &snapshot; }
unsigned bench_snapshot_size(void) { return sizeof snapshot; }
void bench_read_view(void)
{
    snapshot.panel=panel;
    snapshot.view=board_view(panel.page==PANEL_BASS?BOARD_BASS_LINE_ROLE:panel.role);
    snapshot.keys=panel_keys(&panel,snapshot.view);
    snapshot.rack=board_rack_feedback();
}
uint16_t *bench_splash(void) { splash(); return glass; }
uint16_t *bench_draw(uint32_t time)
{
    static uint8_t opening=1, old_song=SET_SLOTS, cameo;
    static uint32_t cameo_at;
    ticks=time;
    if(old_song!=snapshot.view.song) {
        old_song=snapshot.view.song;
        cameo=old_song<SET_SLOTS;
        cameo_at=time;
    }
    if(snapshot.view.fault || time-cameo_at>=21600000u) cameo=0;
    stage_offer(&snapshot.panel,snapshot.view,snapshot.keys,snapshot.rack,opening,cameo && !snapshot.view.result,time);
    opening=0;
    while(stage_work()) {}
    return glass;
}
