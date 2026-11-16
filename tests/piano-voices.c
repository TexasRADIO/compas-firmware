#include <assert.h>
#include "../board/piano.c"

static void advance_to(int32_t tick)
{
    while(player.tick<tick) {
        assert(piano_step()!=PIANO_END);
        if(player.tick<tick) piano_mix(180,0);
    }
}

static unsigned sounding_until(unsigned gate)
{
    for(unsigned i=0;i<score->voices;++i)
        if(player.voice[i].gate==gate) return player.voice[i].note;
    return 0;
}

#ifdef TEST_KEMPTON_EXCERPT
int main(void)
{
    piano_start(1,1);
    assert(score->voices==5 && piano_has_guide() && piano_beats()==2);
    assert(player.tick==-96 && piano_position()==0 && player.click_age);
    advance_to(-72);
    assert(piano_position()==2 && !player.click_age);
    advance_to(-48);
    assert(piano_position()==4 && player.click_age);
    advance_to(0);
    assert(piano_position()==0 && piano_form()==PIANO_FORM_INTRO);

    advance_to(432);
    piano_mix(180,0);
    unsigned held=sounding_until(576);
    assert(held && sounding_until(456));
    advance_to(456);
    piano_mix(180,0);
    assert(sounding_until(576)==held && sounding_until(480));
    advance_to(480);
    piano_mix(180,0);
    assert(sounding_until(576)==held && player.guide_gate==576 && !player.guide_gain);

    piano_jump(5);
    assert(player.tick==480 && sounding_until(576)==held && player.guide_gate==576);
    piano_mix(180,1);
    assert(player.guide_gain==1);
    advance_to(504);
    piano_mix(180,1);
    assert(sounding_until(576)==held && sounding_until(516) && sounding_until(552));

    piano_jump(6);
    assert(!sounding_until(576) && !player.guide_gate);
    advance_to(624);
    piano_mix(180,0);
    assert(sounding_until(768)==held);
    while(piano_step()!=PIANO_END) piano_mix(180,0);
    assert(player.tick==768);
}
#else
int main(void)
{
    piano_start(3,1);
    assert(piano_has_guide() && piano_beats()==2 && player.tick==-96);
    piano_jump(84);
    advance_to(8112);
    piano_mix(112,1);
    assert(player.guide_gate==8424);
    unsigned held=player.guide_note;
    piano_jump(87);
    assert(player.tick==8352 && player.guide_gate==8424 && player.guide_note==held);
    advance_to(8424);
    assert(guide_mix(1)==0);
    piano_jump(88);
    assert(!player.guide_gate);

    piano_start(4,1);
    assert(score->lanes && score->bar_count==81 && score->voices==5);
    advance_to(12);
    piano_mix(88,0);
    unsigned tied=sounding_until(68);
    assert(tied && sounding_until(30));
    piano_jump(2);
    assert(player.tick==60 && sounding_until(68)==tied);
    piano_jump(80);
    advance_to(3828);
    piano_mix(88,0);
    unsigned ending_voices=0;
    for(unsigned i=0;i<score->voices;++i)
        if(player.voice[i].gate==3840) ++ending_voices;
    assert(ending_voices==5);

    piano_start(5,0);
    assert(score->lanes && score->bar_count==41 && score->voices==6);
    piano_jump(40);
    assert(player.tick==3744);
    advance_to(3792);
    piano_mix(84,0);
    ending_voices=0;
    for(unsigned i=0;i<score->voices;++i)
        if(player.voice[i].gate==3804) ++ending_voices;
    assert(ending_voices==6);
    assert(!piano_has_percussion());
    const PianoScore *printed_piano=score;

    piano_start(19,1);
    assert(score->bars==printed_piano->bars && score->lanes==printed_piano->lanes &&
           score->canto==printed_piano->canto && score->increments==printed_piano->increments);
    assert(piano_has_guide() && piano_has_percussion());
    assert(score->lanes && score->bar_count==41 && score->voices==6);
    assert(piano_form()==PIANO_FORM_COUNT_IN && !piano_percussion());
    advance_to(0);
    assert(piano_form()==PIANO_FORM_INTRO && !piano_percussion());
    piano_jump(piano_part_bar(PIANO_FORM_VERSE));
    int32_t verse=player.tick;
    assert(piano_percussion()==PIANO_MARACA && !piano_percussion());
    advance_to(verse+18);
    assert(piano_percussion()==PIANO_CONGA_MUTE);
    advance_to(verse+42);
    assert(piano_percussion()==PIANO_CONGA_OPEN);
    piano_jump(piano_part_bar(PIANO_FORM_REFRAIN));
    assert(piano_percussion()==PIANO_MARACA);
    piano_jump(piano_coda());
    assert(piano_form()==PIANO_FORM_CODA && !piano_percussion());

    piano_start(PIANO_SCORE_TEODORA,1);
    assert(score->bar_count==21 && score->voices==3 && piano_beats()==2);
    assert(player.tick==-72 && score->tempo_ticks==36 && !piano_has_guide());
    for(unsigned frame=0;frame<73500u;++frame) {
        assert(piano_step()==PIANO_INSIDE);
        if(frame==36750) assert(player.tick==-36 && player.click_age==96 && piano_position()==4);
        piano_mix(72,0);
    }
    assert(piano_step()==PIANO_BAR && !player.tick && piano_position()==0);
    assert(piano_form()==PIANO_FORM_INTRO && !player.click_age);
    advance_to(36);
    assert(piano_bar()==1 && piano_position()==0 && piano_form()==PIANO_FORM_VERSE);
    piano_jump(piano_part_bar(PIANO_FORM_REFRAIN));
    assert(player.tick==828 && player.gain==128 && piano_form()==PIANO_FORM_REFRAIN);
    advance_to(888);
    assert(player.gain==97);
    advance_to(1032);
    assert(player.gain==72);
    advance_to(1176);
    assert(player.gain==50);
    piano_jump(12);
    assert(player.gain==128 && player.mark==0);
    piano_jump(piano_coda());
    assert(player.tick==1404 && player.gain==50 && !piano_percussion());
    advance_to(1429);
    assert(piano_mix(72,0)==0);
    while(piano_step()!=PIANO_END) piano_mix(72,0);
    assert(player.tick==1476);

    piano_start(22,0);
    assert(score->bar_count==64 && piano_beats()==3 && player.gain==72);
    piano_jump(23);
    advance_to(1728);
    assert(player.gain==175);
    piano_jump(31);
    advance_to(2304);
    assert(player.gain==72);
    piano_jump(piano_coda());
    assert(player.tick==4464 && player.gain==175 && piano_form()==PIANO_FORM_CODA);

    piano_start(PIANO_SCORE_JURAME,0);
    assert(score->bar_count==23 && piano_beats()==2 && piano_has_guide());
    piano_jump(piano_coda());
    assert(player.tick==816 && piano_form()==PIANO_FORM_CODA);
    piano_mix(72,1);
    /* The high G5 is chosen over E5. Index 1 is the C1 in the final bass. */
    assert(player.guide_note==56 && player.guide_gate==840);
    advance_to(840);
    piano_mix(72,1);
    assert(player.guide_note==56 && player.guide_gate==876);
    piano_jump(22);
    assert(player.tick==1056 && player.guide_gate==1068);
    while(piano_step()!=PIANO_END) piano_mix(72,1);
    assert(player.tick==1104);

    piano_start(PIANO_SCORE_JURAME_SONG,0);
    assert(score->bar_count==90 && piano_beats()==2 && piano_has_guide());
    piano_jump(45);
    assert(player.tick==2160 && !player.guide_gate && piano_form()==PIANO_FORM_INTRO);
    piano_jump(50);
    assert(player.tick==2400 && piano_form()==PIANO_FORM_VERSE);
    piano_jump(67);
    assert(player.tick==3216 && piano_form()==PIANO_FORM_REFRAIN);
    piano_jump(64);
    advance_to(3090);
    assert(player.guide_gate==3090 && guide_mix(1)==0);
    advance_to(3108);
    piano_mix(72,1);
    assert(player.guide_gate==3114);
    piano_jump(piano_coda());
    assert(player.tick==4032 && piano_form()==PIANO_FORM_CODA);
    while(piano_step()!=PIANO_END) piano_mix(72,1);
    assert(player.tick==4320);
}
#endif
