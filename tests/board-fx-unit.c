/* Host test for board/fx.c: echo TIME is the division shown at every tempo the
 * FM-1 can play, re-arming never replays old audio, nothing blows up at max. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "board.h"
#include "fx.c"

enum { BLOCK = 128 };
static int32_t in[BLOCK], out[2 * BLOCK];

static void run_silence(unsigned blocks, unsigned bpm)
{
    for (unsigned i = 0; i < BLOCK; ++i) in[i] = 0;
    while (blocks--) fx_process(in, out, BLOCK, bpm);
}

static void set_all(unsigned room, unsigned echo, unsigned time, unsigned glue, unsigned repeat)
{
    assert(fx_set(FX_SALA, room) && fx_set(FX_ECO, echo) && fx_set(FX_TIEMPO, time) &&
           fx_set(FX_PEGA, glue) && fx_set(FX_REPITE, repeat));
}

/* First echo of an impulse, in output samples after the impulse. */
static long first_echo(unsigned bpm, unsigned division)
{
    fx_init();
    set_all(0, 100, division, 0, 0);
    run_silence(1200, bpm);                     /* glide settles; the ring fills */
    long best_at = -1; float best = 0;
    long t = 0;
    const long span = (long)(FX_RATE * 15.0 / bpm * fx_sixteenths[division]) + 2000;
    for (long b = 0; t < span; ++b) {
        for (unsigned i = 0; i < BLOCK; ++i) in[i] = (b == 0 && i == 0) ? 20000 : 0;
        fx_process(in, out, BLOCK, bpm);
        for (unsigned i = 0; i < BLOCK; ++i, ++t) {
            if (t < 64) continue;               /* skip the dry impulse itself */
            float v = fabsf((float)out[2 * i + 1]);
            if (v > best) { best = v; best_at = t; }
        }
    }
    return best_at;
}

