#include "panel.h"
#include "piano.h"

enum { OCT_DOWN=0, OCT_UP=1, MIX=2, ACCENT=3, BASS=4, PERC=5, EDIT=6, SET=7, HOME=8, SAVE=9,
       FILL=10, SECTION=11, PLAY=PANEL_PLAY_BUTTON, RACK=13 };
#include "panel-score.inc"
#define LANG_TABLE
#include "lang.inc"

uint8_t panel_lang;

static int word_compare(const char *a, const char *b)
{
    while(*a && *a==*b) { ++a; ++b; }
    return (unsigned char)*a-(unsigned char)*b;
}

const char *panel_word(const char *english)
{
    if(!panel_lang || !english || !*english) return english;
    unsigned lo=0, hi=LANG_WORDS;
    while(lo<hi) {
        unsigned mid=(lo+hi)/2u;
        const char *key=lang_text+lang_index[mid];
        int order=word_compare(english,key);
        if(!order) {
            for(unsigned skip=panel_lang;skip;--skip) while(*key++) {}
            return key;
        }
        if(order<0) hi=mid; else lo=mid+1u;
    }
    return english;
}
static const char *const sections[]={"BASE","LIFT","BREAK"};
const char *panel_groove_part_name(unsigned study,unsigned part,unsigned abbreviated)
{
    return conductor_forms[study_form[study]][part][abbreviated];
}
const char *panel_study_family(unsigned study)
{
    return study<sizeof study_family/sizeof *study_family ? study_family[study] : "";
}
unsigned panel_study_flags(unsigned study)
{
    return study<sizeof study_flags/sizeof *study_flags ? study_flags[study] : 0u;
}
const char *panel_score_part_name(unsigned score,unsigned part)
{
    if(score==PIANO_SCORE_JURAME) {
        static const char *const form[PART_NONE]={"START","REFRAIN","","ANSWER","","CADENCE"};
        return part<PART_NONE ? form[part] : "";
    }
    if(score==PIANO_SCORE_TEODORA) {
        static const char *const form[PART_NONE]={"PICKUP","CALL","","ECHOES","","CLOSE"};
        return part<PART_NONE ? form[part] : "";
    }
    if(part==PART_INTRO) {
        if(score==PIANO_SCORE_CIELITO) return "PICKUP";
        return score==PIANO_SCORE_CUCARACHA?"START":"INTRO";
    }
    if(part==PART_VERSE_A) return "VERSE";
    if(part==PART_CHORUS) return "CHORUS";
    if(part==PART_END) return score==PIANO_SCORE_CIELITO?"ENDING":"CODA";
    return "";
}
static const char *const sounds[]={
    "HIGH BONGO","LOW BONGO","MUTED HI CONGA","OPEN HI CONGA","LOW CONGA",
    "HIGH TIMBALE","LOW TIMBALE","HIGH AGOGO","LOW AGOGO","CABASA","MARACAS",
    "SHORT WHISTLE","LONG WHISTLE","QUIJADA","STAR CHIME"
};

static unsigned band_page(const Panel *panel)
{
    return panel->page==PANEL_HOME || panel->page==PANEL_BASS || panel->page==PANEL_PERC;
}

const uint8_t panel_part_keys[PART_NONE]={PANEL_INTRO_C4,PANEL_VERSE_D4,PANEL_VARIATION_E4,
                                 PANEL_CHORUS_F4,PANEL_BREAK_G4,PANEL_END_A4};
const uint8_t panel_layer_keys[5]={PANEL_CONGA_CS4,10,13,15,17};

static unsigned score_part(unsigned part)
{
    return part==PART_INTRO || part==PART_VERSE_A || part==PART_CHORUS || part==PART_END;
}

static unsigned score_hold_available(BoardView v)
{
    return v.playing && !v.score_ending && v.score_form!=PIANO_FORM_CODA;
}

