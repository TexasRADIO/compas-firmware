/* Host test: Sideman report encoding/rate limit and the MIDI clock lock. */
#include <assert.h>
#include <stdio.h>
#include "sideman.h"

_Static_assert(sizeof(SidemanState) == 12, "SidemanState has no hidden padding");

static unsigned clocks_for(unsigned bpm, unsigned bars, unsigned *starts, unsigned *stops,
                           unsigned jitter)
{
    MidiClock c = {0};
    const uint32_t step_ticks = (uint32_t)(SIDEMAN_TICK_HZ * 60ull / (bpm * 4u));
    const uint32_t poll = 12000u;
    uint32_t t = 1000u, total = 0, end;
    unsigned pos = 0;
    uint32_t next_step = t + step_ticks;
    *starts = *stops = 0;
    /* one idle look while stopped, then play */
    midiclock_step(&c, 0, 0, bpm, t);
    end = t + step_ticks * 16u * bars + step_ticks / 2u;
    for (; t < end; t += poll + (jitter ? (t / poll) % 7u * 1500u : 0u)) {
        while (t >= next_step) { pos = (pos + 1u) & 15u; next_step += step_ticks; }
        ClockOut o = midiclock_step(&c, 1, pos, bpm, t);
        *starts += o.start; *stops += o.stop; total += o.ticks;
    }
    ClockOut o = midiclock_step(&c, 0, pos, bpm, t);
    *stops += o.stop; total += o.ticks;
    return total;
}

int main(void)
{
    uint8_t out[SIDEMAN_BYTES];
    SidemanState s = {3, 2, 1, 4, 300, 6, 1, 3, 2, 1, 0};
    assert(sideman_encode(&s, out) == 19);
    const uint8_t want[19] = {0xF0, 0x00, 0x22, 0x24, 0x35, 0x01, 0x02, 3, 2, 1, 4,
                              300 >> 7, 300 & 127, 6, 1, 3, 2, 1, 0xF7};
    for (unsigned i = 0; i < 19; ++i) assert(out[i] == want[i]);
    for (unsigned i = 1; i < 18; ++i) assert(out[i] < 0x80);

    Sideman m = {0};
    assert(sideman_due(&m, &s, 0, out));            /* first report always */
    sideman_sent(&m, &s, 0);
    assert(!sideman_due(&m, &s, 1000, out));        /* unchanged */
    s.page = 4;
    assert(!sideman_due(&m, &s, SIDEMAN_MIN_GAP - 1, out) && m.waiting);  /* too soon */
    assert(sideman_due(&m, &s, SIDEMAN_MIN_GAP, out) && out[7] == 4);     /* not lost */
    sideman_sent(&m, &s, SIDEMAN_MIN_GAP);

    /* clock: 24 per quarter, one Start, one Stop, exact over whole bars */
    static const unsigned tempos[] = {60, 90, 120, 178, 240, 300};
    for (unsigned j = 0; j < 2; ++j)
        for (unsigned i = 0; i < sizeof tempos / sizeof *tempos; ++i) {
            unsigned starts, stops, n = clocks_for(tempos[i], 8, &starts, &stops, j);
            /* Start goes with the first sixteenth seen: 8 bars minus that step,
             * plus up to the half step after the last boundary. */
            unsigned lo = 8u * 96u - 6u, hi = 8u * 96u + 3u;
            if (starts != 1 || stops != 1 || n < lo || n > hi) {
                printf("bpm %u jitter %u: %u clocks (%u..%u) start %u stop %u\n",
                       tempos[i], j, n, lo, hi, starts, stops);
                return 1;
            }
        }
    puts("sideman ok");
    return 0;
}
