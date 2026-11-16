#include <assert.h>
#include <stdint.h>
#include "../board/piano.c"

/* Measured before transposition was added, at 120 BPM with count-in.
 * Protect the printed-pitch audio for all six published score arrangements. */
static const uint64_t printed_pcm[6][2]={
 {0x84f5e4f81e60da26ull,0xc9fb7a5298ea250aull},
 {0xd848f33a9bd694a7ull,0x87861cac1495efccull},
 {0xb1fd81f8b74c8d88ull,0x6e59d79b10443daeull},
 {0xb9f1ee2b2e584098ull,0x32a9ad20dd078cfdull},
 {0x0e475937e8fcd3eaull,0xf9b832b86dfc0f4full},
 {0x758d5bdf098d15bbull,0x4651d648be735044ull}
};
static void check_pitch(unsigned note,int shift)
{
    if(!note) return;
    static const double ratio[]={.7071067811865476,.7491535384383408,.7937005259840998,
      .8408964152537145,.8908987181403393,.9438743126816935,1,
      1.0594630943592953,1.122462048309373,1.189207115002721,1.2599210498948732,1.3348398541700344};
    double error=score->increments[note+6+shift]-score->increments[note+6]*ratio[shift+6];
    assert(error>-2 && error<2);
}
int main(void)
{
    for(unsigned id=1;id<=6;++id) {
        for(unsigned guide=0;guide<2;++guide) {
            piano_start(id,1);
            uint64_t hash=14695981039346656037ull;
            for(unsigned i=0;i<44100*30;++i) {
                piano_step();uint32_t value=(uint32_t)piano_mix(120,guide);
                for(unsigned b=0;b<4;++b) {hash^=(uint8_t)(value>>(8*b));hash*=1099511628211ull;}
            }
            assert(hash==printed_pcm[id-1][guide]);
        }
        for(int shift=-6;shift<=5;++shift) {
            piano_start(id,1);piano_transpose(shift);
            for(unsigned h=0;h<score->hit_count;++h)
                for(unsigned n=0;n<score->voices;++n) check_pitch(score->hits[h].note[n],shift);
            if(score->lanes) for(unsigned v=0;v<score->voices;++v)
                for(unsigned h=0;h<score->lanes[v].count;++h) check_pitch(note_pitch(&score->lanes[v].notes[h]),shift);
            for(unsigned h=0;h<score->guide_count;++h) check_pitch(note_pitch(&score->canto[h]),shift);
            for(unsigned i=0;i<44100*3;++i) {piano_step();piano_mix(120,1);}
            /* Changing pitch preserves current envelopes, oscillator phase and time. */
            uint32_t phase=player.phase, guide_phase=player.guide_phase;
            int32_t tick=player.tick;unsigned gate=player.guide_gate;
            piano_transpose(0);
            assert(player.phase==phase && player.guide_phase==guide_phase && player.tick==tick && player.guide_gate==gate);
            piano_transpose(shift);piano_jump(score->form[1]);
            assert(player.pitch_offset==shift+6);
            piano_transpose(-7);piano_transpose(6);assert(player.pitch_offset==shift+6);
        }
    }
}
