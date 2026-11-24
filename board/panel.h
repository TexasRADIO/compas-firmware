#ifndef COMPAS_PANEL_H
#define COMPAS_PANEL_H
#include "board.h"
enum { PANEL_HOME, PANEL_MIX, PANEL_EDIT, PANEL_RACK, PANEL_SET, PANEL_BASS, PANEL_PERC, PANEL_HANAN, PANEL_FX };
enum { PANEL_PLAY_BUTTON=12 };
enum { FACE_LINES=8, FACE_COLS=20 };
enum { PANEL_INTRO_C4=7, PANEL_VERSE_D4=9, PANEL_VARIATION_E4=11, PANEL_CHORUS_F4=12,
       PANEL_BREAK_G4=14, PANEL_END_A4=16, PANEL_PAD_B4=18, PANEL_CONGA_CS4=8 };
typedef struct {
    uint8_t page, role, bar, rack_sound, locked, slot;
    uint16_t master_pot;
    uint32_t held, part_at;
    uint8_t last_part, stop_epoch, rack_edit;
} Panel;
typedef struct {
    uint32_t parts, upper, pads, active, queued, enabled;
} PanelKeys;
extern const uint8_t panel_part_keys[PART_NONE], panel_layer_keys[5];
PanelKeys panel_keys(const Panel *panel, BoardView view);
unsigned panel_key_number(unsigned key, PanelKeys map);
void panel_input(Panel *panel, uint32_t buttons, uint32_t keys, const int32_t enc[7]);
void panel_scan(Panel *panel, uint32_t buttons, uint32_t keys, const int32_t enc[7], uint32_t now);
void panel_remote(Panel *panel, unsigned start);
void panel_master(Panel *panel, int32_t raw);
unsigned panel_beats(unsigned study);
unsigned panel_beat_position(unsigned study,unsigned step);
/* The 727 keeps its historic 16/32-step grid; compound role editing is separate. */
unsigned panel_rack_edit_supported(unsigned study);
unsigned panel_has_bass(BoardView view);
const char *panel_title(BoardView view);
const char *panel_root_name(unsigned root);
const char *panel_role_name(unsigned study,unsigned role);
const char *panel_groove_part_name(unsigned study,unsigned part,unsigned abbreviated);
const char *panel_score_part_name(unsigned score, unsigned part);
/* GLO metadata from the study registry. Flags: 1 = source-backed body,
 * 2 = written in cut time (counted in two). */
enum { STUDY_SOURCED=1u, STUDY_CUT_TIME=2u };
const char *panel_study_family(unsigned study);
unsigned panel_study_flags(unsigned study);
void panel_face(const Panel *panel, BoardView view, char lines[FACE_LINES][FACE_COLS+1]);
/* Screen language: 0 English, 1 Spanish (KNOB3 on GLO). panel_word() returns the
 * word in the current language, or its argument when the table has no entry. */
extern uint8_t panel_lang;
const char *panel_word(const char *english);
#endif
