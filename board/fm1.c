/* SPDX-License-Identifier: GPL-3.0-only
 * Offline FM-1 adapter. The original register definitions, IRQ entry and
 * ST7789 driver are consumed from the pinned Felucca BSP (see board/BSP.json).
 */
#include "panel.h"
#include "recovery.h"
#include "fm1_adc.h"
#include "fm1_audio.h"
#include "fm1_input.h"
#include "fm1_lcd_hw.h"
#include "fm1_sys.h"
#include "lcd.c"
/* Keep quadrature scanning alive while SPI DMA owns the drawing stripe.
 * A 3,360-pixel transfer takes 4.48 ms at the pinned 12 MHz clock: polling
 * only after it can skip both states of a detent. Scanning touches the key
 * matrix, never the stripe or the panel command owner. Leave a conservative
 * 1.33 ms tail to lcd_sync so a final scan cannot extend the transfer wait.
 * This hook runs only for stage transfers, after input/audio initialization. */
static void scan_during_transfer(unsigned pixels)
{
    uint32_t started=fm1_ticks(), duration=pixels*32u;
    while(!(FM1_LCD_SPI_CON&0x8000u) &&
          fm1_ticks()-started+32000u<duration)
        fm1_input_scan();
}
#define STAGE_TRANSFER_POLL(pixels) scan_during_transfer(pixels)
#include "stage.c"
#undef STAGE_TRANSFER_POLL
#ifdef COMPAS_EMU_STORE
#include "store.h"
#ifdef COMPAS_USB
#include "usb/identity.h"
#define FL_RANGE_OK(off,n) (FL_IN(off,n,STORE_A,STORE_B+STORE_SECTOR) || \
                            FL_IN(off,n,COMPAS_OTA_START,COMPAS_OTA_END))
#else
#define FL_RANGE_OK(off,n) FL_IN(off,n,STORE_A,STORE_B+STORE_SECTOR)
#endif
#include "fm1_flash.h"
extern uint32_t _rt_start[], _rt_end[], _rt_load[];
static unsigned store_enabled;
#endif

extern uint32_t _bss_start[], _bss_end[], _pool_start[], _pool_end[];
extern uint32_t _data_start[], _data_end[], _data_load[];
extern uint32_t _sstack_lo[], _sstack_top[];
extern void isr_alnk0(void);
static Panel panel;
static volatile RecoveryGuard recovery_guard __attribute__((section(".noinit")));
volatile uint32_t compas_supervisor_watermark;
enum { STACK_PATTERN=0x5ca17 };
static PanelKeys screen_keys;
static uint8_t dim_keys[FM1_NCOL], bright_keys[FM1_NCOL], queued_keys[FM1_NCOL];

static void update_key_map(PanelKeys map)
{
    if(screen_keys.parts==map.parts && screen_keys.upper==map.upper &&
       screen_keys.pads==map.pads && screen_keys.active==map.active &&
       screen_keys.queued==map.queued && screen_keys.enabled==map.enabled) return;
    screen_keys=map;
    for(unsigned col=0;col<FM1_NCOL;++col) {
        unsigned dim=0, bright=0, queued=0;
        for(unsigned row=1;row<5;++row) {
            int id=FM1_KEYMAP[row][col];
            if(id<14) continue;
            unsigned note=1u<<(id-14);
            if(note&(map.parts|map.upper|map.pads)) dim|=1u<<row;
            if(note&(map.active|map.enabled)) bright|=1u<<row;
            if(note&map.queued) queued|=1u<<row;
        }
        dim_keys[col]=(uint8_t)dim;
        bright_keys[col]=(uint8_t)bright;
        queued_keys[col]=(uint8_t)queued;
    }
}

static void update_key_lights(uint32_t now,unsigned rapid)
{
    unsigned dim=(fm1_in.frames&7u)<2u;
    unsigned pulse=(now/(rapid?1440000u:2880000u))&1u;
    for(unsigned col=0;col<FM1_NCOL;++col)
        fm1_led[col]=bright_keys[col] | (pulse?queued_keys[col]:0u) | (dim?dim_keys[col]:0u);
}

