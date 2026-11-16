/* SPDX-License-Identifier: GPL-3.0-only
 * Score audio fingerprint. Plays every written song to its end with the
 * count-in, guide melody on and off, at -6, 0 and +5 semitones, plus one
 * mid-song jump, and prints one FNV-1a hash per run. Any change to how the
 * scores are stored must leave every line identical.
 *
 *   python3 tools/board-piano.py --compact
 *   cc -std=c11 -O2 -DCOMPAS_BOARD -DCOMPAS_EMU_STORE -Iboard -Ibuild/board \
 *      tests/piano-audio.c board/piano.c -o build/piano-audio && build/piano-audio
 */
#include <stdio.h>
#include <stdlib.h>
#include "piano.h"
unsigned piano_id_valid(unsigned id, unsigned guide);

static unsigned long long run(unsigned id, int shift, unsigned guide, int jump)
{
    unsigned long long hash = 14695981039346656037ull;
    piano_start(id, 1);
    piano_transpose(shift);
    unsigned bars = 0;
    int jumped = 0;
    for (unsigned long n = 0; n < 44100ul * 600ul; ++n) {
        unsigned edge = piano_step();
        if (edge == PIANO_END && (!jump || jumped)) return hash;
        if (edge == PIANO_END) break;
        if (edge == PIANO_BAR && ++bars == 6 && jump) {
            piano_jump(2);
            jumped = 1;
        }
        uint32_t sample = (uint32_t)piano_mix(92, guide);
        for (unsigned b = 0; b < 4; ++b) {
            hash ^= (unsigned char)(sample >> (8 * b));
            hash *= 1099511628211ull;
        }
    }
    fprintf(stderr, "Incomplete score run: id=%u shift=%d guide=%u jump=%d\n",
            id, shift, guide, jump);
    exit(EXIT_FAILURE);
}

int main(void)
{
    static const int shifts[] = {-6, 0, 5};
    for (unsigned id = 1; piano_id_valid(id, 0); ++id)
        for (unsigned g = 0; g < 2; ++g)
            for (unsigned s = 0; s < 3; ++s)
                for (int j = 0; j < 2; ++j)
                    printf("%u %d %u %d %016llx\n", id, shifts[s], g, j, run(id, shifts[s], g, j));
    return 0;
}
