/* SPDX-License-Identifier: GPL-3.0-only
 * Compás 1.2 effects: SALA (room), ECO (tempo-synced echo), PEGA (glue).
 *
 * Included by board.c; runs in the audio interrupt after the master gain.
 *
 *   mono dry ──┬───────────────────────────────┬─► PEGA ─► L/R
 *              ├─► SALA (8-line FDN) ─► stereo ┤
 *              └─► ECO  (one head, tempo) ─────┘
 *
 * SALA is BB1 2.3's ROOM (bb_fxbus.c, GPL-3.0-only, adapted from the OG Synth
 * X0X fxbus, (C) Charles Vestal): block-float 8-line FDN with a 4-stage
 * diffuser. Compás runs it at half rate (22,050 Hz) with half-length lines,
 * so the room's times are BB1's at half the interrupt cost; its tone filter
 * and loop gain are rescaled to match. ECO is a single-head echo locked to
 * the band's tempo in sixteenth-note divisions, with BB1's tone-filtered
 * feedback and TAPE saturation, recorded at half rate so a 1/2 fits at the
 * slowest tempo the FM-1 can play (60 BPM at REBAJADA 60 %: 3.33 s). PEGA is
 * BB1's one-knob glue compressor, moved onto the full mix.
 *
 * Nothing large is cleared in the interrupt. A re-armed ECHO only plays back
 * what it has recorded since it was armed; a re-armed ROOM is wiped 1 KiB per
 * audio block before it sounds (about 25 ms).
 *
 * With every amount at 0 (the default) the whole block is skipped and the
 * output is bit-identical to Compás 1.1. The delay lines live in .pool on
 * the board (the FM-1's 360 KiB pool region, otherwise unused by Compás). */
#include <stdint.h>

enum { FX_RATE = 44100 };   /* FX_* parameters and BOARD_FX live in board.h */
/* echo divisions in sixteenths: 1/16, 1/8, 3/16, 1/4, 3/8, 1/2 */
static const uint8_t fx_sixteenths[FX_DIVISIONS] = {1, 2, 3, 4, 6, 8};

#define RM_N 8
#define RM_BLK 16
#define RM_NB(l) ((l) / RM_BLK + 1)
/* half of BB1's lengths (prime), at half rate: the same times in seconds */
#define RM_BLOCKS (RM_NB(563)+RM_NB(733)+RM_NB(877)+RM_NB(787)+RM_NB(643)+RM_NB(1039)+RM_NB(1123)+RM_NB(953))
#define RM_DIF_TOTAL (107+79+199+149)
enum { FX_HALF = FX_RATE / 2 };
/* echo ring at half rate: 3.4 s, so a 1/2 at 36 BPM (60 BPM at REBAJADA 60 %) fits */
enum { ECO_RING = 75000 };

typedef struct {                  /* everything a re-armed ROOM forgets */
    float stage[RM_N][RM_BLK], read[RM_N][RM_BLK];
    float lp[RM_N], y, x_half;
    uint16_t wblk[RM_N];
    int16_t dif_pos[4];
    int16_t dif[RM_DIF_TOTAL];
    int8_t exp[RM_BLOCKS];
    int8_t man[RM_BLOCKS * RM_BLK];
    float yr;
    uint8_t pos, phase;
} FxRoomState;

typedef struct {
    FxRoomState s;
    float c[RM_N], a[RM_N];
    float out_gain;
    uint16_t base[RM_N], nblk[RM_N];
    uint32_t wipe;                /* bytes of s still to clear; the room is silent until 0 */
} FxRoom;

typedef struct {
    int16_t ring[ECO_RING];
    float target, cur, fdbk, level, lp, hp_z1, x_half, rec_half;
    int32_t w, fill;              /* fill: half-samples recorded since the echo was armed */
    uint8_t phase;
} FxEcho;

typedef struct {
    FxRoom room;
    FxEcho echo;
    float det, env, thr, slope, makeup;
    uint8_t param[FX_PARAMS];
    uint16_t bpm;
    uint8_t active;
} Fx;