uint32_t board_ticks(void) { return fm1_ticks(); }
uint32_t board_pending(void) { return fm1_audio_pending(); }
void board_ack_aux(uint32_t pending) { fm1_audio_ack_aux((uint8_t)pending); }
unsigned board_free_half(void) { return fm1_audio_free_half(); }
void board_ack_half(void) { fm1_audio_ack_half(); }
void board_disable_output(void) { fm1_audio_stop(); }
uint32_t board_lock(void) { return fm1__lock(); }
void board_unlock(uint32_t previous) { fm1__unlock(previous); }

void fm1_timer5_irq(void) {} /* Only the audio IRQ is attached. Scan the panel in the foreground. */
static void fm1_fault(const fm1_crash_t *crash)
{
    (void)crash;
    fm1_audio_stop();
    for (;;) {} /* watchdog resets the board */
}
void *memcpy(void *dest, const void *source, unsigned bytes)
{
    uint8_t *out=dest;
    const uint8_t *in=source;
    while (bytes--) *out++=*in++;
    return dest;
}
void *memset(void *dest, int value, unsigned bytes)
{
    uint8_t *out=dest;
    while (bytes--) *out++=(uint8_t)value;
    return dest;
}

static void mark_supervisor_stack(void)
{
    const volatile uint32_t *probe=_sstack_lo;
    while(probe<_sstack_top && *probe==STACK_PATTERN) ++probe;
    compas_supervisor_watermark=(uint32_t)((uintptr_t)_sstack_top-(uintptr_t)probe);
}

#ifdef COMPAS_EMU_STRESS
/* Separate emulator-only image. The regular board always boots stopped. */
static void emu_load(void)
{
    static uint32_t last, next;
    static unsigned stage;
    const volatile BoardStats *stats=&board_stats;
    const uint32_t block=stats->callbacks;
    if(block<20 || block==last || block<next) return;
    last=block;
    if(stage==0) {
        board_submit((BoardCommand){BOARD_ASSIGN,0,15});
        board_submit((BoardCommand){BOARD_ASSIGN,1,14});
        board_submit((BoardCommand){BOARD_MASTER,100,0});
        board_submit((BoardCommand){CP_BPM,180,0});
        board_submit((BoardCommand){CP_FEEL,100,0});
        next=block+8;
    } else if(stage==1) {
        board_submit((BoardCommand){CP_START,0,0});
        next=block+24;
    } else if(stage==24) {
        board_submit((BoardCommand){BOARD_CUE,8,0});
        next=block+24;
    } else if(stage==44) {
        board_submit((BoardCommand){BOARD_GUIDE,0,0});
        board_submit((BoardCommand){BOARD_ENDING,0,0});
        next=block+24;
    } else if(stage==9) {
        board_submit((BoardCommand){CP_SECTION,1,0});
        next=block+24;
    } else {
        static const uint8_t sound[]={0,2,4,5,6,7,9,11};
        for(unsigned i=0;i<sizeof sound;++i)
            board_submit((BoardCommand){BOARD_RACK,sound[i],127});
        next=block+24;
    }
    ++stage;
}
#endif

#ifdef COMPAS_EMU_STORE
int store_read(uint32_t off, void *dest, unsigned size)
{
    return FL_FAR(fl_read_ram)(off,dest,size);
}
int store_erase(uint32_t off)
{
    uint32_t elapsed;
    fm1_wdt_feed();
    return FL_FAR(fl_erase4k_ram)(off,&elapsed);
}
int store_program(uint32_t off, const void *src, unsigned size)
{
    uint32_t elapsed;
    fm1_wdt_feed();
    return FL_FAR(fl_prog_ram)(off,src,size,&elapsed);
}

