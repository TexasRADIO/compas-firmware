#include <assert.h>
#include <stdio.h>
#include "board.h"
/* Exhaust the actual final converter, not a duplicate test implementation. */
int main(void)
{
    int32_t previous=-8388609;
    for(int32_t sample=-32768;sample<=32767;++sample) {
        int32_t word=board_dac_word(sample);
        assert(word>=-8388608 && word<=8388607);
        assert(word/BOARD_DAC_SCALE==sample);
        assert(word==sample*128*2);
        assert(word>previous);
        previous=word;
    }
    assert(board_dac_word(0)==0);
    assert(board_dac_word(-32768)==-8388608);
    assert(board_dac_word(32767)==8388352);
    puts("DAC: all 65536 Q15 inputs exact, monotonic, within signed 24-bit; +6.0206 dB.");
    return 0;
}