PanelKeys panel_keys(const Panel *panel, BoardView v)
{
    PanelKeys map={.parts=0};
#ifdef COMPAS_HANAN
    if(panel->page==PANEL_HANAN) {
        map.pads=255u;
        map.active=1u<<(4u+v.hanan_pattern);
        if(v.hanan_next<HANAN_PATTERNS) map.queued=1u<<(4u+v.hanan_next);
        return map;
    }
#endif
    if(panel->page==PANEL_SET) {
        for(unsigned slot=0;slot<SET_SLOTS;++slot)
            if(board_song_exists(slot)) map.pads|=1u<<slot;
        if(v.song<SET_SLOTS) map.active=map.pads&(1u<<v.song);
        if(v.next_song<SET_SLOTS) map.queued=map.pads&(1u<<v.next_song);
    } else if(panel->page==PANEL_RACK) {
        map.pads=0xffffu;
        if(panel->rack_edit) {
            unsigned offset=meter[v.study]==2?0:panel->bar;
            map.enabled=(v.rack_steps>>offset)&0xffffu;
            map.queued=(v.rack_accents>>offset)&0xffffu;
            if(v.playing && v.position>=offset && v.position<offset+16u)
                map.active=1u<<(v.position-offset);
            if(v.piano) map=(PanelKeys){.parts=0};
        } else {
            map.active=1u<<panel->rack_sound;
            if(v.accent) map.enabled=1u<<15;
        }
    } else if(band_page(panel)) {
        map.pads=127u | (255u<<PANEL_PAD_B4);
        for(unsigned i=0;i<PART_NONE;++i)
            if(!v.piano || score_part(i)) map.parts|=1u<<panel_part_keys[i];
        if(v.piano) {
            if(v.score_has_guide) {
                map.upper|=1u<<panel_layer_keys[0];
                if(v.score_guide) map.enabled|=1u<<panel_layer_keys[0];
            }
            if(score_hold_available(v)) map.upper|=1u<<panel_layer_keys[1];
            if(v.score_hold) map.enabled|=1u<<panel_layer_keys[1];
            if(v.score_percussion) {
                map.upper|=1u<<panel_layer_keys[2];
                if(board_score_perc_on(v)) map.enabled|=1u<<panel_layer_keys[2];
            }
        } else {
            for(unsigned i=0;i<4;++i) {
                map.upper|=1u<<panel_layer_keys[i];
                if(v.overlays&(1u<<i)) map.enabled|=1u<<panel_layer_keys[i];
            }
            if(panel_has_bass(v)) {
                map.upper|=1u<<panel_layer_keys[4];
                if(!(v.muted&32u) && v.levels[5]) map.enabled|=1u<<panel_layer_keys[4];
            }
        }
        unsigned part=v.part;
        if(v.piano && (v.playing || part==PART_NONE)) {
            static const uint8_t written[]={PART_INTRO,PART_INTRO,PART_VERSE_A,PART_CHORUS,PART_END};
            part=written[v.score_form];
        } else if(part==PART_NONE) part=v.section==1 ? PART_CHORUS : v.section==2 ? PART_BREAK : PART_VERSE_A;
        map.active=1u<<panel_part_keys[part];
        if(v.next_part<PART_NONE) map.queued=1u<<panel_part_keys[v.next_part];
    }
    return map;
}

unsigned panel_key_number(unsigned key,PanelKeys map)
{
    unsigned bit=1u<<key;
    if(map.parts&bit)
        for(unsigned i=0;i<PART_NONE;++i) if(panel_part_keys[i]==key) return i+1;
    if(map.upper&bit)
        for(unsigned i=0;i<sizeof panel_layer_keys;++i) if(panel_layer_keys[i]==key) return i+1;
    return 0;
}

static void select_instrument(Panel *panel,int32_t delta)
{
    static const uint8_t pages[]={PANEL_HOME,PANEL_BASS,PANEL_PERC,PANEL_RACK
#ifdef COMPAS_HANAN
        ,PANEL_HANAN
#endif
    };
    for(unsigned i=0;i<sizeof pages;++i) {
        if(panel->page!=pages[i]) continue;
        int count=(int)sizeof pages;
        int index=((int)i+delta%count+count)%count;
        panel->page=pages[index];
        return;
    }
}

static void send(Panel *panel, unsigned cmd, unsigned a, unsigned b)
{
    panel->locked=(uint8_t)board_submit((BoardCommand){cmd,a,b});
}
static unsigned adjust(unsigned value, int32_t delta, unsigned lo, unsigned hi)
{
    int32_t next=(int32_t)value+delta;
    if(next<(int32_t)lo) return lo;
    if(next>(int32_t)hi) return hi;
    return (unsigned)next;
}

void panel_master(Panel *panel, int32_t raw)
{
    if (raw<0 || raw>1023) return;
    unsigned position=(unsigned)raw+1u;
    /* Learn the parked position at power-on. Deadband keeps a stationary
     * pot from overriding KNOB4 or unmuting the stopped board. */
    if (!panel->master_pot) {
        panel->master_pot=(uint16_t)position;
        return;
    }
    if (position+12u>panel->master_pot && position<panel->master_pot+12u) return;
    unsigned level=raw<16 ? 0u : raw>=1008 ? 100u : ((unsigned)raw*100u+511u)/1023u;
    if (board_submit((BoardCommand){BOARD_MASTER,level,0})==CP_OK)
        panel->master_pot=(uint16_t)position;
}

static void browse_song(Panel *panel, BoardView v, int32_t delta)
{
    int slot=v.next_song<SET_SLOTS ? v.next_song : v.song<SET_SLOTS ? v.song : panel->slot;
    int direction=delta<0 ? -1 : 1;
    unsigned target=(unsigned)slot;
    for(unsigned i=0;i<SET_SLOTS && delta && (slot+=direction)>=0 && slot<SET_SLOTS;++i) {
        if(!board_song_exists((unsigned)slot)) continue;
        target=(unsigned)slot;
        delta-=direction;
    }
    if(target==v.next_song || (target==v.song && v.next_song==SET_SLOTS)) return;
    panel->slot=(uint8_t)target;
    send(panel,BOARD_CUE,target,0);
}

