/* SPDX-License-Identifier: GPL-3.0-only
 * Compás 1.2: Sideman state report and MIDI clock output.
 *
 * Sideman: the browser guide at thebigbeat.app/sideman listens over WebMIDI.
 * The firmware sends one 19-byte SysEx state report whenever what the
 * player sees changes, at most ten times a second. The format is BB1 2.3's
 * (bb_learn_report), with firmware identity 2 = Compás:
 *
 *   F0 00 22 24 35 01 02 [page] [role] [lang] [knob] [bpm-hi] [bpm-lo]
 *      [study] [running] [part] [hanan] [kumbia-mode] F7
 *
 * Compás field map (the browser's compas.js reads these):
 *   page    PANEL_* (HOME, MIX, EDIT, RACK, SET, BASS, PERC, HANAN)
 *   role    the lane chosen on Mix/Edit (panel.role)
 *   lang    0 English, 1 Spanish: the guide follows the screen language
 *   knob    last knob or encoder turned: 0..3 = K1..K4, 4 SELECT, 5 ALGORITHM,
 *           6 PRESETS, 127 none yet
 *   bpm     the tempo the band is playing (REBAJADA included)
 *   study   the groove study of the current song
 *   running 1 while the band or the Kumbia kit plays
 *   part    PART_* now playing (6 = none)
 *   hanan   the Kumbia pattern 0..3 (0 when the kit is not built in)
 *   kumbia  HANAN_OFF / HANAN_LAYER / HANAN_SOLO
 *
 * MIDI clock: 24 clocks per quarter note while playing, with Start and Stop,
 * phase-locked to the band's own sixteenth-note position so it cannot drift
 * from the music. It runs in the foreground with USB; the audio interrupt is
 * not involved.
 *
 * Both are pure state machines (no hardware) so the host tests drive them. */
#ifndef COMPAS_SIDEMAN_H
#define COMPAS_SIDEMAN_H
#include <stdint.h>

enum { SIDEMAN_FW_COMPAS = 2, SIDEMAN_BYTES = 19, SIDEMAN_KNOB_NONE = 127 };
/* fm1_ticks() runs at 24 MHz */
enum { SIDEMAN_TICK_HZ = 24000000u, SIDEMAN_MIN_GAP = SIDEMAN_TICK_HZ / 10u };

typedef struct {
    uint8_t page, role, lang, knob;
    uint16_t bpm;
    uint8_t study, running, part, hanan, kumbia;
    uint8_t pad;                      /* explicit, so the byte compare sees no stray padding */
} SidemanState;

typedef struct {
    SidemanState sent;
    uint32_t last_tick;
    uint8_t primed, waiting;
} Sideman;

static inline unsigned sideman_encode(const SidemanState *s, uint8_t out[SIDEMAN_BYTES])
{
    out[0] = 0xF0u;
    out[1] = 0x00u; out[2] = 0x22u; out[3] = 0x24u; out[4] = 0x35u;
    out[5] = 0x01u;
    out[6] = SIDEMAN_FW_COMPAS;
    out[7] = s->page & 0x7Fu;
    out[8] = s->role & 0x7Fu;
    out[9] = s->lang & 0x7Fu;
    out[10] = s->knob & 0x7Fu;
    out[11] = (uint8_t)((s->bpm >> 7) & 0x7Fu);
    out[12] = (uint8_t)(s->bpm & 0x7Fu);
    out[13] = s->study & 0x7Fu;
    out[14] = s->running ? 1u : 0u;
    out[15] = s->part & 0x7Fu;
    out[16] = s->hanan & 0x7Fu;
    out[17] = s->kumbia & 0x7Fu;
    out[18] = 0xF7u;
    return SIDEMAN_BYTES;
}

/* Returns 1 and fills out[] when a report is due: the state changed and at
 * least 100 ms have passed since the last one. A change inside the 100 ms
 * window is not lost: it is sent when the window closes. */