static void store_open(void)
{
#ifdef COMPAS_NO_SHOW_STORE
    /* 1.0 release: the BB1 layout puts the app area over 0x90000-0x93000, so
     * nothing is ever written there; the set list lives until power-off. */
    return;
#endif
    const char *marker=STORE_MARKER;
    char readback[sizeof STORE_MARKER-1];
    if(FL_FAR(fl_jedec_ram)()!=0x856014u ||
       store_read(STORE_FIXTURE,readback,sizeof readback)) return;
    for(unsigned i=0;i<sizeof readback;++i)
        if(readback[i]!=marker[i]) return;
    store_enabled=1;
    store_boot();
}

static void store_poll(void)
{
    if(!store_enabled || !board_store_pending()) return;
    uint32_t irq=board_lock();
    if(!board_store_pending()) {
        board_unlock(irq);
        return;
    }
    /* NOR temporarily takes XIP offline. Quiesce DMA before touching it. */
    fm1_audio_stop();
    fm1_audio_ack_half();
    board_store_result(store_commit());
    for(unsigned half=0;half<2;++half)
        for(unsigned word=0;word<BOARD_WORDS;++word) board_dma[half][word]=0;
    fm1_audio_init(board_dma[0],BOARD_WORDS,isr_alnk0,3);
    board_unlock(irq);
}
#endif

#ifdef COMPAS_USB
#include "usb/runtime.inc"

/* Spell out the store: the PI32 compiler can emit a stride of eight for
 * the paired pointer increments in this copy loop. */
static __attribute__((noinline)) void usb_load_ram_code(void)
{
    const uint32_t *source=_rt_load;
    for(uint32_t *dest=_rt_start;dest<_rt_end;++dest) {
        uint32_t word=*source++;
        __asm__ volatile("[%0] = %1" :: "r"(dest), "r"(word) : "memory");
    }
}
#endif

/* Check only at startup, with audio and LCD still inactive. */
static unsigned recovery_chord_held(void)
{
    const uint32_t chord=(1u<<FM1_BTN_OCT_DOWN)|(1u<<FM1_BTN_OCT_UP);
    for(unsigned frame=0;frame<FM1_DEB_PRESS+2;++frame)
        fm1_input_scan();
    const uint32_t pressed=fm1_in.buttons;
#ifdef COMPAS_USB
    const uint32_t rom_chord=chord|(1u<<PANEL_PLAY_BUTTON);
    if((pressed!=chord && pressed!=rom_chord) || fm1_in.notes) return 0;
#else
    if(pressed!=chord || fm1_in.notes) return 0;
#endif

    const uint32_t start=fm1_ticks();
    do {
        fm1_input_scan();
        if(fm1_in.buttons!=pressed || fm1_in.notes) return 0;
    } while(fm1_ticks()-start<6000000u); /* 250 ms at 24 MHz */
    return pressed==chord ? 1u : 2u;
}

#ifndef COMPAS_LANG_DEFAULT
#define COMPAS_LANG_DEFAULT 0
#endif

