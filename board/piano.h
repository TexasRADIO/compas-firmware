#ifndef COMPAS_PIANO_H
#define COMPAS_PIANO_H
#include <stdint.h>

enum { PIANO_INSIDE, PIANO_BAR, PIANO_END };
enum { PIANO_FORM_COUNT_IN, PIANO_FORM_INTRO, PIANO_FORM_VERSE, PIANO_FORM_REFRAIN, PIANO_FORM_CODA };
/* Saved score IDs follow the append-only repertoire/board-scores.json registry. */
enum { PIANO_SCORE_CIELITO=7, PIANO_SCORE_CUCARACHA=8, PIANO_SCORE_TEODORA=20, PIANO_SCORE_JURAME=23, PIANO_SCORE_JURAME_SONG=24 };
void piano_start(unsigned id,unsigned count_in);
/* Twelve nearest keys: -6 through +5 semitones; zero retains printed pitch. */
void piano_transpose(int semitones);
unsigned piano_step(void);
int32_t piano_mix(unsigned bpm,unsigned guide);
void piano_jump(unsigned bar);
enum { PIANO_MARACA=1, PIANO_MARACA_ACCENT, PIANO_CONGA_MUTE, PIANO_CONGA_OPEN };
unsigned piano_percussion(void);
unsigned piano_bar(void);
unsigned piano_position(void);
unsigned piano_form(void);
unsigned piano_coda(void);
unsigned piano_part_bar(unsigned form);
unsigned piano_beats(void);
unsigned piano_has_guide(void);
unsigned piano_has_percussion(void);
unsigned piano_can_hold(void);
#endif
