#ifndef COMPAS_BOARD_H
#define COMPAS_BOARD_H
#include <stdint.h>
#include "engine.h"

enum { BOARD_FRAMES=128, BOARD_WORDS=BOARD_FRAMES*2, RACK_VOICES=10, RACK_SOUNDS=15, SET_SLOTS=16, BOARD_GROOVE_PRESETS=29, BOARD_CONGA_PATTERNS=8, BOARD_ROLAND_LESSON=18, BOARD_COLOMBIAN_RHYTHMS=19, BOARD_RHYTHM_PATTERNS=22, BOARD_CUSTOM_RHYTHM=255 };
enum { BOARD_RACK=32, BOARD_KIT, BOARD_MASTER, BOARD_ACCENT, BOARD_ASSIGN, BOARD_CUE, BOARD_SAVE, BOARD_REPEAT, BOARD_HOLD, BOARD_ENDING, BOARD_GUIDE, BOARD_PART, BOARD_PAD_HITS, BOARD_BASS_STYLE, BOARD_BASS_SHAPE, BOARD_OVERLAY, BOARD_OVERLAY_LEVEL, BOARD_RACK_CONTROL, BOARD_SEQ_SELECT, BOARD_SEQ_STEP, BOARD_SEQ_ENABLE, BOARD_RHYTHM_CUE, BOARD_SPEED, BOARD_GROOVE_CUE, BOARD_SCORE_PERC, BOARD_COUNT_IN, BOARD_ROLL, BOARD_ROLL_KEYS, BOARD_REC };
#ifdef COMPAS_HANAN
enum { BOARD_HANAN_CUE=BOARD_REC+1, BOARD_HANAN_HIT, BOARD_HANAN_MODE, BOARD_HANAN_MOD, BOARD_HANAN_SAVE };
enum { HANAN_OFF, HANAN_LAYER, HANAN_SOLO, HANAN_PATTERNS=4 };
extern uint8_t board_hanan_mode;
enum { KUMBIA_DRIVE, KUMBIA_DECAY, KUMBIA_PITCH, KUMBIA_CLOCK, KUMBIA_SETTINGS };
enum { KUMBIA_CLOCK_MAX=54, KUMBIA_CLOCK_HOME=9804 };
static inline unsigned board_kumbia_hz(unsigned clock)
{ return clock==KUMBIA_CLOCK_MAX?4400u:KUMBIA_CLOCK_HOME-clock*100u; }
void board_hanan_step(unsigned step, unsigned accented);
void board_kumbia_export(uint8_t settings[KUMBIA_SETTINGS]);
int board_kumbia_restore(const uint8_t settings[KUMBIA_SETTINGS]);
#endif
enum { RACK_HIT_STRENGTH, RACK_ACCENT_STRENGTH, RACK_LEVEL };
enum { BOARD_OVERLAY_ALL=4 }; /* BOARD_OVERLAY_LEVEL lane: shift all four lanes, b=delta+128 */
enum { BASS_ENSEMBLE, BASS_ACID };
enum { PART_INTRO, PART_VERSE_A, PART_VERSE_B, PART_CHORUS, PART_BREAK, PART_END, PART_NONE };
static inline unsigned board_part_energy(unsigned part,unsigned fallback)
{
    if(part==PART_NONE) return fallback;
    if(part==PART_CHORUS) return 1;
    return part==PART_BREAK || part==PART_END ? 2u : 0u;
}
enum { BOARD_FAULT=8, BOARD_QUEUE_FULL, BOARD_EMPTY, BOARD_SESSION, BOARD_SAVED };
enum { BOARD_BASS_LINE_ROLE=CP_ROLES };
enum { BOARD_HALF_IRQ=128, BOARD_TICKS_PER_FRAME=544 };
typedef struct { uint8_t command, a, b; } BoardCommand;
typedef struct {
    uint8_t playing, study, section, pending, fill, position, bpm, feel, muted, solo;
    uint8_t audible, kit, master, result, fault, accent, levels[6], assigned[6];
    uint8_t song, next_song, phrases;
    uint8_t piano, score_form;
    union { uint8_t score_bar, groove_count; };
    uint8_t score_hold;
    union { uint8_t score_ending, overlay_hits; };
    union { uint8_t score_guide, rack_live; };
    uint8_t score_beats, score_has_guide;
    uint8_t clave; /* bit 0: direction; bits 1+: pending, 2 = none; occupies old padding */
    uint32_t steps;
    uint8_t part, next_part;
    uint8_t bass_style, bass_cut, bass_bite, overlays;
    uint8_t overlay_levels[4], rack_strength, rack_accent, rack_level;
    uint8_t next_bar;
    uint8_t phrases_left; /* auto-advance phrases left incl. this one; 0 = none; occupies old padding */
    uint8_t bass_hit; /* note-on sequence, after FEEL */
    uint32_t rack_steps, rack_accents;
    uint8_t rack_sequence, rack_sequence_sound, rhythm, next_rhythm;
    uint8_t speed, score_percussion, next_groove, key; /* root / pending root (12 = none); scores map 6..11 to -6..-1 */
#ifdef COMPAS_HANAN
    uint8_t hanan_pattern, hanan_next, hanan_mode, kumbia[KUMBIA_SETTINGS];
#endif
} BoardView;
enum { ROLL_OFF, ROLL_EIGHTH, ROLL_TRIPLET, ROLL_SIXTEENTH, ROLL_THIRTYSECOND };
static inline unsigned board_roll_rate(BoardView v)
{ return v.piano?ROLL_OFF:v.rack_live&7u; }
static inline unsigned board_recording(BoardView v)
{ return !v.piano && (v.rack_live&8u); }
unsigned cp_board_nearest_step(void);
extern uint8_t board_roll_mode;
extern uint8_t board_rhythm_only;
static inline int board_drums_only(void)
{
#ifdef COMPAS_HANAN
    if(board_hanan_mode==HANAN_SOLO) return 1;
#endif
    return board_rhythm_only;
}
extern uint16_t board_roll_keys;
void board_roll_trigger(void);
enum { GROOVE_COUNT_ENABLED=1u, GROOVE_COUNT_ACTIVE=2u };
static inline unsigned board_count_enabled(BoardView v)
{ return !v.piano && (v.groove_count&GROOVE_COUNT_ENABLED); }
static inline unsigned board_count_active(BoardView v)
{ return !v.piano && (v.groove_count&GROOVE_COUNT_ACTIVE); }
enum { SCORE_PERC_AVAILABLE=1u, SCORE_PERC_MUTED=2u };
static inline unsigned board_score_perc_on(BoardView v)
{ return v.score_percussion==SCORE_PERC_AVAILABLE; }
static inline unsigned board_has_clave(BoardView v) { return !v.piano && (v.study==2 || v.study==6); }
/* Stable bank IDs: source-ledger tests verify these paired tumbao grids. */
static inline unsigned board_rhythm_direction(unsigned rhythm)
{ return rhythm==9 || rhythm==12 ? 1u : rhythm==10 || rhythm==13 ? 0u : 2u; }
static inline unsigned board_clave_rhythm(unsigned rhythm,unsigned direction)
{ return rhythm==9 || rhythm==10 ? (direction?9u:10u) :
         rhythm==12 || rhythm==13 ? (direction?12u:13u) : BOARD_CUSTOM_RHYTHM; }