void fm1_cstart(void)
{
    uint32_t *dest;
    const uint32_t *source;
    for(dest=_bss_start; dest<_bss_end; ++dest) *dest=0;
    for(dest=_pool_start; dest<_pool_end; ++dest) *dest=0;
    for(dest=_data_start,source=_data_load; dest<_data_end; ++dest) *dest=*source++;
    for(volatile uint32_t *mark=_sstack_lo;mark<_sstack_top;++mark) *mark=STACK_PATTERN;

    fm1_time_init();
    fm1_reset_reason();
    fm1_wdt_arm(0x0D);
#ifdef COMPAS_USB
    unsigned safe=recovery_usb_begin(&recovery_guard,fm1_boot.p3_rst&4u);
    fm1_input_init();
    unsigned chord=recovery_chord_held();
    if(chord==2) compas_enter_rom();
    fm1_irq_init();
    lcd_init();
    usb_load_ram_code();
    usb_flash_ok=usb_supported_flash();
    if(usb_flash_ok) (void)ota_boot_cleanup(); /* before safe mode too, as BB1 does */
    if(safe || chord) {
        fm1_irq_enable_all();
        compas_usb_safe();
    }
#else
    if(recovery_begin(&recovery_guard,fm1_boot.p3_rst&4u))
        fm1_enter_uboot();
    fm1_input_init();
    if(recovery_chord_held()) {
        recovery_healthy(&recovery_guard);
        fm1_enter_uboot();
    }
    fm1_irq_init();
    lcd_init();
#endif
    board_init();
    panel_lang=COMPAS_LANG_DEFAULT;
    fm1_adc_init();
    (void)fm1_adc_read(FM1_ADC_MASTER);
    panel_master(&panel,fm1_adc_read(FM1_ADC_MASTER));
#ifdef COMPAS_EMU_STORE
    for(dest=_rt_start,source=_rt_load; dest<_rt_end; ++dest) *dest=*source++;
    store_open();
#endif
    fm1_audio_init(board_dma[0],BOARD_WORDS,isr_alnk0,3);
    fm1_irq_enable_all();
    board_submit((BoardCommand){BOARD_CUE,0,0});
    splash();
    const uint32_t splash_started=fm1_ticks();
    uint32_t last_frame=splash_started, last_master=splash_started;
    unsigned opening=1, previous_song=SET_SLOTS, rapid=0;
    for (;;) {
        fm1_wdt_feed();
#ifdef COMPAS_USB
        compas_usb_poll();
        ota_service();
        if(usb.uboot_req) {
            fm1_audio_stop();
            compas_enter_rom();
        }
        if(usb.ota_req) {
            usb.ota_req=0;
            if(usb_flash_ok && board_ota_idle()) {
                fm1_audio_stop();
                fm1_audio_ack_half();
                ota_session();
                for(unsigned half=0;half<2;++half)
                    for(unsigned word=0;word<BOARD_WORDS;++word) board_dma[half][word]=0;
                fm1_audio_init(board_dma[0],BOARD_WORDS,isr_alnk0,3);
                model.panel.page=255;
                jobs=0;
            }
        }
#endif
#ifdef COMPAS_EMU_STORE
#ifdef COMPAS_USB
        /* Finish the frame already on the wire before an XIP-stalling save. */
        if(!jobs || measurement!=3)
#endif
            store_poll();
#endif
#ifdef COMPAS_EMU_STRESS
        emu_load();
#endif
        fm1_input_scan();
        int32_t enc[7];
        const uint32_t buttons=fm1_input_edges(0), keys=fm1_in.notes;
        unsigned activity=(buttons|(keys&~panel.held))!=0;
        for(unsigned i=0;i<7;++i) {
            enc[i]=fm1_enc_take(i);
            if(enc[i]) activity=1;
        }
        const uint32_t now=fm1_ticks();
        if(recovery_guard.pending && now-splash_started>=720000000u)
            recovery_healthy(&recovery_guard);
        panel_scan(&panel,buttons,keys,enc,now);
        update_key_lights(now,rapid);
        if(now-last_master>=480000u) {
            last_master=now;
            panel_master(&panel,fm1_adc_read(FM1_ADC_MASTER));
        }
        if(activity && !opening) stage_touch(now);
        if((opening && activity) || now-last_frame>=120000u) {
            last_frame=now;
            const BoardView v=board_view(panel.page==PANEL_BASS?BOARD_BASS_LINE_ROLE:panel.role);
            if(opening && !activity && !v.fault && now-splash_started<15600000u) continue;
            if(v.song!=previous_song) {
                previous_song=v.song;
                cameo_active=v.song<SET_SLOTS;
                cameo_started=now;
            }
            if(v.fault || now-cameo_started>=21600000u) cameo_active=0;
            PanelKeys keys_for_screen=panel_keys(&panel,v);
            update_key_map(keys_for_screen);
            rapid=v.next_bar;
            stage_offer(&panel,v,keys_for_screen,board_rack_feedback(),opening,cameo_active && !v.result,now);
            opening=0;
            mark_supervisor_stack();
        }
        /* A beat has a bounded set of small regions. Finish that set in
         * one pass so a new decorative offer cannot restart its strips. */
        do { stage_work(); } while(jobs && measurement==1);
    }
}