#ifdef __PI32V2__
static Fx fx __attribute__((section(".pool")));
#else
static Fx fx;
#endif

static inline float fx_abs(float x) { return x < 0 ? -x : x; }

static float fx_tanh(float x)
{
    if (fx_abs(x) >= 4.0f) return x < 0 ? -1.0f : 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static float fx_log2(float x)
{
    union { float f; uint32_t u; } v = { .f = x };
    float e = (float)(int32_t)((v.u >> 23) & 0xFFu) - 127.0f;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;           /* mantissa in [1, 2) */
    return e + (-0.34484843f * v.f + 2.02466578f) * v.f - 0.67487759f;
}

static float fx_exp2(float x)
{
    if (x < -30.0f) return 0.0f;
    int32_t i = (int32_t)(x < 0 ? x - 1.0f : x);
    float f = x - (float)i;
    union { float f; uint32_t u; } v;
    v.u = (uint32_t)(i + 127) << 23;
    return v.f * (1.0f + f * (0.6931472f + f * (0.2402265f + f * 0.0555041f)));
}

static void fx_clear(void *p, unsigned long n)
{
    uint8_t *b = (uint8_t *)p;
    for (unsigned long i = 0; i < n; ++i) b[i] = 0;
}

static float fx_pow2(int e)                   /* 2^e for -126 <= e <= 127 */
{
    union { float f; uint32_t u; } v = { .u = (uint32_t)(e + 127) << 23 };
    return v.f;
}

/* Output knee for the effect path only: linear to -1 dBFS, then a smooth
 * curve that never reaches full scale, so a wet mix on a hot band does not
 * hit the codec rail. The dry path (effects off) never comes through here. */
static float fx_soft(float v)
{
    const float knee = 0.89f, room = 1.0f - knee;
    float a = fx_abs(v);
    if (a <= knee) return v;
    float shaped = fx_tanh((a - knee) * (1.0f / room));
    /* The rational tanh approximation slightly exceeds 1 just below its
     * saturation cutoff. Bound this return curve, not the shared echo drive. */
    if (shaped > 1.0f) shaped = 1.0f;
    a = knee + room * shaped * 0.995f;
    return v < 0 ? -a : a;
}

/* ---- SALA: BB1 2.3 ROOM ---- */
static const uint16_t rm_lengths[RM_N] = {563, 733, 877, 787, 643, 1039, 1123, 953};

static void rm_encode(FxRoom *r, int line)
{
    uint16_t blk = r->s.wblk[line], base = r->base[line];
    const float *src = r->s.stage[line];
    float peak = 0;
    for (int i = 0; i < RM_BLK; i++) {
        float a = fx_abs(src[i]);
        if (a > peak) peak = a;
    }
    /* Block floating point: e is the smallest power of two at or above the
     * block peak, and the mantissas are src * 127 / 2^e. (BB1 2.3's version
     * scaled by 127/2^e twice over, so every mantissa clipped to +-127 and
     * the room came back about 40 dB down and square-edged.) */
    int e = -60;
    if (peak > 0) {
        union { float f; uint32_t u; } v = { .f = peak };
        e = (int)((v.u >> 23) & 0xFF) - 126;          /* peak < 2^e */
        if (e < -60) e = -60;
        if (e > 60) e = 60;
    }
    const float fwd = 127.0f * fx_pow2(-e);
    r->s.exp[base + blk] = (int8_t)e;
    int8_t *dst = &r->s.man[(base + blk) * RM_BLK];
    for (int i = 0; i < RM_BLK; i++) {
        int32_t q = (int32_t)(src[i] * fwd);
        if (q > 127) q = 127;
        if (q < -127) q = -127;
        dst[i] = (int8_t)q;
    }
    r->s.wblk[line] = blk + 1 >= r->nblk[line] ? 0 : blk + 1;
}

static void rm_decode(FxRoom *r, int line)
{
    uint16_t blk = r->s.wblk[line], base = r->base[line];
    const float scale = fx_pow2(r->s.exp[base + blk]) * (1.0f / 127.0f);
    const int8_t *src = &r->s.man[(base + blk) * RM_BLK];
    for (int i = 0; i < RM_BLK; i++)
        r->s.read[line][i] = (float)src[i] * scale;
}

static float rm_diffuse(FxRoom *r, float x)
{
    static const uint16_t dlen[4] = {107, 79, 199, 149};
    static const float dg[4] = {0.7f, 0.7f, 0.65f, 0.65f};
    uint16_t off = 0;
    for (int d = 0; d < 4; d++) {
        int16_t pos = r->s.dif_pos[d];
        float rd = (float)r->s.dif[off + pos] * (1.0f / 2048.0f);
        float y = rd - dg[d] * x;
        int32_t wr = (int32_t)((x + dg[d] * y) * 2048.0f);
        if (wr > 32767) wr = 32767;
        if (wr < -32767) wr = -32767;
        r->s.dif[off + pos] = (int16_t)wr;
        r->s.dif_pos[d] = pos + 1 >= (int16_t)dlen[d] ? 0 : pos + 1;
        x = y;
        off += dlen[d];
    }
    return x;
}

static void rm_hadamard(float *x)
{
    for (int s = 1; s < 8; s <<= 1)
        for (int i = 0; i < 8; i += s << 1)
            for (int j = i; j < i + s; j++) {
                float a = x[j], b = x[j + s];
                x[j] = a + b;
                x[j + s] = a - b;
            }
    for (int i = 0; i < 8; i++) x[i] *= 0.35355339f;
}

/* ---- the block ---- */
static void fx_tempo(unsigned bpm)
{
    if (bpm < 20u) bpm = 20u;
    /* in half-rate samples. 36..180 BPM always fits (tests/board-fx-unit.c);
     * the clamp only guards a tempo outside the FM-1's range. */
    float samples = (float)FX_HALF * 15.0f / (float)bpm * (float)fx_sixteenths[fx.param[FX_TIEMPO]];
    if (samples > (float)(ECO_RING - 8)) samples = (float)(ECO_RING - 8);
    fx.echo.target = samples;
    fx.bpm = (uint16_t)bpm;
}

static void fx_apply(void)
{
    FxRoom *r = &fx.room;
    /* BB1's tone pole at 44.1 kHz is tone; at half rate the same filter is tone^2.
     * The loop gain c / (1 - a) stays decay, so the decay time is unchanged. */
    const float decay = 0.1f + 0.85f * 0.55f, tone = 0.3f + 0.65f * 0.5f, a = tone * tone;
    for (int l = 0; l < RM_N; l++) {
        r->a[l] = a;
        r->c[l] = decay * (1.0f - a);
    }
    r->out_gain = (float)fx.param[FX_SALA] * 0.006f;
    fx.echo.level = (float)fx.param[FX_ECO] * 0.007f;
    fx.echo.fdbk = (float)fx.param[FX_REPITE] * 0.01f;
    {   /* PEGA: one-knob glue */
        float a = (float)fx.param[FX_PEGA] * 0.01f, ratio = 2.0f + 3.0f * a;
        fx.thr = -6.0f - 18.0f * a;
        fx.slope = a > 0 ? 1.0f - 1.0f / ratio : 0.0f;
        fx.makeup = a * 2.2f + a * a * 6.6f;
    }
    fx.active = fx.param[FX_SALA] || fx.param[FX_ECO] || fx.param[FX_PEGA];
}

static void fx_init(void)
{
    fx_clear(&fx, sizeof fx);
    uint16_t base = 0;
    for (int l = 0; l < RM_N; l++) {
        fx.room.base[l] = base;
        fx.room.nblk[l] = RM_NB(rm_lengths[l]);
        base += fx.room.nblk[l];
    }
    fx.param[FX_TIEMPO] = 2;      /* 3/16: the dotted eighth */
    fx.param[FX_REPITE] = 35;
    fx_tempo(120);
    fx.echo.cur = fx.echo.target;
    fx_apply();
}

static int fx_set(unsigned param, unsigned value)
{
    static const uint8_t top[FX_PARAMS] = {100, 100, FX_DIVISIONS - 1, 100, 90};
    if (param >= FX_PARAMS || value > top[param]) return 0;
    /* waking a line: forget what it held when it was last on, without clearing
     * kilobytes here (this runs in the audio interrupt). */
    if (param == FX_SALA && value && !fx.param[FX_SALA])
        fx.room.wipe = sizeof fx.room.s;
    /* ECHO is a return level: at 0 nothing of the echo is heard, so its
     * feedback ends there too. Without this, with ROOM and GLUE also at 0
     * the block stops running and the echo's state froze, to be replayed
     * stale when ECHO came back (Codex round 2). O(1): the ring is not
     * touched; the next arming only reads what it records from then on. */
    if (param == FX_ECO && !value)
        fx.echo.lp = 0.0f;
    if (param == FX_ECO && value && !fx.param[FX_ECO]) {
        fx.echo.fill = 0;                         /* and the send filter starts clean */
        fx.echo.hp_z1 = fx.echo.rec_half = 0.0f;
    }
    fx.param[param] = (uint8_t)value;
    if (param == FX_TIEMPO) fx_tempo(fx.bpm);
    fx_apply();
    return 1;
}

/* CPU guard (Compás 1.2, after BB1 2.3's). Just before the effects run,
 * look at how much of this block's audio deadline is already spent. A missed
 * deadline silences Compás until it restarts, so if the effects might not fit,
 * skip the costly stages for this one block instead: ROOM first (the 8-line
 * reverb, the largest cost), then ECHO. GLUE and the output clip always run.
 * Measured peak with every effect at maximum is about 51 %, so on a healthy
 * unit this never acts; board_stats.fx_shed_blocks counts blocks where it did. */
enum { FX_SHED_SALA = 1u, FX_SHED_ECO = 2u };
#ifndef FX_GUARD_SALA_PCT   /* test builds may lower these to make the guard act */
#define FX_GUARD_SALA_PCT 55u
#endif
#ifndef FX_GUARD_ECO_PCT
#define FX_GUARD_ECO_PCT 65u
#endif
static uint8_t fx_shed;
static unsigned fx_guard(uint32_t spent, uint32_t budget)
{
    unsigned shed = 0;
    if (spent > budget / 100u * FX_GUARD_SALA_PCT) shed |= FX_SHED_SALA;
    if (spent > budget / 100u * FX_GUARD_ECO_PCT) shed |= FX_SHED_ECO;
    return shed;
}

/* in: mono samples after master gain, int16 range; out: interleaved L/R
 * in the same range (unclipped). bpm: the tempo the listener hears. */
static void fx_process(const int32_t *in, int32_t *out, unsigned frames, unsigned bpm)
{
    FxRoom *r = &fx.room;
    FxEcho *e = &fx.echo;
    if (bpm != fx.bpm) fx_tempo(bpm);
    if (r->wipe) {                                /* re-armed ROOM: 1 KiB per block */
        uint32_t n = r->wipe > 1024u ? 1024u : r->wipe;
        r->wipe -= n;
        fx_clear((uint8_t *)&r->s + r->wipe, n);
    }
    const int room_on = r->out_gain > 0.0f && !r->wipe && !(fx_shed & FX_SHED_SALA);
    for (unsigned i = 0; i < frames; ++i) {
        const float x = (float)in[i] * (1.0f / 32768.0f);
        float l = x, rt = x;
        if (room_on) {                            /* half rate: one room sample per two */
            FxRoomState *st = &r->s;
            if (!st->phase) {
                st->x_half = x;
                st->phase = 1;
                l += st->y * r->out_gain;         /* second half of the last step */
                rt += st->yr * r->out_gain;
            } else {
                const float xin = 0.5f * (st->x_half + x);
                uint8_t pos = st->pos;
                st->phase = 0;
                if (pos == 0)
                    for (int k = 0; k < RM_N; k++) { rm_encode(r, k); rm_decode(r, k); }
                float d = rm_diffuse(r, xin * 0.5f), lanes[RM_N];
                for (int k = 0; k < RM_N; k++) {
                    st->lp[k] = r->a[k] * st->lp[k] + st->read[k][pos] * r->c[k];
                    lanes[k] = st->lp[k];
                }
                rm_hadamard(lanes);
                float lo = 0, ro = 0;
                for (int k = 0; k < RM_N; k++) st->stage[k][pos] = lanes[k] + d;
                for (int k = 0; k < RM_N; k += 2) { lo += st->read[k][pos]; ro += st->read[k + 1][pos]; }
                st->pos = (uint8_t)((pos + 1) & (RM_BLK - 1));
                /* linear in-between: this sample is halfway from the last step */
                l += 0.5f * (st->y + lo) * r->out_gain;
                rt += 0.5f * (st->yr + ro) * r->out_gain;
                st->y = lo;
                st->yr = ro;
            }
        }
        if (!(fx_shed & FX_SHED_ECO) && (e->level > 0.0f || e->lp != 0.0f)) {
            float hp = x - e->hp_z1;
            e->hp_z1 += hp * 0.0113f;                 /* ~80 Hz high-pass on the send */
            {   /* glide on tempo change. Near the end the proportional step falls
                 * below float resolution at a 3 s delay, so finish at a fixed
                 * 1/64 half-sample per sample (at most ~500 samples). */
                float gap = e->target - e->cur;
                if (fx_abs(gap) > 8.0f) e->cur += gap * 0.0008f;
                else if (fx_abs(gap) > 0.016f) e->cur += gap > 0.0f ? 0.015625f : -0.015625f;
                else e->cur = e->target;
            }
            /* read cur half-samples back from now; nothing older than the arming */
            float rp = (float)e->w + (e->phase ? 0.5f : 0.0f) - e->cur;
            float tap = 0.0f;
            if ((float)e->fill >= e->cur + 2.0f) {
                while (rp < 0.0f) rp += (float)ECO_RING;
                int32_t i0 = (int32_t)rp, i1;
                float fr = rp - (float)i0;
                if (i0 >= ECO_RING) i0 -= ECO_RING;
                i1 = i0 + 1 >= ECO_RING ? 0 : i0 + 1;
                tap = ((float)e->ring[i0] + ((float)e->ring[i1] - (float)e->ring[i0]) * fr) * (1.0f / 16384.0f);
            }
            e->lp += (tap * e->fdbk - e->lp) * 0.36f;  /* darker each repeat */
            if (fx_abs(e->lp) < 3e-6f) e->lp = 0.0f;    /* a tenth of one 16-bit step: the tail is over */
            float rec = fx_tanh((hp * (e->level > 0.0f) + e->lp) * 1.3f) * (1.0f / 1.3f);
            if (!e->phase) {
                e->rec_half = rec;
                e->phase = 1;
            } else {                                  /* record the pair's average */
                int32_t k = (int32_t)(0.5f * (e->rec_half + rec) * 16384.0f);
                if (k > 32767) k = 32767;
                if (k < -32767) k = -32767;
                e->ring[e->w] = (int16_t)k;
                e->w = e->w + 1 >= ECO_RING ? 0 : e->w + 1;
                if (e->fill < ECO_RING) e->fill++;
                e->phase = 0;
            }
            l += tap * e->level * 0.85f;
            rt += tap * e->level;
        }
        if (fx.slope > 0.0f) {
            float mag = fx_abs(l) > fx_abs(rt) ? fx_abs(l) : fx_abs(rt);
            fx.det = mag > fx.det ? mag : fx.det * 0.9999f;
            float over = (fx.det > 1e-6f ? 6.0206f * fx_log2(fx.det) : -120.0f) - fx.thr, gr = 0.0f;
            if (over >= 3.0f) gr = -over * fx.slope;
            else if (over > -3.0f) { float t = over + 3.0f; gr = -(t * t) / 12.0f * fx.slope; }
            fx.env = gr + (fx.env - gr) * (gr < fx.env ? 0.01f : 0.9995f);
            float g = fx_exp2((fx.env + fx.makeup) * (1.0f / 6.0206f));
            l *= g;
            rt *= g;
        }
        out[2 * i] = (int32_t)(fx_soft(l) * 32768.0f);
        out[2 * i + 1] = (int32_t)(fx_soft(rt) * 32768.0f);
    }

}