static inline unsigned board_clave_linked(BoardView v)
{ return board_has_clave(v) && board_rhythm_direction(v.rhythm)<2; }
static inline unsigned board_clave(BoardView v) { return v.clave&1u; }
static inline unsigned board_next_clave(BoardView v) { return v.clave>>1; }
static inline unsigned board_root(BoardView v) { return v.key&15u; }
static inline unsigned board_next_root(BoardView v) { return v.key>>4; }
static inline unsigned board_effective_bpm(BoardView v)
{ return (v.bpm*(v.speed?v.speed:100u)+50u)/100u; }
typedef struct {
    uint32_t callbacks, max_ticks, deadline_misses, late_halves, rail_samples;
    uint32_t choke_cuts, rack_peak, sequence_peak, total_peak;
    uint32_t irq_max_ticks;
} BoardStats;

typedef struct {
    uint16_t hits, cuts, manual;
    uint8_t sound, serial;
} RackFeedback;
RackFeedback board_rack_feedback(void);

extern int32_t board_dma[2][BOARD_WORDS];
extern BoardStats board_stats;
enum { BOARD_RHYTHM_BYTES=140, BOARD_RHYTHM_SLOT_BYTES=151, BOARD_RHYTHM_SLOTS_BYTES=SET_SLOTS*BOARD_RHYTHM_SLOT_BYTES };
unsigned board_rhythm_slots_export(uint8_t *bytes,unsigned size);
unsigned board_rhythm_slots_load(const uint8_t *bytes,unsigned size);
/* Foreground APIs. Import is atomic, stopped-only and requires matching meter. */
unsigned board_rhythm_export(uint8_t *bytes,unsigned size);
unsigned board_rhythm_import(const uint8_t *bytes,unsigned size);
const char *board_rhythm_title(unsigned index);
const char *board_groove_title(unsigned index);
unsigned board_rhythm_sound(unsigned index);
void board_init(void);
unsigned board_submit(BoardCommand cmd);
BoardView board_view(unsigned role);
void fm1_alnk0_irq(void);

