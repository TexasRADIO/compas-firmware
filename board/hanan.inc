/* Kumbia Kit. The local bank generator verifies the factory sample and preset bytes. */
#include "hanan-data.inc"

enum { BOARD_RATE=44100 };
static const uint8_t *const hanan_pcm[]={hanan_pcm_0,hanan_pcm_1,hanan_pcm_2,hanan_pcm_3};
static const uint16_t hanan_length[]={sizeof hanan_pcm_0,sizeof hanan_pcm_1,
                                     sizeof hanan_pcm_2,sizeof hanan_pcm_3};
static struct { uint32_t phase, increment; uint16_t end, base; } hanan_voice[4];
static uint8_t hanan_pattern, hanan_next, hanan_active, kumbia_accent;
static uint8_t kumbia[KUMBIA_SETTINGS];
static uint16_t hanan_phase, kumbia_hz, kumbia_gain[2];
static int16_t hanan_output;
uint8_t board_hanan_mode;

static void hanan_clear(void)
{
    hanan_active=kumbia_accent=0;
    hanan_phase=BOARD_RATE;
    hanan_output=0;
    hanan_next=HANAN_PATTERNS;
}

static void kumbia_voice_settings(unsigned voice)
{
    hanan_voice[voice].increment=hanan_voice[voice].base*kumbia_pitch_q10[kumbia[KUMBIA_PITCH]];
    hanan_voice[voice].end=hanan_length[voice]*kumbia[KUMBIA_DECAY]/100u;
}

void board_kumbia_export(uint8_t settings[KUMBIA_SETTINGS])
{
    for(unsigned i=0;i<KUMBIA_SETTINGS;++i) settings[i]=kumbia[i];
}

int board_kumbia_restore(const uint8_t settings[KUMBIA_SETTINGS])
{
    if(settings[0]>100 || settings[1]<5 || settings[1]>100 ||
       settings[2]>24 || settings[3]>KUMBIA_CLOCK_MAX) return 0;
    for(unsigned i=0;i<KUMBIA_SETTINGS;++i) kumbia[i]=settings[i];
    kumbia_hz=(uint16_t)board_kumbia_hz(kumbia[KUMBIA_CLOCK]);
    kumbia_gain[0]=kumbia_gain_q8[kumbia[KUMBIA_DRIVE]];
    kumbia_gain[1]=(uint16_t)(kumbia_gain[0]*(100u+kumbia[KUMBIA_DRIVE])/100u);
    for(unsigned i=0;i<4;++i) kumbia_voice_settings(i);
    return 1;
}

static void hanan_hit(unsigned voice, unsigned increment)
{
    hanan_voice[voice].phase=0;
    hanan_voice[voice].base=(uint16_t)increment;
    kumbia_voice_settings(voice);
    hanan_active|=1u<<voice;
}

void board_hanan_step(unsigned step, unsigned accented)
{
    if(!(step&1u)) {
        if(board_hanan_mode==HANAN_LAYER && kumbia[KUMBIA_DRIVE] && rack_sequence && !accented) {
            uint32_t bit=1u<<cp_status(CP_POSITION);
            for(unsigned i=0;i<RACK_SOUNDS;++i)
                if(rack_pattern_accents[i]&rack_pattern[i]&~record_pending[i]&bit) { accented=1; break; }
        }
        kumbia_accent=board_hanan_mode==HANAN_LAYER && accented;
    }
    if(!step && hanan_next<HANAN_PATTERNS) {
        hanan_pattern=hanan_next;
        hanan_next=HANAN_PATTERNS;
    }
    unsigned word=hanan_steps[hanan_pattern][step];
    if(word&1u) hanan_hit(0,157);
    if(word&2u) hanan_hit(1,128);
    if(word&4u) hanan_hit(2,16u+((word>>9)&31u)*16u);
    if(word&8u) hanan_hit(3,16u+((word>>4)&31u)*16u);
}

static int kumbia_shape(int sum)
{
    int dry=sum>128?128:sum<-127?-127:sum;
    unsigned drive=kumbia[KUMBIA_DRIVE];
    if(drive) {
        int input=sum*kumbia_gain[kumbia_accent]/256;
        if(input>256) input=256;
        if(input<-256) input=-256;
        int shaped=kumbia_clip[input+256];
        return (dry*(int)(100u-drive)*128 + shaped*(int)drive*80)/100;
    }
    return dry*128;
}

/* Source playback uses a hold between clock ticks. The transport has its own
 * clock, so changing the kit rate leaves the groove's BPM and cues in place. */
static int32_t hanan_mix(void)
{
    if(!board_hanan_mode && !hanan_active && !hanan_output) return 0;
    if(hanan_phase>=BOARD_RATE) {
        hanan_phase-=BOARD_RATE;
        int sum=0;
        for(unsigned i=0;i<4;++i) if(hanan_active&(1u<<i)) {
            unsigned at=hanan_voice[i].phase>>16;
            unsigned end=hanan_voice[i].end;
            if(at>=end) hanan_active&=~(1u<<i);
            else {
                int sample=(int)hanan_pcm[i][at]-127;
                if(kumbia[KUMBIA_DECAY]<100 && end-at<64u)
                    sample=sample*(int)(end-at)/64;
                sum+=sample;
                hanan_voice[i].phase+=hanan_voice[i].increment;
            }
        }
        hanan_output=(int16_t)kumbia_shape(sum);
    }
    hanan_phase+=kumbia_hz;
    return hanan_output;
}
