/* Emulator-only full-bank load. Included only by COMPAS_EMU_SWEEP builds.
 * All changes go through the normal foreground command queue. */
volatile uint32_t compas_sweep_done, compas_sweep_refused;
volatile uint8_t compas_sweep_parts[BOARD_GROOVE_PRESETS];
volatile uint32_t compas_sweep_blocks[BOARD_GROOVE_PRESETS];
static void sweep_send(unsigned cmd,unsigned a,unsigned b)
{
    if(board_submit((BoardCommand){(uint8_t)cmd,(uint8_t)a,(uint8_t)b})!=CP_OK)
        ++compas_sweep_refused;
}
static void emu_sweep(void)
{
    static unsigned study, action;
    static uint32_t base, last, hit;
    uint32_t block=board_stats.callbacks;
    if(block<20 || block==last || compas_sweep_done) return;
    last=block;
    if(!base) base=block;
    unsigned phase=block-base;
    BoardView v=board_view(0);
    if(v.study==study && v.playing) {
        if(v.part<PART_NONE) compas_sweep_parts[study]|=(uint8_t)(1u<<v.part);
        ++compas_sweep_blocks[study];
    }
    static const uint16_t times[]={0,2,4,6,8,1300,2300,3300,4300,5300};
    if(action<sizeof times/sizeof *times && phase>=times[action]) {
        switch(action++) {
        case 0: sweep_send(CP_STOP,0,0);break;
        case 1:
            sweep_send(CP_STUDY,study,0);sweep_send(BOARD_KIT,1,0);
            sweep_send(BOARD_MASTER,100,0);break;
        case 2:
            sweep_send(CP_BPM,180,0);sweep_send(CP_FEEL,100,0);
            sweep_send(BOARD_FX,FX_SALA,100);sweep_send(BOARD_FX,FX_ECO,100);
            sweep_send(BOARD_FX,FX_TIEMPO,5);sweep_send(BOARD_FX,FX_REPITE,90);
            sweep_send(BOARD_FX,FX_PEGA,100);break;
        case 3: sweep_send(BOARD_PART,PART_INTRO,0);break;
        case 4: sweep_send(CP_START,0,0);break;
        case 5: sweep_send(BOARD_PART,PART_VERSE_A,0);break;
        case 6: sweep_send(BOARD_PART,PART_VERSE_B,0);break;
        case 7: sweep_send(BOARD_PART,PART_CHORUS,0);break;
        case 8: sweep_send(BOARD_PART,PART_BREAK,0);break;
        case 9: sweep_send(BOARD_PART,PART_END,0);break;
        }
    } else if(phase>10 && phase<5300 && block-hit>=11) {
        static const uint8_t sounds[]={0,2,4,5,6,7,9,11};
        for(unsigned i=0;i<sizeof sounds;++i) sweep_send(BOARD_RACK,sounds[i],127);
        hit=block;
    }
    if(phase>=7500) {
        if(++study==BOARD_GROOVE_PRESETS) compas_sweep_done=1;
        else { base=block;action=0; }
    }
}