static inline int sideman_due(Sideman *m, const SidemanState *now, uint32_t tick,
                              uint8_t out[SIDEMAN_BYTES])
{
    const uint8_t *a = (const uint8_t *)&m->sent, *b = (const uint8_t *)now;
    int changed = !m->primed;
    for (unsigned i = 0; i < sizeof *now; ++i)   /* no libc: fm1.c has its own mem* */
        changed |= a[i] != b[i];
    if (!changed) {
        m->waiting = 0;
        return 0;
    }
    if (m->primed && tick - m->last_tick < SIDEMAN_MIN_GAP) {
        m->waiting = 1;
        return 0;
    }
    sideman_encode(now, out);
    return 1;
}

/* Call after the report has been queued for USB. */
static inline void sideman_sent(Sideman *m, const SidemanState *now, uint32_t tick)
{
    m->sent = *now;
    m->last_tick = tick;
    m->primed = 1;
    m->waiting = 0;
}

/* ---- MIDI clock ---- */
enum { CLOCK_PER_STEP = 6, CLOCK_START = 0xFA, CLOCK_STOP = 0xFC, CLOCK_TICK = 0xF8 };
/* A clock is due every 60 s / (bpm * 24). In fm1 ticks: 60 * 24e6 / (bpm * 24)
 * = 60e6 / bpm. The accumulator adds elapsed ticks * bpm and emits one clock
 * per 60e6, which never overflows 32 bits for gaps under a second. */
enum { CLOCK_UNIT = 60000000u };

typedef struct {
    uint32_t acc, last_tick, sent, steps;
    uint8_t playing, last_position, started;
} MidiClock;

typedef struct {
    uint8_t start, stop, ticks;   /* send Start / Stop first, then this many clocks */
} ClockOut;

/* playing: band running; position: BoardView.position (sixteenth step, wraps);
 * bpm: effective tempo. Returns what to send now. */
static inline ClockOut midiclock_step(MidiClock *c, unsigned playing, unsigned position,
                                      unsigned bpm, uint32_t tick)
{
    ClockOut out = {0, 0, 0};
    uint32_t elapsed = tick - c->last_tick;
    c->last_tick = tick;
    if (!playing) {
        if (c->playing)
            out.stop = 1;
        c->playing = 0;
        c->started = 0;
        return out;
    }
    if (!c->playing) {                          /* first look at a running band */
        c->playing = 1;
        c->started = 0;
        c->last_position = (uint8_t)position;
        return out;
    }
    if (!c->started) {                          /* start on the first sixteenth boundary seen */
        if ((uint8_t)position == c->last_position)
            return out;
        c->started = 1;
        c->acc = 0;
        c->sent = 0;
        c->steps = 0;
        c->last_position = (uint8_t)position;
        out.start = 1;
        out.ticks = 1;                          /* the downbeat clock goes with Start */
        c->sent = 1;
        return out;
    }
    if (elapsed > SIDEMAN_TICK_HZ / 4u)
        elapsed = SIDEMAN_TICK_HZ / 4u;         /* a stalled loop must not burst */
    if (bpm < 20u)
        bpm = 20u;
    if ((uint8_t)position != c->last_position) {
        uint8_t moved = (uint8_t)((uint8_t)position - c->last_position);
        c->last_position = (uint8_t)position;
        if (moved > 8u)
            moved = 1u;                         /* a jump (cue, wrap reset) counts as one step */
        c->steps += moved;
        {   /* lock: by now the band has played steps*6 clocks, plus the first */
            uint32_t want = c->steps * CLOCK_PER_STEP + 1u;
            if (c->sent < want) {
                uint32_t behind = want - c->sent;
                if (behind > 3u) behind = 3u;   /* catch up gently */
                out.ticks = (uint8_t)behind;
                c->sent += behind;
                c->acc = 0;
                return out;
            }
            if (c->sent > want + 1u) {          /* ahead: wait for the band */
                c->acc = 0;
                return out;
            }
            c->acc = 0;                         /* realign the in-between clocks to the step */
            return out;
        }
    }
    c->acc += elapsed * bpm;
    while (c->acc >= CLOCK_UNIT && out.ticks < 2u) {
        uint32_t want_max = (c->steps + 1u) * CLOCK_PER_STEP;
        c->acc -= CLOCK_UNIT;
        if (c->sent >= want_max)               /* never run past the next step */
            break;
        out.ticks++;
        c->sent++;
    }
    return out;
}

#endif