void panel_scan(Panel *panel, uint32_t buttons, uint32_t keys, const int32_t enc[7], uint32_t now)
{
    BoardView v=board_view(panel->page==PANEL_BASS?BOARD_BASS_LINE_ROLE:panel->role);
    unsigned previous_page=panel->page, previous_edit=panel->rack_edit;
    unsigned play=buttons&(1u<<PLAY);
    if (play && (v.playing || (!band_page(panel) && panel->page!=PANEL_SET))) {
        if(v.playing) ++panel->stop_epoch;
#ifdef COMPAS_HANAN
        if(panel->page==PANEL_HANAN && !v.playing) {
            if(v.piano) return;
            if(v.hanan_mode==HANAN_OFF) {
                send(panel,CP_BPM,90,0);
                send(panel,BOARD_HANAN_MODE,HANAN_SOLO,0);
            }
        }
#endif
        send(panel,v.playing?CP_STOP:CP_START,0,0);
        panel->held=keys;
        panel->last_part=0;
        return;
    }
    if (buttons&(1u<<HOME)) panel->page=PANEL_HOME;
    else if (buttons&(1u<<MIX)) {
#ifdef COMPAS_HANAN
        if(panel->page==PANEL_HANAN)
            send(panel,BOARD_HANAN_MODE,(v.hanan_mode+1u)%3u,0);
        else
#endif
        if(panel->page==PANEL_RACK && !v.piano) send(panel,BOARD_SEQ_ENABLE,(v.rack_sequence+1u)%3u,0);
        else panel->page=PANEL_MIX;
    }
    else if (buttons&(1u<<BASS)) panel->page=panel->page==PANEL_BASS?PANEL_HOME:PANEL_BASS;
    else if (buttons&(1u<<PERC)) panel->page=panel->page==PANEL_PERC?PANEL_HOME:PANEL_PERC;
    else if (buttons&(1u<<EDIT)) {
        if(panel->page==PANEL_RACK) send(panel,BOARD_ASSIGN,panel->role,panel->rack_sound+1u);
        else panel->page=PANEL_EDIT;
    }
    else if(buttons&(1u<<RACK)) {
        if(panel->page==PANEL_RACK && !v.piano)
            send(panel,BOARD_REC,board_recording(v)?0u:panel->rack_sound+1u,0);
        panel->page=PANEL_RACK; panel->rack_edit=0;
    }
    else if (buttons&(1u<<SET)) {
        if(panel->page!=PANEL_SET && v.song<SET_SLOTS) panel->slot=v.song;
        panel->page=PANEL_SET;
    }
    if(v.piano && panel->page==PANEL_EDIT) panel->page=PANEL_HOME;
    if(enc[0]) select_instrument(panel,enc[0]);
#ifdef COMPAS_HANAN
    if(panel->page==PANEL_HANAN) {
        unsigned pressed=keys&~panel->held;
        panel->held=keys;
        for(unsigned i=0;i<4;++i) {
            if(pressed&(1u<<i)) send(panel,BOARD_HANAN_HIT,i,0);
            if(pressed&(1u<<(i+4u))) send(panel,BOARD_HANAN_CUE,i,0);
        }
        if(enc[6]) {
            unsigned from=v.hanan_next<HANAN_PATTERNS?v.hanan_next:v.hanan_pattern;
            send(panel,BOARD_HANAN_CUE,adjust(from,enc[6],0,HANAN_PATTERNS-1),0);
        }
        static const uint8_t min[]={0,5,0,0}, max[]={100,100,24,KUMBIA_CLOCK_MAX};
        for(unsigned i=0;i<KUMBIA_SETTINGS;++i)
            if(enc[2+i]) send(panel,BOARD_HANAN_MOD,i,adjust(v.kumbia[i],enc[2+i],min[i],max[i]));
        if(buttons&(1u<<SAVE)) send(panel,BOARD_HANAN_SAVE,0,0);
        return;
    }
#endif
    if(panel->page==PANEL_RACK && previous_page!=PANEL_RACK && v.rack_sequence)
        panel->rack_sound=v.rack_sequence_sound;
    if(panel->page==PANEL_RACK && (buttons&(1u<<SECTION))) {
        panel->rack_edit=!panel->rack_edit;
        if(panel->rack_edit && board_recording(v)) send(panel,BOARD_REC,0,0);
        panel->bar=0;
        send(panel,BOARD_SEQ_SELECT,panel->rack_sound,0);
        buttons&=~(1u<<SECTION);
    }
    unsigned rack_roll=board_roll_rate(v);
    if(previous_page==PANEL_RACK && !previous_edit && (panel->page!=PANEL_RACK || panel->rack_edit) && rack_roll)
        send(panel,BOARD_ROLL_KEYS,0,0);
    if(panel->page==PANEL_RACK && !panel->rack_edit && !v.piano) {
        unsigned toggled=buttons&(1u<<FILL);
        if(toggled) {
            rack_roll=rack_roll?ROLL_OFF:ROLL_SIXTEENTH;
            send(panel,BOARD_ROLL,rack_roll,0);
            buttons&=~(1u<<FILL);
        }
        if(rack_roll && enc[1])
            send(panel,BOARD_ROLL,adjust(rack_roll,enc[1],ROLL_EIGHTH,ROLL_THIRTYSECOND),0);
        if(rack_roll && (toggled || ((keys^panel->held)&0x7fffu))) {
            unsigned held=keys&0x7fffu;
            send(panel,BOARD_ROLL_KEYS,held&255u,held>>8);
        }
    }
    if(enc[6]) {
        if(panel->page==PANEL_RACK && (!panel->rack_edit || v.piano)) {
            unsigned from=v.next_groove<BOARD_GROOVE_PRESETS?v.next_groove:v.study;
            unsigned index=v.piano?0:adjust(from,enc[6],0,BOARD_GROOVE_PRESETS-1);
            panel->rack_edit=0;panel->bar=0;
            send(panel,BOARD_GROOVE_CUE,index,0);
        } else if(panel->page==PANEL_RACK) {
            unsigned from=v.next_rhythm<BOARD_RHYTHM_PATTERNS?v.next_rhythm:v.rhythm;
            unsigned index=from<BOARD_RHYTHM_PATTERNS?adjust(from,enc[6],0,BOARD_RHYTHM_PATTERNS-1):0;
            panel->rack_edit=1;panel->bar=0;
            panel->rack_sound=(uint8_t)board_rhythm_sound(index);
            send(panel,BOARD_SEQ_SELECT,panel->rack_sound,0);
            send(panel,BOARD_RHYTHM_CUE,index,0);
        } else browse_song(panel,v,enc[6]);
    }
    if(panel->page==PANEL_SET) {
        int32_t slot_delta=enc[4]+(enc[6]?0:enc[0]);
        /* ALGORITHM picks the screen language here (RPT has its own knob). */
        if(enc[1]) panel_lang=(uint8_t)adjust(panel_lang,enc[1],0,LANG_COUNT-1);
        if(slot_delta) panel->slot=(uint8_t)adjust(panel->slot,slot_delta,0,SET_SLOTS-1);
        if(buttons&(1u<<OCT_DOWN)) panel->slot=(uint8_t)adjust(panel->slot,-1,0,SET_SLOTS-1);
        if(buttons&(1u<<OCT_UP)) panel->slot=(uint8_t)adjust(panel->slot,1,0,SET_SLOTS-1);
        if(buttons&(1u<<SECTION))
            send(panel,v.piano && v.playing ? BOARD_ENDING : BOARD_CUE,panel->slot,0);
        if(buttons&(1u<<SAVE)) send(panel,BOARD_SAVE,panel->slot,0);
        if(buttons&(1u<<FILL)) send(panel,v.piano?BOARD_HOLD:CP_FILL,0,0);
        if(enc[3] && !v.piano)
            send(panel,BOARD_REPEAT,adjust(v.phrases,enc[3],0,64),0);
        if(enc[2]) send(panel,CP_BPM,adjust(v.bpm,enc[2],60,180),0);
        if(enc[5] && !v.piano && !v.playing)
            send(panel,BOARD_COUNT_IN,enc[5]>0,0);
        uint32_t pressed=keys&~panel->held;
        panel->held=keys;
        for(unsigned i=0;i<SET_SLOTS;++i) {
            if(pressed&(1u<<i)) {
                panel->slot=(uint8_t)i;
                send(panel,BOARD_CUE,i,0);
                break;
            }
        }
        if(play) send(panel,CP_START,0,0);
        return;
    }
    if(band_page(panel)) {
        if(panel->page==PANEL_BASS) {
            unsigned key_buttons=buttons&((1u<<OCT_DOWN)|(1u<<OCT_UP));
            if(panel_has_bass(v) && (key_buttons==1u<<OCT_DOWN || key_buttons==1u<<OCT_UP)) {
                unsigned from=board_next_root(v)<12?board_next_root(v):board_root(v);
                send(panel,CP_ROOT,adjust(from,key_buttons==1u<<OCT_UP?1:-1,0,11),0);
            }
            if(panel_has_bass(v) && (enc[2] || enc[3])) send(panel,BOARD_BASS_SHAPE,adjust(v.bass_cut,enc[2],0,100),
                                     adjust(v.bass_bite,enc[3],0,100));
            if(enc[4] && panel_has_bass(v))
                send(panel,CP_LEVEL,5,adjust(v.levels[5],enc[4],0,100));
            if(enc[5] && panel_has_bass(v)) send(panel,BOARD_BASS_STYLE,adjust(v.bass_style,enc[5],0,1),0);
        } else if(panel->page==PANEL_PERC) {
            unsigned direction=buttons&((1u<<OCT_DOWN)|(1u<<OCT_UP));
            if(board_has_clave(v) && (direction==1u<<OCT_DOWN || direction==1u<<OCT_UP))
                send(panel,CP_CLAVE,direction==1u<<OCT_UP,0);
            for(unsigned lane=0;lane<4;++lane)
                if(enc[2+lane] && !v.piano) send(panel,BOARD_OVERLAY_LEVEL,lane,
                                      adjust(v.overlay_levels[lane],enc[2+lane],0,100));
        } else {
            if(enc[2]) send(panel,CP_BPM,adjust(v.bpm,enc[2],60,180),0);
            if(v.piano && v.score_has_guide && enc[3] && (enc[3]>0)!=v.score_guide)
                send(panel,BOARD_GUIDE,0,0);
            if(v.piano && score_hold_available(v) && enc[4] && (enc[4]>0)!=v.score_hold)
                send(panel,BOARD_HOLD,0,0);
            if(v.piano && enc[5]) {
                unsigned root=board_next_root(v)<12 ? board_next_root(v) : board_root(v);
                /* Order the twelve nearest keys by signed semitone, not pitch class. */
                unsigned position=root<6 ? root+6 : root-6;
                unsigned next=adjust(position,enc[5],0,11);
                send(panel,CP_ROOT,next<6 ? next+6 : next-6,0);
            }
            if(enc[3] && !v.piano) send(panel,CP_FEEL,adjust(v.feel,enc[3],0,100),0);
            if(enc[4] && panel_has_bass(v))
                send(panel,CP_LEVEL,5,adjust(v.levels[5],enc[4],0,100));
            if(enc[5] && !v.piano) {
                int32_t delta=enc[5]<-100 ? -100 : enc[5]>100 ? 100 : enc[5];
                send(panel,BOARD_OVERLAY_LEVEL,BOARD_OVERLAY_ALL,(unsigned)(128+delta));
            }
        }
        /* ALGORITHM is Energy on every performance page; lands on the phrase. */
        if(enc[1] && !v.piano) {
            unsigned from=v.pending<3 ? v.pending : board_part_energy(v.part,v.section);
            send(panel,CP_SECTION,adjust(from,enc[1],0,2),0);
        }
        if(buttons&(1u<<FILL)) send(panel,v.piano?BOARD_HOLD:CP_FILL,0,0);
        if(buttons&(1u<<SECTION)) {
            if(v.piano && v.playing) send(panel,BOARD_ENDING,0,0);
            else send(panel,BOARD_PART,PART_END,0);
        }
        uint32_t pressed=keys&~panel->held;
        panel->held=keys;
        for(unsigned i=0;i<PART_NONE;++i) {
            if(!(pressed&(1u<<panel_part_keys[i]))) continue;
            if(!v.piano || score_part(i)) {
                unsigned rapid=!v.piano && v.playing && !v.fill && !(buttons&(1u<<FILL)) &&
                    panel->last_part==i+1 && v.next_part==i && now-panel->part_at<=9600000u;
                send(panel,BOARD_PART,i,rapid);
                panel->last_part=(uint8_t)(i+1);
                panel->part_at=now;
            }
            break;
        }
        if(play) send(panel,CP_START,0,0);
        if(v.piano) {
            if((pressed&(1u<<panel_layer_keys[0])) && v.score_has_guide) send(panel,BOARD_GUIDE,0,0);
            if(score_hold_available(v) && (pressed&(1u<<panel_layer_keys[1]))) send(panel,BOARD_HOLD,0,0);
            if(v.score_percussion && (pressed&(1u<<panel_layer_keys[2]))) send(panel,BOARD_SCORE_PERC,0,0);
        } else {
            for(unsigned i=0;i<4;++i)
                if(pressed&(1u<<panel_layer_keys[i])) send(panel,BOARD_OVERLAY,i,0);
            if((pressed&(1u<<panel_layer_keys[4])) && panel_has_bass(v)) send(panel,CP_MUTE,5,0);
        }
        if(buttons&(1u<<ACCENT)) send(panel,BOARD_ACCENT,!v.accent,0);
        unsigned pads=(pressed&127u) | ((pressed>>PANEL_PAD_B4)&255u)<<7;
        if(pads) send(panel,BOARD_PAD_HITS,pads&255u,pads>>8);
        return;
    }
    if(play) {
        send(panel,CP_START,0,0);
        panel->held=keys;
        return;
    }
    if(panel->page==PANEL_RACK && panel->rack_edit) {
        if(enc[2]) {
            panel->rack_sound=(uint8_t)adjust(panel->rack_sound,enc[2],0,RACK_SOUNDS-1);
            send(panel,BOARD_SEQ_SELECT,panel->rack_sound,0);
        }
        if(enc[3] && !v.piano) send(panel,CP_BPM,adjust(v.bpm,enc[3],60,180),0);
        if(enc[4]) send(panel,BOARD_RACK_CONTROL,RACK_HIT_STRENGTH,adjust(v.rack_strength,enc[4],0,127));
        if(enc[5]) send(panel,BOARD_RACK_CONTROL,RACK_LEVEL,adjust(v.rack_level,enc[5],0,100));
        if(enc[1] && !v.piano) send(panel,CP_SECTION,adjust(v.pending<3?v.pending:board_part_energy(v.part,v.section),enc[1],0,2),0);
        if(buttons&(1u<<ACCENT)) send(panel,BOARD_ACCENT,!v.accent,0);
        if(buttons&(1u<<OCT_DOWN)) panel->bar=0;
        if(buttons&(1u<<OCT_UP)) panel->bar=meter[v.study]==4?16:0;
        if(meter[v.study]==2) panel->bar=0;
        uint32_t pressed=keys&~panel->held;
        panel->held=keys;
        for(unsigned i=0;i<16;++i) if(pressed&(1u<<i))
            send(panel,BOARD_SEQ_STEP,panel->rack_sound,panel->bar+i);
        return;
    }
    if (meter[v.study]==2) panel->bar=panel->bar?8:0;
    else panel->bar=panel->bar?16:0;
    if (buttons&(1u<<OCT_DOWN)) {
        if(panel->page==PANEL_RACK) send(panel,BOARD_ASSIGN,panel->role,0);
        else panel->bar=0;
    }
    if (buttons&(1u<<OCT_UP)) panel->bar=meter[v.study]==2?8:16;
    if (buttons&(1u<<FILL)) send(panel,v.piano?BOARD_HOLD:CP_FILL,0,0);
    if(buttons&(1u<<SECTION)) {
        if(v.piano) send(panel,BOARD_ENDING,0,0);
        else if(panel->page==PANEL_RACK) send(panel,BOARD_KIT,!v.kit,0);
        else send(panel,CP_SECTION,(v.section+1u)%3u,0);
    }
    if(panel->page==PANEL_RACK) {
        if(enc[1] && !v.piano && !rack_roll)
            send(panel,CP_SECTION,adjust(v.pending<3?v.pending:board_part_energy(v.part,v.section),enc[1],0,2),0);
        if(enc[2]) {
            panel->rack_sound=(uint8_t)adjust(panel->rack_sound,enc[2],0,RACK_SOUNDS-1);
            if(!board_recording(v)) send(panel,BOARD_SEQ_SELECT,panel->rack_sound,0);
        }
        if(enc[3]) send(panel,BOARD_RACK_CONTROL,RACK_HIT_STRENGTH,adjust(v.rack_strength,enc[3],0,127));
        if(enc[4]) send(panel,BOARD_RACK_CONTROL,RACK_ACCENT_STRENGTH,adjust(v.rack_accent,enc[4],0,127));
        if(enc[5]) send(panel,BOARD_RACK_CONTROL,RACK_LEVEL,adjust(v.rack_level,enc[5],0,100));
        if(buttons&(1u<<ACCENT)) send(panel,BOARD_ACCENT,!v.accent,0);
    } else {
        if(enc[0]) panel->role=(uint8_t)adjust(panel->role,enc[0],0,5);
        if(enc[1]) send(panel,CP_SECTION,adjust(v.section,enc[1],0,2),0);
        if(enc[2]) send(panel,CP_BPM,adjust(v.bpm,enc[2],60,180),0);
        if(enc[3] && !v.piano) send(panel,CP_FEEL,adjust(v.feel,enc[3],0,100),0);
        if(enc[4] && !v.piano) send(panel,CP_LEVEL,panel->role,adjust(v.levels[panel->role],enc[4],0,100));
        if(enc[5] && panel->page==PANEL_MIX) send(panel,BOARD_SPEED,adjust(v.speed,enc[5],60,100),0);
    }
    uint32_t pressed=keys&~panel->held;
    panel->held=keys;
    for (unsigned i=0;i<16;++i) {
        if (!(pressed&(1u<<i))) continue;
        if (panel->page==PANEL_RACK) {
            if(i==15) send(panel,BOARD_ACCENT,!v.accent,0);
            else {
                panel->rack_sound=(uint8_t)i;
                send(panel,BOARD_RACK,i,0);
            }
        } else if(panel->page==PANEL_EDIT) {
            if(i<(meter[v.study]==2?8u:16u)) send(panel,CP_STEP,panel->role,panel->bar+i);
        } else if(panel->page==PANEL_MIX && !v.piano) {
            if(i<6) send(panel,CP_MUTE,i,0);
            else if(i<12) send(panel,CP_SOLO,i-6,0);
        } else if(i<6) send(panel,CP_HIT,i,0);
    }
}