int main(void)
{
    /* A bounded output knee must also cover the tanh approximation's small
     * overshoot near its cutoff (native dense load reached this interval). */
    float previous = 0.0f;
    for (unsigned i = 0; i <= 300000; ++i) {
        const float x = (float)i / 100000.0f;
        const float y = fx_soft(x);
        assert(y >= 0.0f && y <= 0.9995f);
        assert(y + 0.000001f >= previous);
        assert(fabsf(y + fx_soft(-x)) <= 0.000001f);
        previous = y;
    }
    /* 1. TIME: 36 BPM is 60 BPM at REBAJADA 60 %; 180 is the top of TEMPO. */
    static const unsigned tempos[] = {36, 45, 54, 60, 72, 90, 96, 108, 120, 144, 180};
    for (unsigned ti = 0; ti < sizeof tempos / sizeof *tempos; ++ti)
        for (unsigned d = 0; d < FX_DIVISIONS; ++d) {
            const double want = FX_RATE * 15.0 / tempos[ti] * fx_sixteenths[d];
            long got = first_echo(tempos[ti], d);
            if (fabs((double)got - want) > 4.0) {
                printf("TIME wrong: %u BPM division %u: echo at %ld, want %.1f\n", tempos[ti], d, got, want);
                return 1;
            }
        }

    /* 2. ECHO re-armed after its tail died replays nothing it heard before. */
    fx_init();
    set_all(0, 100, 5, 0, 60);
    for (unsigned b = 0; b < 400; ++b) {        /* loud material into the ring */
        for (unsigned i = 0; i < BLOCK; ++i) in[i] = (int32_t)((i * 7919u + b * 131u) % 20000u) - 10000;
        fx_process(in, out, BLOCK, 120);
    }
    assert(fx_set(FX_ECO, 0));
    run_silence(8000, 120);                     /* ECHO 0 ends the feedback at once; keep running anyway */
    assert(fx.echo.lp == 0.0f);
    assert(fx_set(FX_ECO, 100));
    for (unsigned b = 0; b < 1500; ++b) {       /* 4.3 s of silence after re-arming */
        run_silence(1, 120);
        for (unsigned i = 0; i < 2 * BLOCK; ++i) assert(out[i] == 0);
    }

    /* 3. ROOM re-armed: silent while it wipes, then nothing old comes back. */
    fx_init();
    set_all(100, 0, 2, 0, 35);
    for (unsigned b = 0; b < 400; ++b) {
        for (unsigned i = 0; i < BLOCK; ++i) in[i] = (int32_t)((i * 104729u + b) % 16000u) - 8000;
        fx_process(in, out, BLOCK, 120);
    }
    assert(fx_set(FX_SALA, 0));
    run_silence(10, 120);
    assert(fx_set(FX_SALA, 100));
    assert(fx.room.wipe == sizeof fx.room.s);
    for (unsigned b = 0; b < 2000; ++b) {
        run_silence(1, 120);
        for (unsigned i = 0; i < 2 * BLOCK; ++i) assert(out[i] == 0);
    }
    assert(fx.room.wipe == 0);

    /* 4. ROOM is a room: an impulse at ROOM 100 comes back within 20 dB of the
     * dry level and dies away in 150-600 ms. (The block encoder once clipped
     * every mantissa, leaving a 55 ms burst 40 dB down.) */
    {
        fx_init();
        assert(fx_set(FX_SALA, 100));
        static double env[400];
        for (unsigned b = 0; b < 400; ++b) {
            for (unsigned i = 0; i < BLOCK; ++i) in[i] = (b == 10 && i == 0) ? 20000 : 0;
            fx_process(in, out, BLOCK, 120);
            env[b] = 0;
            for (unsigned i = 0; i < BLOCK; ++i) { double w = out[2 * i] - in[i]; env[b] += w * w; }
        }
        double peak = 0, total = 0; unsigned at = 0, end = 0;
        for (unsigned b = 0; b < 400; ++b) { total += env[b]; if (env[b] > peak) { peak = env[b]; at = b; } }
        for (unsigned b = at; b < 400 && !end; ++b) if (env[b] < peak * 1e-6) end = b;
        const double rt60 = (end - at) * BLOCK * 1000.0 / FX_RATE, level = 10 * log10(total / (20000.0 * 20000.0));
        if (!end || rt60 < 150 || rt60 > 600 || level < -20 || level > 6) {
            printf("ROOM wrong: RT60 %.0f ms, energy %.1f dB re the impulse\n", rt60, level);
            return 1;
        }
        printf("room: RT60 %.0f ms, energy %.1f dB re the impulse\n", rt60, level);
    }

    /* 5. Everything at maximum for 60 s of loud input: finite and bounded. */
    fx_init();
    set_all(100, 100, 5, 100, 90);
    int32_t peak = 0;
    for (unsigned b = 0; b < 60u * FX_RATE / BLOCK; ++b) {
        for (unsigned i = 0; i < BLOCK; ++i) in[i] = (i & 32) ? 30000 : -30000;
        fx_process(in, out, BLOCK, 180);
        for (unsigned i = 0; i < 2 * BLOCK; ++i) {
            int32_t v = out[i] < 0 ? -out[i] : out[i];
            if (v > peak) peak = v;
        }
    }
    assert(peak < 4 * 32768);

    /* 6. CPU guard: thresholds, and a shed block carries the dry signal only. */
    {
        const uint32_t budget = 128u * 544u;           /* one block's deadline in ticks */
        assert(fx_guard(0, budget) == 0);
        assert(fx_guard(budget / 100u * 55u, budget) == 0);
        assert(fx_guard(budget / 100u * 55u + 1u, budget) == FX_SHED_SALA);
        assert(fx_guard(budget / 100u * 65u + 1u, budget) == (FX_SHED_SALA | FX_SHED_ECO));
        assert(fx_guard(budget, budget) == (FX_SHED_SALA | FX_SHED_ECO));
        fx_init();
        set_all(100, 100, 2, 0, 0);                    /* ROOM and ECHO full, GLUE off */
        for (unsigned b = 0; b < 200; ++b) {           /* fill the room and echo */
            for (unsigned i = 0; i < BLOCK; ++i) in[i] = (i & 16) ? 8000 : -8000;
            fx_process(in, out, BLOCK, 120);
        }
        fx_shed = FX_SHED_SALA | FX_SHED_ECO;
        for (unsigned i = 0; i < BLOCK; ++i) in[i] = (int32_t)(i * 37 % 2000) - 1000;
        fx_process(in, out, BLOCK, 120);
        for (unsigned i = 0; i < BLOCK; ++i) {
            int32_t dl = out[2 * i] - in[i], dr = out[2 * i + 1] - in[i];
            assert(dl >= -1 && dl <= 1 && dr >= -1 && dr <= 1);
        }
        fx_shed = 0;
        fx_process(in, out, BLOCK, 120);               /* and the tails come back */
        int wet = 0;
        for (unsigned i = 0; i < BLOCK; ++i) wet |= out[2 * i] - in[i] > 2 || out[2 * i] - in[i] < -2;
        assert(wet);
        printf("guard: ROOM sheds above 55 %%, ECHO above 65 %%; a shed block is dry\n");
    }
    printf("fx unit ok: TIME exact at 36..180 BPM x 6 divisions; re-arm silent; max peak %d\n", peak);
    return 0;
}