/* Low-level port contract. The target binds these to the pinned BSP. */
uint32_t board_ticks(void);
uint32_t board_pending(void);
void board_ack_aux(uint32_t pending);
unsigned board_free_half(void);
void board_ack_half(void);
void board_disable_output(void);
uint32_t board_lock(void);
void board_unlock(uint32_t previous);

/* Internal sequencer-to-sampler seam, compiled only in the board build. */
int board_note727(unsigned study, unsigned role, unsigned event, unsigned velocity);
int32_t board_sample727(unsigned role);
void board_clear727(unsigned role);
#ifdef COMPAS_USB
int board_ota_idle(void);
#endif
int board_playing727(unsigned role);
int32_t board_rack_sample(void);
void board_mix_clip(void);
int board_song_boundary(unsigned completed_phrase);
void board_groove_bar(void);
void cp_board_section(unsigned section);
void cp_board_root(unsigned root);
void cp_board_clave(unsigned direction);
extern uint8_t board_piano_selected;
int board_piano_frame(int16_t *out);
const char *board_song_title(unsigned slot);
unsigned board_song_meta(unsigned slot,unsigned *study,unsigned *bpm,unsigned *reverse);
int board_song_exists(unsigned slot);
unsigned board_song_is_score(unsigned slot);
unsigned board_groove_section(unsigned step, unsigned steps, unsigned original);
void board_bass_note_on(void);
void cp_bass_preview(int8_t *out,unsigned acid,unsigned cut,unsigned bite);
void board_overlay_step(unsigned study, unsigned step);
void board_sequence_step(unsigned step);
void cp_board_clear_backing(void);
void cp_board_start_count(void);
unsigned cp_board_counting(void);
void cp_bass_set(unsigned style, unsigned cut, unsigned bite);
#ifdef COMPAS_EMU_STORE
enum { BOARD_SET_BYTES=SET_SLOTS*(19+CP_PROJECT_BYTES+CP_ROLES+5) };
unsigned board_store_load(const uint8_t *bytes);
unsigned board_store_snapshot(uint8_t *bytes);
unsigned board_store_pending(void);
unsigned board_store_pull(uint8_t *bytes);
void board_store_result(unsigned code);
unsigned piano_id_valid(unsigned id, unsigned guide);
#endif
#endif
