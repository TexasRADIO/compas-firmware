#ifndef COMPAS_ENGINE_H
#define COMPAS_ENGINE_H
#include <stdint.h>

enum { CP_ROLES=6, CP_MAX_BLOCK=256, CP_PROJECT_BYTES=55 };
enum { CP_START=1, CP_STOP, CP_STUDY, CP_SECTION, CP_FILL, CP_MUTE, CP_SOLO,
       CP_LEVEL, CP_BPM, CP_FEEL, CP_STEP, CP_CLAVE, CP_HIT, CP_ROOT };
enum { CP_OK=0, CP_BUSY, CP_STOP_FIRST, CP_NO_ROLE, CP_RANGE, CP_BAD_PROJECT, CP_STOPPED };
enum { CP_PLAYING=0, CP_ACTIVE, CP_PENDING, CP_FILL_STATE, CP_POSITION,
       CP_STUDY_ID, CP_TEMPO, CP_FEEL_VALUE, CP_MUTES, CP_SOLO_ROLE, CP_REVERSE,
       CP_AUDIBLE, CP_SAMPLE_RATE, CP_PHRASE, CP_ROOT_NOTE, CP_STEP_COUNT };

uint32_t cp_init(uint32_t sample_rate);
uint32_t cp_command(uint32_t cmd, uint32_t a, uint32_t b);
uint32_t cp_status(uint32_t key);
uint32_t cp_step(uint32_t role, uint32_t step);
uint32_t cp_level(uint32_t role);
uint32_t cp_snapshot(void);
uint32_t cp_restore(uint32_t size);
uint32_t cp_render(uint32_t count);
int16_t *cp_pcm(void);
uint8_t *cp_blob(void);
uint32_t cp_state_bytes(void);
#endif
