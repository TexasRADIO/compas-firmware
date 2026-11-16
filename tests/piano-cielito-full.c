#include <assert.h>
#include "../board/piano.c"

static void advance_to(int32_t tick)
{
    while(player.tick<tick) {
        assert(piano_step()!=PIANO_END);
        if(player.tick<tick) piano_mix(108,1);
    }
    piano_mix(108,1);
}

static unsigned sounding(unsigned pitch,unsigned end)
{
    for(unsigned i=0;i<score->voices;++i)
        if(player.voice[i].note==pitch-35+1 && player.voice[i].gate==end) return 1;
    return 0;
}

int main(void)
{
    piano_start(1,0);
    assert(score->voices==6 && piano_beats()==3 && piano_has_guide());
    advance_to(120);
    assert(sounding(59,144) && !sounding(65,144)); /* B3 starts the roll. */
    advance_to(121);
    assert(sounding(59,144) && sounding(65,144) && !sounding(67,144));
    advance_to(123);
    assert(sounding(59,144) && sounding(65,144) && sounding(67,144) && sounding(71,144));
    piano_jump(28); /* G/B/D chord carries from the previous written bar. */
    assert(sounding(67,1992) && sounding(71,1992) && sounding(74,1992));
    piano_transpose(5);
    piano_jump(32);
    piano_mix(108,1);
    assert(player.pitch_offset==11 && player.guide_gate==2304);
    assert(sounding(36,2280) && sounding(48,2280));
    assert(sounding(72,2280) && sounding(76,2280) && sounding(79,2280) && sounding(84,2280));
    advance_to(2280);
    assert(player.guide_gate==2304);
    while(piano_step()!=PIANO_END) piano_mix(108,1);
    assert(player.tick==2304);
}
