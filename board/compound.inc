/* Foreground target test driver. Never a release or packaging input.
 * The private meter fixtures use their synth voices; manually triggered 727
 * hits exercise the real bank without inventing a 727 kit for the fixtures. */
#include "score-test.inc"
enum { COMPOUND_CASES=6 };
typedef struct {
    uint32_t study,bpm,steps,pulses,edited,restored,high_step;
    uint32_t phrase_frames,step_low,step_high,pulse_mask,count_mask,clock_bpm,clicks;
} CompoundResult;
volatile CompoundResult compas_compound_results[COMPOUND_CASES];
volatile uint32_t compas_compound_done,compas_compound_refused;
extern volatile uint32_t compas_compound_phrase_frame;
extern volatile uint32_t compas_compound_click_mask;
static void compound_send(unsigned cmd,unsigned a,unsigned b)
{
    if(board_submit((BoardCommand){(uint8_t)cmd,(uint8_t)a,(uint8_t)b})!=CP_OK)
        ++compas_compound_refused;
}
static void emu_compound(void)
{
    static unsigned index,stage,last_phrase;
    static uint32_t last,at,wrap,hit;
    uint32_t block=board_stats.callbacks;
    if(block<20 || block==last || compas_compound_done) return;
    last=block;
    static const uint8_t ids[]={CP_TEST_STUDY_COMPOUND_6_8_ENGINE_FIXTURE,
        CP_TEST_STUDY_COMPOUND_6_8_HEMIOLA_ENGINE_FIXTURE,
        CP_TEST_STUDY_COMPOUND_12_8_ENGINE_FIXTURE};
    volatile CompoundResult *r=&compas_compound_results[index];
    if(stage && block-at<3) return;
    at=block;
    BoardView v=board_view(0);
    switch(stage) {
    case 0:
        compound_send(CP_STOP,0,0);++stage;break;
    case 1:
        r->study=ids[index/2];r->bpm=index&1u?180u:60u;
        compound_send(CP_STUDY,r->study,0);compound_send(CP_BPM,r->bpm,0);
        compound_send(BOARD_KIT,0,0);compound_send(CP_FEEL,0,0);
        compound_send(BOARD_MASTER,60,0);compound_send(BOARD_COUNT_IN,1,0);
        ++stage;break;
    case 2: {
        static const int32_t neutral[7]={0};
        r->steps=cp_status(CP_STEP_COUNT);r->pulses=panel_beats(v.study);
        panel.page=PANEL_EDIT;panel.role=0;panel.bar=0;
        for(unsigned page=1;page<(r->steps+15u)/16u;++page)
            panel_input(&panel,1u<<1,0,neutral);
        panel_input(&panel,0,1u<<((r->steps-1u)%16u),neutral);
        ++stage;break;
    }
    case 3: {
        unsigned lock=board_lock();
        r->edited=cp_step(0,r->steps-1u)!=0;
        r->high_step=r->steps>32u && (v.steps_high&(1u<<(r->steps-33u)))!=0;
        r->restored=cp_snapshot()==CP_PROJECT_BYTES;
        board_unlock(lock);
        compound_send(CP_STEP,0,r->steps-1u);++stage;break;
    }
    case 4: {
        unsigned lock=board_lock();
        r->restored&=cp_step(0,r->steps-1u)==0;
        r->restored&=cp_restore(CP_PROJECT_BYTES)==CP_OK;
        r->restored&=cp_step(0,r->steps-1u)!=0;
        r->clock_bpm=cp_quarter_bpm(r->bpm);
        compas_compound_click_mask=0;
        board_unlock(lock);
        panel.page=PANEL_HOME;panel.bar=0;panel.held=0;
        compound_send(CP_START,0,0);last_phrase=wrap=0;++stage;break;
    }
    default: {
        if(board_count_active(v)) r->count_mask|=1u<<panel_beat_position(v.study,v.position);
        if(v.playing && !board_count_active(v)) {
            if(v.position<32u) r->step_low|=1u<<v.position;
            else r->step_high|=1u<<(v.position-32u);
            r->pulse_mask|=1u<<panel_beat_position(v.study,v.position);
        }
        unsigned phrase=cp_status(CP_PHRASE);
        r->clicks=compas_compound_click_mask;
        if(phrase!=last_phrase) {
            if(wrap) {
                r->phrase_frames=compas_compound_phrase_frame-wrap;
                if(++index==COMPOUND_CASES) compas_compound_done=1;
                else stage=0;
            }
            wrap=compas_compound_phrase_frame;last_phrase=phrase;
        }
        if(block-hit>=24u) {
            compound_send(BOARD_RACK,2,100);compound_send(BOARD_RACK,9,92);
            hit=block;
        }
        break;
    }
    }
}