void panel_input(Panel *panel,uint32_t buttons,uint32_t keys,const int32_t enc[7])
{
    panel_scan(panel,buttons,keys,enc,0);
}

static void label(char *line, unsigned col, const char *text)
{
    text=panel_word(text);
    while(*text && col<FACE_COLS) line[col++]=*text++;
}
static void number(char *line, unsigned col, unsigned value)
{
    line[col]=(char)('0'+value/100);
    line[col+1]=(char)('0'+value/10%10);
    line[col+2]=(char)('0'+value%10);
}
unsigned panel_beats(unsigned study) { return 2u*meter[study]; }
unsigned panel_has_bass(BoardView view) { return !view.piano && study_bass[view.study]; }
const char *panel_root_name(unsigned root)
{
    static const char *const names[]={"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    return root<12?names[root]:"-";
}
const char *panel_title(BoardView v)
{
    return v.song<SET_SLOTS ? board_song_title(v.song) : studies[v.study];
}
void panel_face(const Panel *panel, BoardView v, char lines[FACE_LINES][FACE_COLS+1])
{
    for(unsigned i=0;i<FACE_LINES;++i) {
        for(unsigned j=0;j<FACE_COLS;++j) lines[i][j]=' ';
        lines[i][FACE_COLS]='\0';
    }
    label(lines[0],0,"COMPAS");
    static const char *const pages[]={"BAND","MIX","STEP","727","SONGS","BASS","PERC"};
    label(lines[0],10,pages[panel->page]);
    label(lines[0],15,v.fault?"FAULT":v.playing?"LIVE":"READY");
    label(lines[1],0,v.song<SET_SLOTS?board_song_title(v.song):studies[v.study]);
    label(lines[2],0,sections[v.section]);
    if(v.pending<3) {
        label(lines[2],6,">");
        label(lines[2],8,sections[v.pending]);
    } else if(v.fill) label(lines[2],7,v.fill==1?"FILL QUEUED":"FILL");
    if(band_page(panel) || panel->page==PANEL_SET) {
        static const uint8_t score_parts[]={PART_INTRO,PART_INTRO,PART_VERSE_A,PART_CHORUS,PART_END};
        const char *current=board_count_active(v)?"COUNT IN":v.piano ? (v.score_form?panel_score_part_name(v.piano,score_parts[v.score_form]):"COUNT IN") :
            panel_groove_part_name(v.study,v.part<PART_NONE?v.part:
                v.section==1?PART_CHORUS:v.section==2?PART_BREAK:PART_VERSE_A,0);
        if(v.piano && !v.playing && v.part<PART_NONE)
            current=panel_score_part_name(v.piano,v.part);
        label(lines[2],0,"                    ");
        label(lines[2],0,current);
        if(v.piano && v.playing && v.score_bar) {
            label(lines[2],12,"BAR"); number(lines[2],16,v.score_bar);
        } else if(!v.piano && v.fill) label(lines[2],9,v.fill==1?"FILL NEXT":"FILL");
        if(v.next_song<SET_SLOTS) {
            label(lines[3],0,"NEXT SONG");
            label(lines[4],0,board_song_title(v.next_song));
        } else if(v.next_part<PART_NONE) {
            label(lines[3],0,"NEXT PART");
            label(lines[4],0,v.piano?panel_score_part_name(v.piano,v.next_part):panel_groove_part_name(v.study,v.next_part,0));
        } else if(panel->page==PANEL_SET) {
            label(lines[3],0,"PRESETS LOAD SONG");
            label(lines[4],0,"SAVE TO"); number(lines[4],8,panel->slot+1);
            label(lines[4],12,"SELECT");
        } else if(panel->page==PANEL_BASS) {
            label(lines[3],0,v.piano ? "SCORE ACCOMPANIMENT" : !panel_has_bass(v) ? "DRUM ENSEMBLE" :
                v.muted&32u ? "BASS MUTED / TOP 5" : v.bass_style==BASS_ACID ? "4 ACID BASS" : "4 ENSEMBLE BASS");
            if(!panel_has_bass(v)) label(lines[4],0,"NO SEPARATE BASS");
            else {
                label(lines[4],0,"1 TONE"); number(lines[4],7,v.bass_cut);
                label(lines[4],11,"2 BITE"); number(lines[4],17,v.bass_bite);
            }
        } else if(panel->page==PANEL_PERC) {
            if(v.piano) {
                label(lines[3],0,v.score_percussion?
                    (board_score_perc_on(v)?"BOLERO BACKING":"BOLERO MUTED"):"NO AUTO RHYTHM");
                label(lines[4],0,v.score_percussion?"CONGA + MARACA":"727 FOR DRUM PADS");
            } else {
            label(lines[3],0,"CON"); lines[3][3]=v.overlays&1u?'+':'-'; number(lines[3],5,v.overlay_levels[0]);
            label(lines[3],11,"MAR"); lines[3][14]=v.overlays&2u?'+':'-'; number(lines[3],16,v.overlay_levels[1]);
            label(lines[4],0,"TIM"); lines[4][3]=v.overlays&4u?'+':'-'; number(lines[4],5,v.overlay_levels[2]);
            label(lines[4],11,"BEL"); lines[4][14]=v.overlays&8u?'+':'-'; number(lines[4],16,v.overlay_levels[3]);
            }
        } else if(v.piano==PIANO_SCORE_JURAME) {
            label(lines[3],0,"1 START  2 REFRAIN");
            label(lines[4],0,"4 ANSWER 6 CADENCE");
        } else if(v.piano==PIANO_SCORE_TEODORA) {
            label(lines[3],0,"1 PICKUP 2 CALL");
            label(lines[4],0,"4 ECHOES 6 CLOSE");
        } else if(v.piano) {
            label(lines[3],0,v.piano==PIANO_SCORE_CIELITO?"1 PICKUP 2 VERSE":
                  v.piano==PIANO_SCORE_CUCARACHA?"1 START  2 VERSE":"1 INTRO  2 VERSE");
            label(lines[4],0,v.piano==PIANO_SCORE_CIELITO?"4 CHORUS 6 ENDING":"4 CHORUS  6 CODA");
        } else {
            label(lines[3],0,"1 INTRO  2 VERSE A");
            label(lines[4],0,"3 VERSE B 4 CHORUS");
        }
        if(!v.playing && panel->page==PANEL_SET && !board_song_exists(panel->slot))
            label(lines[5],0,"EMPTY / SAVE HERE");
        else if(!v.playing) label(lines[5],0,"PLAY / PRESETS SONG");
        else if(!v.piano && v.fill && (v.next_song<SET_SLOTS || v.next_part<PART_NONE))
            label(lines[5],0,"CHANGE AFTER FILL");
        else if(v.piano && v.score_form==PIANO_FORM_CODA) label(lines[5],0,v.piano==PIANO_SCORE_CIELITO?"ENDING PLAYING":"CODA PLAYING");
        else if(v.piano && v.score_hold) label(lines[5],0,"HOLD / ARP RELEASE");
        else if(panel->page==PANEL_SET) label(lines[5],0,"HOME FOR SONG PARTS");
        else if(panel->page==PANEL_PERC) label(lines[5],0,v.piano?
            (v.score_percussion?"TOP 3 TOGGLE BOLERO":"727 FOR DRUM PADS"):"TOP KEYS TOGGLE");
        else if(panel->page==PANEL_BASS) {
            if(!panel_has_bass(v)) label(lines[5],0,"HOME FOR SONG PARTS");
            else { label(lines[5],0,"OCT-/+ KEY"); label(lines[5],11,panel_root_name(board_root(v)));
                if(board_next_root(v)<12) { label(lines[5],14,">");label(lines[5],16,panel_root_name(board_next_root(v))); } }
        }
        else label(lines[5],0,v.piano?"TOP 2 HOLD / SEQ CODA":"ALGO ENERGY / 6 END");
        if(v.piano && v.score_has_guide) label(lines[6],0,v.score_guide?"GUIDE ON / TOP 1":"GUIDE OFF / TOP 1");
        else label(lines[6],0,band_page(panel) && panel->page!=PANEL_HOME?"SELECT PAGE":"KNOB1 TEMPO");
        if(panel->page==PANEL_PERC && board_has_clave(v)) {
            label(lines[5],0,"CLAVE               ");label(lines[5],6,board_clave(v)?"2-3":"3-2");
            if(board_next_clave(v)<2) { label(lines[5],10,"> ");label(lines[5],12,board_next_clave(v)?"2-3":"3-2"); }
            label(lines[6],0,board_clave_linked(v)?"BAND+727 +/- ":"BAND ONLY +/-");
        }
        number(lines[6],13,v.bpm); label(lines[6],17,"BPM");
    } else if (panel->page==PANEL_RACK && panel->rack_edit) {
        label(lines[3],0,sounds[panel->rack_sound]);
        if(v.piano) label(lines[4],0,"CHOOSE A GROOVE");
        else for(unsigned i=0;i<16;++i) lines[4][i]=(v.rack_steps&(1u<<(panel->bar+i)))?'#':'-';
        label(lines[5],0,v.rack_sequence==2?"FX DRUMS ONLY":v.rack_sequence?"FX LAYER ON":"FX LAYER OFF");
        label(lines[6],0,"SEQ PADS / SESSION");
    } else if (panel->page==PANEL_RACK) {
        label(lines[3],0,"2 HIT"); number(lines[3],6,v.rack_strength);
        label(lines[3],10,"3 ACC"); number(lines[3],16,v.rack_accent);
        label(lines[4],0,"1"); label(lines[4],2,sounds[panel->rack_sound]);
        label(lines[5],0,"4 LEVEL"); number(lines[5],8,v.rack_level);
        label(lines[5],13,v.accent?"ACC ON":"ACC OFF");
        label(lines[6],0,v.piano?"SEQ 16-STEP EDITOR":v.rack_sequence==2?"FX DRUM LOOP":v.rack_sequence?"FX LAYER ON":"FX LAYER OFF");
    } else {
        label(lines[3],0,names[v.study][panel->role]);
        label(lines[3],12,"LVL");
        number(lines[3],16,v.levels[panel->role]);
        if(panel->page==PANEL_EDIT) {
            unsigned count=meter[v.study]==2?8:16;
            for(unsigned i=0;i<count;++i)
                lines[4][i]=(v.steps&(1u<<(i+panel->bar)))?'#':'-';
            label(lines[5],0,panel->bar?"BAR 2 / OCT-":"BAR 1 / OCT+");
        } else if(panel->page==PANEL_MIX) {
            label(lines[4],0,"1-6 MUTE 7-12 SOLO");
            for(unsigned i=0;i<6;++i) {
                lines[5][i*3]=(char)('1'+i);
                lines[5][i*3+1]=v.solo==i?'S':(v.muted&(1u<<i))?'M':'-';
            }
        } else label(lines[5],0,"ARP FILL / FX MIX");
        label(lines[6],0,"BPM"); number(lines[6],4,v.bpm);
        label(lines[6],10,"FEEL"); number(lines[6],15,v.feel);
        if(panel->page==PANEL_MIX) {
            /* Rounded tenths of a semitone, -120*log2(percent/100). */
            static const uint8_t pitch[]={88,86,83,80,77,75,72,69,67,64,62,59,57,54,52,50,48,45,43,41,39,36,34,32,30,28,26,24,22,20,18,16,14,13,11,9,7,5,3,2,0};
            unsigned rate=v.speed>=60 && v.speed<=100?v.speed:100;
            unsigned drop=pitch[rate-60];
            label(lines[6],0,"                    ");
            number(lines[6],0,board_effective_bpm(v));label(lines[6],4,"BPM  -00.0 ST");
            lines[6][10]=(char)('0'+drop/100u);
            lines[6][11]=(char)('0'+drop/10u%10u);
            lines[6][13]=(char)('0'+drop%10u);
        }
    }
    if(v.fault) label(lines[7],0,"OUTPUT STOPPED");
    else if(panel->locked || v.result==CP_BUSY) label(lines[7],0,"BUSY / RETRY");
    else if(v.result==CP_STOP_FIRST) label(lines[7],0,"STOP TO EDIT");
    else if(v.result==BOARD_EMPTY) label(lines[7],0,"EMPTY SLOT");
    else if(v.result==BOARD_SESSION) label(lines[7],0,"SAVED FOR SESSION");
    else if(v.result==BOARD_SAVED) label(lines[7],0,"SET SAVED");
    else if(v.result) label(lines[7],0,"ACTION UNAVAILABLE");
    else if(!v.master) label(lines[7],0,"TURN MASTER");
    else if(v.playing && !v.audible && panel->page!=PANEL_RACK) label(lines[7],0,"MIX SILENT");
    else {
        label(lines[7],0,"MASTER"); number(lines[7],8,v.master);
        if(band_page(panel)) {
            label(lines[7],12,"BPM"); number(lines[7],16,v.bpm);
        }
    }
}
