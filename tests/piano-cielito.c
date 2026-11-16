#include <assert.h>
#include "../board/piano.c"

static void advance_to(int32_t tick)
{
    while(player.tick<tick) {
        assert(piano_step()!=PIANO_END);
        if(player.tick<tick) piano_mix(108,1);
    }
}

static unsigned voices_until(unsigned gate)
{
    unsigned count=0;
    for(unsigned i=0;i<score->voices;++i) count+=player.voice[i].gate==gate;
    return count;
}

int main(void)
{
    piano_start(1,1);
    assert(piano_beats()==3 && player.tick==-72 && piano_has_guide());
    assert(score->voices==2 && score->bar_count==33 && score->end==2304);
    assert(!piano_has_percussion()); /* No invented 4/4 bolero under a 3/4 score. */
    advance_to(-48);
    assert(piano_position()==4 && player.click_age);
    advance_to(-24);
    assert(piano_position()==8 && player.click_age);
    advance_to(0);
    assert(piano_position()==0 && piano_bar()==0);
    advance_to(24);
    piano_mix(108,1);
    assert(piano_bar()==1 && piano_position()==0);
    assert(voices_until(26)==1 && voices_until(96)==1); /* Grace over held bass. */
    advance_to(26);
    piano_mix(108,1);
    assert(voices_until(168)==1 && voices_until(96)==1);

    piano_jump(17);
    assert(player.tick==1176 && piano_form()==PIANO_FORM_REFRAIN);
    piano_jump(20); /* Enter the second bar of the six-beat A4. */
    assert(player.tick==1392 && player.guide_gate==1464);
    piano_transpose(-3);
    piano_jump(24); /* Recover both notes of the tied C octave. */
    assert(player.tick==1680 && voices_until(1728)==2);
    assert(player.pitch_offset==3 && player.guide_gate==1728);
    assert(!piano_can_hold());
    piano_jump(25);
    assert(piano_can_hold());
    piano_jump(31);
    advance_to(2208);
    piano_mix(108,1);
    assert(player.guide_gate==2304);
    piano_jump(32);
    assert(player.tick==2256 && player.guide_gate==2304);
    while(piano_step()!=PIANO_END) piano_mix(108,1);
    assert(player.tick==2304); /* Final bar is two beats, not padded to three. */
}
