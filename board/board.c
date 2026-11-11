#include "board.h"
#include "piano.h"

typedef struct { uint32_t offset, frames; } Clip;
#include "727.inc"
extern const uint8_t compas_packed727[];
typedef struct {
    const uint8_t *next;
    uint32_t remaining;
    int16_t sample;
    uint8_t gain;
    uint8_t sound;
    uint8_t repeats;
} Player;

int32_t board_dma[2][BOARD_WORDS] __attribute__((aligned(4)));
BoardStats board_stats;
_Static_assert(__builtin_offsetof(BoardStats, irq_max_ticks)==36, "irq.S counter offset");
static Player sequence[CP_ROLES], rack[RACK_VOICES];
static RackFeedback rack_feedback, rack_pending;
static BoardCommand mailbox[8];
static uint8_t read_pos, write_pos, queued, stop, kit, master, result, fault, accent;
typedef struct {
    char title[19];
    uint8_t project[CP_PROJECT_BYTES], assignment[CP_ROLES], kit, section, phrases, piano, guide_or_count;
} Song;
#define SONG_HIDDEN '\x7f'
#include "set.inc"
#include "board-grooves.inc"
_Static_assert(sizeof rhythm_bank/sizeof *rhythm_bank==BOARD_RHYTHM_PATTERNS,"rhythm bank count");
_Static_assert(sizeof groove_titles/sizeof *groove_titles==BOARD_GROOVE_PRESETS,"groove bank count");
static uint8_t queued_groove, queued_root, queued_clave;
static Song captures[SET_SLOTS];
typedef struct {
    uint8_t bytes[BOARD_RHYTHM_BYTES], origin, bass_style, bass_cut, bass_bite;
    uint8_t overlays, levels[4], accent, part;
} RhythmSlot;
static RhythmSlot rhythm_slots[SET_SLOTS];
_Static_assert(sizeof rhythm_slots==BOARD_RHYTHM_SLOTS_BYTES,"Rhythm slots are packed");
static uint8_t rhythm_origin;
static void capture_rhythm(unsigned slot);
static void recall_rhythm(unsigned slot);
#ifdef COMPAS_EMU_STORE
_Static_assert(sizeof captures==BOARD_SET_BYTES,"Saved set layout is byte-packed");
static volatile uint8_t save_ready;
#endif
static uint8_t active_song, queued_song, phrases, remaining_phrases, song_dirty;
static uint8_t assigned[CP_ROLES];
static uint8_t active_part, queued_part, queued_energy, next_bar;
static uint8_t bass_style, bass_cut, bass_bite, overlays, overlay_hits, bass_hit;
static uint8_t overlay_levels[4], rack_controls[3];
static uint32_t rack_pattern[RACK_SOUNDS], rack_pattern_accents[RACK_SOUNDS];
static uint32_t record_pending[RACK_SOUNDS];
static uint8_t record_armed;
static uint8_t rack_sequence, rack_sequence_sound, active_rhythm, queued_rhythm;
uint8_t board_rhythm_only, board_roll_mode;
uint16_t board_roll_keys;
static const uint8_t score_parts[]={PIANO_FORM_INTRO,PIANO_FORM_VERSE,0,PIANO_FORM_REFRAIN,0,PIANO_FORM_CODA};
enum { NO_MASTER_REQUEST=255 };
static uint8_t master_request;
static uint32_t master_gain;
/* Varispeed is a live master treatment, not part of a song snapshot. Keep two
 * source samples and a hundredth-frame phase; the existing engine block holds
 * at most 130 newly rendered frames. No second PCM block is allocated. */
static uint8_t speed, speed_phase, speed_primed;
static int16_t speed_left, speed_right;
uint8_t board_piano_selected;
static uint8_t groove_count_enabled;
static uint8_t piano_running, piano_tempo, piano_hold, piano_guide, piano_perc_muted;
static BoardView view;
static uint8_t requested_role, shown_role;

#ifdef COMPAS_HANAN
#include "hanan.inc"
#endif
static void publish_view(unsigned refresh);

static void clear_recording(void)
{
    record_armed=0;
    for(unsigned i=0;i<RACK_SOUNDS;++i) record_pending[i]=0;
}

/* Manual p. 7 gives five exclusive pairs. Quijada and chime have their
 * own generators even though the stock pattern display shares a row. */
static const uint8_t group[RACK_SOUNDS]={0,0,1,1,2,3,4,5,5,6,6,7,7,8,9};
static void choke(unsigned sound)
{
    for(unsigned i=0;i<CP_ROLES;++i)
        if(sequence[i].remaining && group[sequence[i].sound]==group[sound]) {
            sequence[i].remaining=0;
            if(sequence[i].sound!=sound) rack_pending.cuts|=1u<<sequence[i].sound;
            ++board_stats.choke_cuts;
        }
    for(unsigned i=0;i<RACK_VOICES;++i)
        if(rack[i].remaining && group[rack[i].sound]==group[sound]) {
            rack[i].remaining=0;
            if(rack[i].sound!=sound) rack_pending.cuts|=1u<<rack[i].sound;
            ++board_stats.choke_cuts;
        }
}
static void begin(Player *player, unsigned sound, unsigned gain)
{
    choke(sound);
    player->next = compas_packed727 + clips[sound].offset;
    player->sample = 0;
    player->remaining = clips[sound].frames;
    player->gain = (uint8_t)gain;
    player->sound = (uint8_t)sound;
    player->repeats = 0;
}

int board_note727(unsigned study, unsigned role, unsigned event, unsigned velocity)
{
    /* +1 indices. Preserve the coastal instruments, cáscara, campana, güiro and bass. */
    unsigned sound = assigned[role] ? assigned[role] : groove_kit[study][role];
    if (!kit || !sound) return 0;
    if (!assigned[role] && sound == 3 && event == 2) sound = 4;
    begin(&sequence[role], sound-1, velocity);
    return 1;
}

/* Clips start at zero. Token zero repeats the previous sample for
 * the next byte's frame count. High-bit tokens carry absolute values. */
static inline int32_t next727(Player *player)
{
    int32_t sample=player->sample;
    if(player->repeats) --player->repeats;
    else {
        unsigned code=*player->next++;
        if(code>=128u) sample=(int32_t)((code-128u)*256u+*player->next++)-16384;
        else if(code) sample+=(int32_t)code-64;
        else player->repeats=(uint8_t)(*player->next++-1u);
        player->sample=(int16_t)sample;
    }
    --player->remaining;
    return sample;
}

int32_t board_sample727(unsigned role)
{
    Player *player = &sequence[role];
    if (!player->remaining) return 0;
    return next727(player) * player->gain / 128;
}
void board_clear727(unsigned role) { sequence[role].remaining = 0; }
int board_playing727(unsigned role) { return sequence[role].remaining != 0; }

static void clear_rack(void)
{
    for (unsigned i=0; i<RACK_VOICES; ++i) rack[i].remaining=0;
}
static void silence(int32_t *dest)
{
    for (unsigned i=0; i<BOARD_WORDS; ++i) dest[i]=0;
}
static void reset_song_layers(void)
{
    bass_style=BASS_ENSEMBLE;
    bass_cut=44;
    bass_bite=45;
    cp_bass_set(bass_style,bass_cut,bass_bite);
    overlays=0;
    for(unsigned i=0;i<4;++i) overlay_levels[i]=100;
}

void board_init(void)
{
    board_stats=(BoardStats){.callbacks=0};
#ifdef COMPAS_HANAN
    board_hanan_mode=HANAN_OFF;
    hanan_pattern=0;
    const uint8_t home[KUMBIA_SETTINGS]={0,100,12,0};
    board_kumbia_restore(home);
    hanan_clear();
#endif
    read_pos=write_pos=queued=stop=kit=master=result=fault=accent=0;
    master_gain=0;
    bass_hit=overlay_hits=0;
    rack_feedback=(RackFeedback){.hits=0};
    board_roll_mode=board_roll_keys=0;
    clear_recording();
    speed=100;speed_phase=speed_primed=0;speed_left=speed_right=0;
    master_request=NO_MASTER_REQUEST;
    board_piano_selected=piano_running=piano_hold=piano_guide=piano_perc_muted=groove_count_enabled=0;
    for(unsigned r=0;r<CP_ROLES;++r) assigned[r]=0;
    requested_role=shown_role=0;
    active_song=queued_song=SET_SLOTS;queued_root=12;queued_clave=2;
    queued_groove=BOARD_GROOVE_PRESETS;
    active_part=queued_part=PART_NONE;
    queued_energy=3; next_bar=0;
    phrases=remaining_phrases=song_dirty=0;
    for(unsigned i=0;i<SET_SLOTS;++i) {
        captures[i].title[0]=0;
        for(unsigned j=0;j<sizeof(RhythmSlot);++j) ((uint8_t *)&rhythm_slots[i])[j]=0;
        rhythm_slots[i].origin=BOARD_CUSTOM_RHYTHM;
    }
    rhythm_origin=BOARD_CUSTOM_RHYTHM;
    cp_init(44100);
    reset_song_layers();
    rack_sequence=rack_sequence_sound=board_rhythm_only=0;
    active_rhythm=queued_rhythm=BOARD_CUSTOM_RHYTHM;
    for(unsigned i=0;i<RACK_SOUNDS;++i) rack_pattern[i]=rack_pattern_accents[i]=0;
    rack_controls[RACK_HIT_STRENGTH]=92;
    rack_controls[RACK_ACCENT_STRENGTH]=124;
    rack_controls[RACK_LEVEL]=100;
#ifdef COMPAS_EMU_STORE
    save_ready=0;
#endif
    clear_rack();
    silence(board_dma[0]);
    silence(board_dma[1]);
    publish_view(1);
}

unsigned board_submit(BoardCommand cmd)
{
    uint32_t previous=board_lock();
    unsigned response=CP_OK;
    if (fault) response=BOARD_FAULT;
    else if (cmd.command==BOARD_MASTER) {
        /* Latest volume wins, even when musical commands fill the mailbox. */
        if (cmd.a>100) response=CP_RANGE;
        else master_request=cmd.a;
    }
    else if (cmd.command==CP_STOP) stop=1;
    else if (stop || queued==sizeof mailbox/sizeof *mailbox) response=BOARD_QUEUE_FULL;
    else {
        mailbox[write_pos]=cmd;
        write_pos=(write_pos+1u)&7u;
        ++queued;
    }
    board_unlock(previous);
    return response;
}

#ifdef COMPAS_USB
int board_ota_idle(void)
{
    uint32_t irq=board_lock();
    unsigned idle=!queued && !view.playing && !save_ready;
    board_unlock(irq);
    return idle;
}
#endif

static void rack_hit(unsigned sound, unsigned velocity)
{
    unsigned free_slot=RACK_VOICES;
    for (unsigned i=0; i<RACK_VOICES; ++i) {
        if (rack[i].remaining && group[rack[i].sound]==group[sound]) { free_slot=i; break; }
        if (!rack[i].remaining && free_slot==RACK_VOICES) free_slot=i;
    }
    /* A full rack contains all ten groups, so the requested group already
     * owns a slot. No independent voice needs to be stolen. */
    if(free_slot<RACK_VOICES) {
        begin(&rack[free_slot],sound,velocity);
        rack_pending.hits|=1u<<sound;
        if(!rack_pending.manual) rack_pending.sound=(uint8_t)sound;
    }
}

static unsigned rack_gain(unsigned sound, unsigned velocity, unsigned accented)
{
    unsigned gain=velocity*rack_controls[RACK_LEVEL]/100u;
    if(accented) gain=(gain*accent_gain_q12[sound]+2048u)>>12;
    return gain;
}

static unsigned paired_rhythm(unsigned rhythm);
static void manual_rack_hit(unsigned sound, unsigned velocity)
{
    unsigned accented=!velocity && accent;
    if(!velocity) velocity=rack_controls[accented ? RACK_ACCENT_STRENGTH : RACK_HIT_STRENGTH];
    unsigned gain=rack_gain(sound,velocity,accented);
    if(record_armed && sound==rack_sequence_sound && cp_status(CP_PLAYING) && !cp_board_counting()) {
        uint32_t bit=1u<<cp_board_nearest_step();
        record_pending[sound]|=bit&~rack_pattern[sound];
        rack_pattern[sound]|=bit;
        if(accented) rack_pattern_accents[sound]|=bit;
        else rack_pattern_accents[sound]&=~bit;
        if(paired_rhythm(queued_rhythm)) queued_clave=2;
        active_rhythm=queued_rhythm=BOARD_CUSTOM_RHYTHM;
        if(!rack_sequence) rack_sequence=1;
    }
    if(gain) {
        rack_hit(sound,gain);
        rack_pending.manual|=1u<<sound;
        rack_pending.sound=(uint8_t)(sound|(accented?128u:0u));
    }
}

void board_roll_trigger(void)
{
    unsigned keys=board_roll_keys&~rack_pending.manual;
    while(keys) {
        unsigned sound=__builtin_ctz(keys);
        manual_rack_hit(sound,0);
        keys&=keys-1u;
    }
}

/* Optional original percussion patterns. Son/salsa keep the two-bar
 * timeline, and the duple patterns use the actual 16-step phrase. */
/* Independent 727 lanes share the accompaniment clock, not its six roles.
 * Iterate in hardware voice order: later hits in an exclusive pair win. */
const char *board_rhythm_title(unsigned index)
{
    return index<BOARD_RHYTHM_PATTERNS?rhythm_bank[index].title:"USER RHYTHM";
}
unsigned board_rhythm_sound(unsigned index)
{
    if(index>=BOARD_RHYTHM_PATTERNS) return 3;
    for(unsigned i=0;i<RACK_SOUNDS;++i) if(rhythm_bank[index].hits[i]) return i;
    return 3;
}
static void copy_rhythm(unsigned index)
{
    for(unsigned i=0;i<RACK_SOUNDS;++i) {
        rack_pattern[i]=rhythm_bank[index].hits[i];
        rack_pattern_accents[i]=rhythm_bank[index].accents[i];
    }
    rhythm_origin=active_rhythm=(uint8_t)index;
}
static unsigned paired_rhythm(unsigned index)
{
    return !board_piano_selected && (cp_status(CP_STUDY_ID)==2 || cp_status(CP_STUDY_ID)==6) &&
        board_rhythm_direction(index)<2;
}
static void align_clave_rhythm(unsigned direction)
{
    if(paired_rhythm(active_rhythm)) copy_rhythm(board_clave_rhythm(active_rhythm,direction));
}
static void load_rhythm(unsigned index)
{
    clear_recording();
    copy_rhythm(index);
    rack_sequence_sound=(uint8_t)board_rhythm_sound(index);
    if(paired_rhythm(index)) { cp_board_clave(board_rhythm_direction(index));queued_clave=2; }
    queued_rhythm=BOARD_CUSTOM_RHYTHM;
    if(!rack_sequence) rack_sequence=1;
}
void board_sequence_step(unsigned step)
{
    if(queued_rhythm<BOARD_RHYTHM_PATTERNS && !paired_rhythm(queued_rhythm) && !(step%16u)) load_rhythm(queued_rhythm);
#ifdef COMPAS_HANAN
    if(board_hanan_mode==HANAN_SOLO) return;
#endif
    if(!rack_sequence) return;
    if(!step) for(unsigned i=0;i<RACK_SOUNDS;++i) record_pending[i]=0;
    uint32_t bit=1u<<step;
    for(unsigned sound=0;sound<RACK_SOUNDS;++sound) if((rack_pattern[sound]&~record_pending[sound])&bit) {
        unsigned accented=(rack_pattern_accents[sound]&bit)!=0;
        unsigned strength=rack_controls[accented ? RACK_ACCENT_STRENGTH : RACK_HIT_STRENGTH];
        unsigned gain=rack_gain(sound,strength,accented);
        if(gain) rack_hit(sound,gain);
    }
}

void board_bass_note_on(void) { ++bass_hit; }

void board_overlay_step(unsigned study,unsigned step)
{
    overlay_hits=0;
    if(!overlays || board_drums_only()) return;
    static const uint32_t patterns[][4]={
        {0x40404040u,0x44444444u,0x80008000u,0x10101010u},
        {0x50105010u,0x44444444u,0x80808080u,0x14111411u},
        {0x00008888u,0x0000aaaau,0x00008080u,0x00004444u},
        {0x00880088u,0x44444444u,0x80008000u,0x10001000u},
    };
    static const uint8_t sound[]={3,10,5,7}, gain[]={66,56,65,58};
    const uint32_t bit=1u<<step;
    for(unsigned lane=0;lane<4;++lane) {
        if(!(overlays&(1u<<lane)) || !(patterns[groove_overlay[study]][lane]&bit)) continue;
        unsigned velocity=gain[lane]*overlay_levels[lane]/100u;
        if(velocity) {
            rack_hit(sound[lane],velocity);
            overlay_hits|=1u<<lane;
        }
    }
}

static const Song *song_at(unsigned slot)
{
    if(slot>=SET_SLOTS) return 0;
    if(captures[slot].title[0]==SONG_HIDDEN) return 0;
    if(captures[slot].title[0]) return &captures[slot];
    return slot<sizeof factory_set/sizeof *factory_set ? &factory_set[slot] : 0;
}
int board_song_exists(unsigned slot) { return song_at(slot)!=0; }
unsigned board_song_is_score(unsigned slot)
{
    const Song *song=song_at(slot);
    return song && song->piano;
}
/* GLO rows: the saved study, tempo and clave of a slot. 0 for an empty slot. */
unsigned board_song_meta(unsigned slot,unsigned *study,unsigned *bpm,unsigned *reverse)
{
    const Song *song=song_at(slot);
    if(!song) return 0;
    *study=song->project[6]; *bpm=song->project[7]; *reverse=song->project[10];
    return 1;
}
const char *board_song_title(unsigned slot)
{
    /* A SAVE to another slot copies the current arrangement's title too. */
    const Song *song=song_at(slot);
    return song ? song->title : "EMPTY";
}
const char *board_groove_title(unsigned index)
{
    return index<BOARD_GROOVE_PRESETS?groove_titles[index]:"RHYTHM";
}
static unsigned recall_groove(unsigned index)
{
    unsigned playing=cp_status(CP_PLAYING);
#ifdef COMPAS_HANAN
    board_hanan_mode=HANAN_OFF;
    hanan_clear();
#endif
    cp_command(CP_STOP,0,0);
    unsigned reply=cp_command(CP_STUDY,index,0);
    if(reply) return reply;
    cp_command(CP_FEEL,0,0);
    board_piano_selected=piano_running=piano_hold=piano_guide=piano_perc_muted=groove_count_enabled=0;
    active_song=queued_song=SET_SLOTS;queued_root=12;queued_clave=2;
    active_part=queued_part=PART_NONE;
    queued_energy=3; next_bar=0;
    queued_groove=BOARD_GROOVE_PRESETS;
    phrases=remaining_phrases=0;
    active_rhythm=queued_rhythm=rhythm_origin=BOARD_CUSTOM_RHYTHM;
    overlay_hits=0;
    board_roll_keys=0;
    clear_recording();
    rack_sequence=board_rhythm_only=0;
    for(unsigned i=0;i<RACK_SOUNDS;++i) rack_pattern[i]=rack_pattern_accents[i]=0;
    for(unsigned i=0;i<CP_ROLES;++i) assigned[i]=0;
    reset_song_layers();
    kit=1; song_dirty=1;clear_rack();
    if(playing) cp_command(CP_START,0,0);
    return CP_OK;
}
static unsigned recall_song(unsigned slot)
{
    const Song *song=song_at(slot);
    if(!song) return BOARD_EMPTY;
    unsigned playing=cp_status(CP_PLAYING);
#ifdef COMPAS_HANAN
    board_hanan_mode=HANAN_OFF;
    hanan_clear();
#endif
    cp_command(CP_STOP,0,0);
    for(unsigned i=0;i<CP_PROJECT_BYTES;++i) cp_blob()[i]=song->project[i];
    unsigned reply=cp_restore(CP_PROJECT_BYTES);
    if(reply) return reply;
    kit=song->kit;queued_root=12;queued_clave=2;
    queued_groove=BOARD_GROOVE_PRESETS;
    active_rhythm=queued_rhythm=BOARD_CUSTOM_RHYTHM;
    overlay_hits=0;
    board_roll_keys=0;
    clear_recording();
    rack_sequence=board_rhythm_only=0; /* An unsaved drum layer never follows a new song silently. */
    for(unsigned i=0;i<CP_ROLES;++i) assigned[i]=song->assignment[i];
    cp_command(CP_SECTION,song->section,0);
    active_part=queued_part=PART_NONE;
    queued_energy=3; next_bar=0;
    active_song=(uint8_t)slot;
    song_dirty=1;
    phrases=remaining_phrases=song->phrases;
    board_piano_selected=song->piano;
    piano_guide=song->piano && song->guide_or_count;
    groove_count_enabled=!song->piano && song->guide_or_count;
    piano_running=playing && song->piano;
    piano_hold=piano_perc_muted=0;
    if(song->piano) {
        piano_start(song->piano,0);
        unsigned root=cp_status(CP_ROOT_NOTE);
        piano_transpose(root<6 ? (int)root : (int)root-12);
        piano_tempo=cp_status(CP_TEMPO);
    }
    if(!song->piano && rhythm_slots[slot].bytes[0]) recall_rhythm(slot);
    else reset_song_layers();
    if(playing) cp_command(CP_START,0,0);
    return CP_OK;
}
static unsigned next_occupied_slot(void)
{
    for(unsigned slot=active_song+1u;slot<SET_SLOTS;++slot)
        if(song_at(slot)) return slot;
    return SET_SLOTS;
}

static void advance_after_ending(void)
{
    unsigned slot=next_occupied_slot();
    if(slot<SET_SLOTS) result=recall_song(slot);
}

void board_groove_bar(void)
{
    if(next_bar && queued_part<PART_NONE && cp_status(CP_FILL_STATE)==0) {
        active_part=queued_part;
        queued_part=PART_NONE;
        next_bar=0;
    }
}
int board_song_boundary(unsigned completed_phrase)
{
    /* A fill requested on the last beat is deferred by the engine. Keep
     * the manual cue with it until that fill has played into a downbeat. */
    if((paired_rhythm(queued_rhythm) || queued_clave<2 || queued_root<12 || queued_groove<BOARD_GROOVE_PRESETS || queued_song<SET_SLOTS || queued_part<PART_NONE || queued_energy<3) &&
       cp_status(CP_FILL_STATE)==1 && active_part!=PART_END) return 0;
    if(queued_groove<BOARD_GROOVE_PRESETS) {
        result=recall_groove(queued_groove);
        return result==CP_OK;
    }
    unsigned target=queued_song;
    queued_song=SET_SLOTS;
    /* A solo percussion loop must not spend the backing song's repeat count.
     * Explicit song/part cues still belong to the performer and work below. */
    if(completed_phrase && !paired_rhythm(queued_rhythm) && queued_clave==2 && queued_root==12 && !board_drums_only() && target==SET_SLOTS && queued_part==PART_NONE && queued_energy==3 && active_part!=PART_INTRO &&
       active_part!=PART_END && remaining_phrases && --remaining_phrases==0)
        target=next_occupied_slot();
    if(target<SET_SLOTS) {
        result=recall_song(target);
        return result==CP_OK;
    }
    next_bar=0;
    if(queued_energy<3) {
        active_part=PART_NONE;
        cp_board_section(queued_energy);
        queued_energy=3;
    } else if(queued_part<PART_NONE) {
        active_part=queued_part;
        queued_part=PART_NONE;
    } else if(completed_phrase && active_part==PART_INTRO) active_part=PART_VERSE_A;
    else if(completed_phrase && active_part==PART_END) {
        cp_command(CP_STOP,0,0);
        active_part=PART_VERSE_A;queued_root=12;queued_clave=2;queued_rhythm=BOARD_CUSTOM_RHYTHM;
        advance_after_ending();
        return 1;
    }
    if(queued_root<12) { cp_board_root(queued_root);queued_root=12; }
    if(paired_rhythm(queued_rhythm)) load_rhythm(queued_rhythm);
    if(queued_clave<2) { cp_board_clave(queued_clave);align_clave_rhythm(queued_clave);queued_clave=2; }
    return 0;
}
unsigned board_groove_section(unsigned step, unsigned steps, unsigned original)
{
    switch(active_part) {
    case PART_INTRO: return step<steps/2u ? 2u : 0u;
    case PART_VERSE_A: return 0;
    case PART_VERSE_B: return step<steps/2u ? 0u : 1u;
    case PART_CHORUS: return 1;
    case PART_BREAK: case PART_END: return 2;
    default: return original;
    }
}
void board_mix_clip(void) { ++board_stats.rail_samples; }
int32_t board_rack_sample(void)
{
#ifdef COMPAS_HANAN
    int32_t sum=hanan_mix();
    if(board_hanan_mode==HANAN_SOLO) return sum;
#else
    int32_t sum=0;
#endif
    for(unsigned r=0;r<RACK_VOICES;++r) {
        Player *voice=&rack[r];
        if(voice->remaining)
            sum+=next727(voice) * voice->gain / 512;
    }
    return sum;
}
int board_piano_frame(int16_t *out)
{
    if(piano_running) {
        unsigned boundary=piano_step();
        if(boundary!=PIANO_INSIDE && queued_groove<BOARD_GROOVE_PRESETS) {
            result=recall_groove(queued_groove);
            return 0;
        } else if(boundary!=PIANO_INSIDE && queued_song<SET_SLOTS) {
            unsigned slot=queued_song;
            queued_song=SET_SLOTS;
            result=recall_song(slot);
            if(!board_piano_selected) return 0;
        } else if(boundary!=PIANO_INSIDE && queued_part<PART_NONE) {
            piano_jump(piano_part_bar(score_parts[queued_part]));
            queued_part=PART_NONE;
        } else if(boundary==PIANO_END) {
            cp_command(CP_STOP,0,0);
            piano_running=piano_hold=0;queued_root=12;
            advance_after_ending();
        } else if(boundary==PIANO_BAR && piano_hold) {
            unsigned bar=piano_bar();
            if(piano_can_hold()) piano_jump(bar-2);
            else if(bar>=piano_coda()) piano_hold=0;
        }
        if(boundary!=PIANO_INSIDE && piano_running && queued_root<12) {
            unsigned root=queued_root;
            cp_board_root(root);queued_root=12;
            piano_transpose(root<6 ? (int)root : (int)root-12);
        }
    }
    if(piano_running) {
        unsigned hit=piano_percussion();
        switch(hit && !piano_perc_muted ? hit : 0u) {
        case PIANO_MARACA: rack_hit(10,54); break;
        case PIANO_MARACA_ACCENT: rack_hit(10,70); break;
        case PIANO_CONGA_MUTE: rack_hit(2,62); break;
        case PIANO_CONGA_OPEN: rack_hit(3,58); break;
        }
    }
    int32_t sample=board_rack_sample();
    if(piano_running) sample+=piano_mix(piano_tempo,piano_guide);
    if(sample>32767 || sample<-32768) board_mix_clip();
    *out=(int16_t)(sample>32767 ? 32767 : sample<-32768 ? -32768 : sample);
    return 1;
}
static unsigned capture_song(unsigned slot)
{
    if(slot>=SET_SLOTS) return CP_RANGE;
    if(cp_status(CP_PLAYING)) return CP_STOP_FIRST;
    const Song *source=song_at(active_song);
    const char *title=source ? source->title : board_groove_title(cp_status(CP_STUDY_ID));
    Song *song=&captures[slot];
    cp_snapshot();
    for(unsigned i=0;i<CP_PROJECT_BYTES;++i) song->project[i]=cp_blob()[i];
    for(unsigned i=0;i<CP_ROLES;++i) song->assignment[i]=assigned[i];
    song->kit=kit;
    song->section=cp_status(CP_ACTIVE);
    song->phrases=phrases;
    song->piano=board_piano_selected;
    song->guide_or_count=board_piano_selected ? piano_guide : groove_count_enabled;
    for(unsigned i=0;i<sizeof song->title;++i) {
        song->title[i]=*title;
        if(*title) ++title;
    }
    capture_rhythm(slot);
    active_song=(uint8_t)slot;
    remaining_phrases=phrases;
#if defined(COMPAS_EMU_STORE) && !defined(COMPAS_NO_SHOW_STORE)
    /* Only a build that keeps shows marks one for the store. Without a store
     * the flag would never clear and board_ota_idle() would refuse updates. */
    save_ready=1;
#endif
    return BOARD_SESSION;
}

#ifdef COMPAS_EMU_STORE
unsigned board_store_snapshot(uint8_t *bytes)
{
    if(cp_status(CP_PLAYING) || queued || fault) return 0;
    for(unsigned slot=0;slot<SET_SLOTS;++slot) {
        const Song *song=song_at(slot);
        for(unsigned i=0;i<sizeof(Song);++i)
            bytes[slot*sizeof(Song)+i]=song?((const uint8_t *)song)[i]:0;
        if(!song) bytes[slot*sizeof(Song)]=SONG_HIDDEN;
    }
    return 1;
}
unsigned board_store_load(const uint8_t *bytes)
{
    unsigned valid=1;
    for(unsigned i=0;i<SET_SLOTS && valid;++i) {
        Song song;
        for(unsigned j=0;j<sizeof song;++j) ((uint8_t *)&song)[j]=bytes[i*sizeof song+j];
        if(!song.title[0]) continue;
        if(song.title[0]==SONG_HIDDEN) {
            for(unsigned j=1;j<sizeof song;++j)
                if(((const uint8_t *)&song)[j]) valid=0;
            continue;
        }
        valid=!song.title[18] && song.kit<=1 && song.section<=2 && song.phrases<=64 &&
              song.guide_or_count<=1 && piano_id_valid(song.piano,song.piano && song.guide_or_count);
        for(unsigned role=0;role<CP_ROLES;++role)
            if(song.assignment[role]>RACK_SOUNDS) valid=0;
        for(unsigned j=0;j<CP_PROJECT_BYTES;++j) cp_blob()[j]=song.project[j];
        if(cp_restore(CP_PROJECT_BYTES)!=CP_OK) valid=0;
    }
    cp_init(44100);
    if(valid)
        for(unsigned i=0;i<sizeof captures;++i) ((uint8_t *)captures)[i]=bytes[i];
    return valid;
}

unsigned board_store_pending(void) { return save_ready && !view.playing && !fault; }

unsigned board_store_pull(uint8_t *bytes)
{
    if(!save_ready || cp_status(CP_PLAYING) || fault) return 0;
    save_ready=0;
    clear_rack();
    for(unsigned i=0;i<sizeof captures;++i) bytes[i]=((const uint8_t *)captures)[i];
    return 1;
}

void board_store_result(unsigned code) { if(code) result=(uint8_t)code; }
#endif

static unsigned apply(BoardCommand cmd)
{
    if(cmd.command==BOARD_ENDING) {
        if(!piano_running) return CP_STOPPED;
        cmd=(BoardCommand){BOARD_PART,PART_END,0};
    }
    switch (cmd.command) {
    case BOARD_SPEED:
        if(cmd.a<60 || cmd.a>100) return CP_RANGE;
        speed=cmd.a;
        return CP_OK;
    case CP_CLAVE:
        if(cmd.a>1 || board_piano_selected || (cp_status(CP_STUDY_ID)!=2 && cp_status(CP_STUDY_ID)!=6)) return CP_RANGE;
        if(!cp_status(CP_PLAYING)) {
            queued_clave=2;unsigned reply=cp_command(CP_CLAVE,cmd.a,0);
            if(!reply) align_clave_rhythm(cmd.a);
            return reply;
        }
        if(paired_rhythm(queued_rhythm)) {
            unsigned target=board_clave_rhythm(queued_rhythm,cmd.a);
            queued_rhythm=target==active_rhythm?BOARD_CUSTOM_RHYTHM:(uint8_t)target;
        }
        queued_clave=cmd.a==cp_status(CP_REVERSE)?2:cmd.a;
        return CP_OK;
    case CP_ROOT:
        if(cmd.a>11 || !cp_status(CP_STUDY_ID)) return CP_RANGE;
        if(!cp_status(CP_PLAYING)) {
            queued_root=12;
            unsigned reply=cp_command(CP_ROOT,cmd.a,0);
            if(!reply && board_piano_selected) piano_transpose(cmd.a<6 ? (int)cmd.a : (int)cmd.a-12);
            return reply;
        }
        queued_root=cmd.a==cp_status(CP_ROOT_NOTE)?12:cmd.a;
        return CP_OK;
    case BOARD_GROOVE_CUE:
        if(cmd.a>=BOARD_GROOVE_PRESETS) return CP_RANGE;
        queued_song=SET_SLOTS;queued_part=PART_NONE;queued_energy=3;next_bar=0;
        queued_rhythm=BOARD_CUSTOM_RHYTHM;
        if(!cp_status(CP_PLAYING)) return recall_groove(cmd.a);
        queued_groove=!board_piano_selected && active_song==SET_SLOTS && cmd.a==cp_status(CP_STUDY_ID)
            ? BOARD_GROOVE_PRESETS : cmd.a;
        return CP_OK;
    case BOARD_CUE:
        if(cmd.a>=SET_SLOTS) return CP_RANGE;
        if(!song_at(cmd.a)) return BOARD_EMPTY;
        queued_groove=BOARD_GROOVE_PRESETS;
        queued_rhythm=BOARD_CUSTOM_RHYTHM;
        if(!cp_status(CP_PLAYING)) return recall_song(cmd.a);
        queued_song=cmd.a==active_song ? SET_SLOTS : cmd.a;
        queued_part=PART_NONE;
        queued_energy=3; next_bar=0;
        return CP_OK;
    case BOARD_PART:
        if(cmd.a>=PART_NONE || (board_piano_selected && !score_parts[cmd.a])) return CP_RANGE;
        if(piano_running && cmd.a==PART_END && piano_form()==PIANO_FORM_CODA) return CP_RANGE;
        queued_song=SET_SLOTS;queued_groove=BOARD_GROOVE_PRESETS;
        piano_hold=0;
        if(!cp_status(CP_PLAYING)) active_part=cmd.a;
        else queued_part=cmd.a;
        queued_energy=3;
        next_bar=!board_piano_selected && cp_status(CP_PLAYING) && cmd.b;
        return CP_OK;
    case CP_SECTION:
        if(board_piano_selected || cmd.a>=3) return CP_RANGE;
        queued_groove=BOARD_GROOVE_PRESETS;
        queued_song=SET_SLOTS;
        queued_part=PART_NONE;
        next_bar=0;
        if(cp_status(CP_PLAYING))
            queued_energy=cmd.a==board_part_energy(active_part,cp_status(CP_ACTIVE)) ? 3 : cmd.a;
        else {
            active_part=PART_NONE;
            cp_board_section(cmd.a);
        }
        return CP_OK;
    case BOARD_SAVE:
        return capture_song(cmd.a);
    case BOARD_GUIDE:
        if(!board_piano_selected || !piano_has_guide()) return CP_NO_ROLE;
        piano_guide=!piano_guide;
        return CP_OK;
    case BOARD_SCORE_PERC:
        if(!board_piano_selected || !piano_has_percussion()) return CP_NO_ROLE;
        piano_perc_muted=!piano_perc_muted;
        return CP_OK;
    case BOARD_HOLD:
        if(!piano_running) return CP_STOPPED;
        if(queued_part==PART_END || piano_form()==PIANO_FORM_CODA) return CP_RANGE;
        piano_hold=!piano_hold;
        return CP_OK;
    case BOARD_REPEAT:
        if(board_piano_selected || cmd.a>64) return CP_RANGE;
        phrases=remaining_phrases=cmd.a;
        return CP_OK;
    case BOARD_COUNT_IN:
        if(board_piano_selected || cmd.a>1) return CP_RANGE;
        if(cp_status(CP_PLAYING)) return CP_STOP_FIRST;
        groove_count_enabled=cmd.a;
        return CP_OK;
    case CP_START:
        if(!cp_status(CP_PLAYING)) {
#ifdef COMPAS_HANAN
            if(board_hanan_mode) hanan_clear();
#endif
            speed_phase=speed_primed=0;
            remaining_phrases=phrases;
            if(board_piano_selected) {
                piano_start(board_piano_selected,active_part==PART_NONE || active_part==PART_INTRO);
                unsigned root=cp_status(CP_ROOT_NOTE);
                piano_transpose(root<6 ? (int)root : (int)root-12);
                if(active_part<PART_NONE && active_part!=PART_INTRO)
                    piano_jump(piano_part_bar(score_parts[active_part]));
                active_part=PART_NONE;
                piano_running=1;
            } else if(groove_count_enabled) {
                cp_board_start_count();
            }
        }
        return cp_command(cmd.command,cmd.a,cmd.b);
    case BOARD_REC:
        if(board_piano_selected || cmd.a>RACK_SOUNDS) return CP_RANGE;
        record_armed=cmd.a!=0;
        if(record_armed) rack_sequence_sound=cmd.a-1u;
        return CP_OK;
    case BOARD_ROLL:
        if(board_piano_selected || cmd.a>ROLL_THIRTYSECOND) return CP_RANGE;
        board_roll_mode=cmd.a;
        if(!board_roll_mode) board_roll_keys=0;
        return CP_OK;
    case BOARD_ROLL_KEYS:
        if(cmd.b&128u) return CP_RANGE;
        board_roll_keys=board_roll_mode && !board_piano_selected ? cmd.a|((unsigned)cmd.b<<8) : 0;
        return CP_OK;
    case BOARD_RACK:
        if (cmd.a>=RACK_SOUNDS || cmd.b>127) return CP_RANGE;
        manual_rack_hit(cmd.a,cmd.b);
        return CP_OK;
    case BOARD_BASS_STYLE:
        if(cmd.a>BASS_ACID) return CP_RANGE;
        bass_style=cmd.a;
        cp_bass_set(bass_style,bass_cut,bass_bite);
        return CP_OK;
    case BOARD_BASS_SHAPE:
        if(cmd.a>100 || cmd.b>100) return CP_RANGE;
        bass_cut=cmd.a;
        bass_bite=cmd.b;
        cp_bass_set(bass_style,bass_cut,bass_bite);
        return CP_OK;
    case BOARD_OVERLAY:
        if(cmd.a>=4) return CP_RANGE;
        overlays^=1u<<cmd.a;
        return CP_OK;
    case BOARD_OVERLAY_LEVEL:
        if(cmd.a==BOARD_OVERLAY_ALL) {
            /* PERC macro: shift all four lanes together, b carries delta+128. */
            const int delta=(int)cmd.b-128;
            for(unsigned lane=0;lane<4;++lane) {
                int next=(int)overlay_levels[lane]+delta;
                overlay_levels[lane]=(uint8_t)(next<0 ? 0 : next>100 ? 100 : next);
            }
            return CP_OK;
        }
        if(cmd.a>=4 || cmd.b>100) return CP_RANGE;
        overlay_levels[cmd.a]=cmd.b;
        return CP_OK;
#ifdef COMPAS_HANAN
    case BOARD_HANAN_HIT:
        if(cmd.a>=4) return CP_RANGE;
        {
            static const uint8_t increments[]={157,128,192,160};
            hanan_hit(cmd.a,increments[cmd.a]);
        }
        return CP_OK;
    case BOARD_HANAN_CUE:
        if(cmd.a>=HANAN_PATTERNS) return CP_RANGE;
        if(board_hanan_mode && cp_status(CP_PLAYING)) {
            hanan_next=cmd.a==hanan_pattern?HANAN_PATTERNS:cmd.a;
        } else hanan_pattern=cmd.a;
        return CP_OK;
    case BOARD_HANAN_MODE:
        if(cmd.a>HANAN_SOLO || (cmd.a && board_piano_selected)) return CP_RANGE;
        if(cmd.a==HANAN_SOLO) {
            cp_board_clear_backing();
            clear_rack();
        }
        hanan_clear();
        board_hanan_mode=cmd.a;
        return CP_OK;
    case BOARD_HANAN_MOD:
        if(cmd.a>=KUMBIA_SETTINGS) return CP_RANGE;
        {
            uint8_t settings[KUMBIA_SETTINGS];
            board_kumbia_export(settings);
            settings[cmd.a]=cmd.b;
            return board_kumbia_restore(settings)?CP_OK:CP_RANGE;
        }
    case BOARD_HANAN_SAVE:
        if(cp_status(CP_PLAYING)) return CP_STOP_FIRST;
#if defined(COMPAS_EMU_STORE) && !defined(COMPAS_NO_SHOW_STORE)
        save_ready=1;
#endif
        return BOARD_SESSION;
#endif
    case BOARD_RHYTHM_CUE:
        if(cmd.a>=BOARD_RHYTHM_PATTERNS) return CP_RANGE;
        if(board_piano_selected || cp_status(CP_STEP_COUNT)!=32) return CP_NO_ROLE;
        queued_groove=BOARD_GROOVE_PRESETS;
        if(cp_status(CP_PLAYING)) {
            if(paired_rhythm(queued_rhythm)) queued_clave=2;
            queued_rhythm=cmd.a==active_rhythm?BOARD_CUSTOM_RHYTHM:cmd.a;
            if(paired_rhythm(cmd.a)) {
                unsigned direction=board_rhythm_direction(cmd.a);
                queued_clave=direction==cp_status(CP_REVERSE)?2:(uint8_t)direction;
            }
        }
        else load_rhythm(cmd.a);
        return CP_OK;
    case BOARD_SEQ_SELECT:
        if(cmd.a>=RACK_SOUNDS) return CP_RANGE;
        if(record_armed && cmd.a!=rack_sequence_sound) return CP_BUSY;
        rack_sequence_sound=cmd.a;
        return CP_OK;
    case BOARD_SEQ_ENABLE:
        if(cmd.a>2 || (cmd.a && board_piano_selected)) return CP_RANGE;
        if(cmd.a==2 || board_rhythm_only) cp_board_clear_backing();
        if(!cmd.a) { if(paired_rhythm(queued_rhythm)) queued_clave=2;queued_rhythm=BOARD_CUSTOM_RHYTHM; }
        rack_sequence=cmd.a;
        board_rhythm_only=cmd.a==2;
        return CP_OK;
    case BOARD_SEQ_STEP: {
        if(board_piano_selected || cmd.a>=RACK_SOUNDS || cmd.b>=32) return CP_RANGE;
        /* A 2/4 study has sixteen steps across the complete two-bar phrase. */
        if(cmd.b>=cp_status(CP_STEP_COUNT)) return CP_RANGE;
        if(paired_rhythm(queued_rhythm)) queued_clave=2;
        active_rhythm=queued_rhythm=BOARD_CUSTOM_RHYTHM;
        uint32_t bit=1u<<cmd.b;
        if(accent) {
            rack_pattern[cmd.a]|=bit;
            rack_pattern_accents[cmd.a]^=bit;
        } else {
            rack_pattern[cmd.a]^=bit;
            rack_pattern_accents[cmd.a]&=~bit;
        }
        if(!rack_sequence) rack_sequence=1;
        return CP_OK;
    }
    case BOARD_RACK_CONTROL:
        if(cmd.a>RACK_LEVEL || cmd.b>(cmd.a==RACK_LEVEL ? 100u : 127u)) return CP_RANGE;
        rack_controls[cmd.a]=cmd.b;
        return CP_OK;
    case BOARD_PAD_HITS: {
        unsigned hits=cmd.a | ((unsigned)cmd.b<<8);
        if(hits>>RACK_SOUNDS) return CP_RANGE;
        for(unsigned i=0;i<RACK_SOUNDS;++i)
            if(hits&(1u<<i)) manual_rack_hit(i,0);
        return CP_OK;
    }
    case BOARD_KIT:
        if (cmd.a>1) return CP_RANGE;
        if (cp_status(CP_PLAYING)) return CP_STOP_FIRST;
        kit=cmd.a;
        return CP_OK;
    case BOARD_ASSIGN: {
        /* Keep lead and timekeeping roles on their own engines. */
        unsigned study=cp_status(CP_STUDY_ID);
        if(cmd.a>=CP_ROLES || cmd.b>RACK_SOUNDS) return CP_RANGE;
        if(!(groove_assign[study]&(1u<<cmd.a))) return CP_NO_ROLE;
        if(cp_status(CP_PLAYING)) return CP_STOP_FIRST;
        assigned[cmd.a]=cmd.b;
        if(cmd.b) kit=1;
        return CP_OK;
    }
    case CP_STUDY: {
        unsigned reply=cp_command(cmd.command,cmd.a,cmd.b);
        if(!reply) {
            speed_phase=speed_primed=0;queued_root=12;queued_clave=2;
            queued_groove=BOARD_GROOVE_PRESETS;
            active_rhythm=queued_rhythm=BOARD_CUSTOM_RHYTHM;
            overlay_hits=groove_count_enabled=0;
            board_roll_keys=0;
            clear_recording();
            rack_sequence=board_rhythm_only=0;
            for(unsigned r=0;r<CP_ROLES;++r) assigned[r]=0;
            active_song=queued_song=SET_SLOTS;queued_root=12;queued_clave=2;
            active_part=queued_part=PART_NONE;
            queued_energy=3; next_bar=0;
            phrases=remaining_phrases=0;
            board_piano_selected=piano_running=piano_hold=0;
        }
        return reply;
    }
    case BOARD_ACCENT:
        if (cmd.a>1) return CP_RANGE;
        accent=cmd.a;
        return CP_OK;
    case CP_HIT:
        if(board_piano_selected) return CP_NO_ROLE;
        return cp_command(CP_HIT,cmd.a,accent ? 124 : 92);
    default:
        if(board_piano_selected && cmd.command!=CP_BPM) return CP_RANGE;
        return cp_command(cmd.command,cmd.a,cmd.b);
    }
}

static void service_commands(void)
{
    if (stop) {
        speed_phase=speed_primed=0;
        unsigned section=cp_status(CP_ACTIVE);
        cp_command(CP_STOP,0,0);
        if(active_song<SET_SLOTS) cp_command(CP_SECTION,section,0);
        clear_rack();
#ifdef COMPAS_HANAN
        hanan_clear();
#endif
        overlay_hits=0;
        board_roll_keys=0;
        clear_recording();
        queued_rhythm=BOARD_CUSTOM_RHYTHM;
        piano_running=piano_hold=0;
        queued_song=SET_SLOTS;queued_groove=BOARD_GROOVE_PRESETS;queued_root=12;queued_clave=2;
        queued_part=PART_NONE;
        queued_energy=3; next_bar=0;
        if(active_part==PART_END) active_part=PART_VERSE_A;
        read_pos=write_pos=queued=stop=0;
        result=CP_OK;
        return;
    }
    if (queued) result=CP_OK;
    for (unsigned i=0; queued && i<8; ++i) {
        unsigned reply=apply(mailbox[read_pos]);
        if (reply) result=(uint8_t)reply;
        read_pos=(read_pos+1u)&7u;
        --queued;
    }
}

static void publish_view(unsigned refresh)
{
    unsigned old_position=view.position;
    view.playing=cp_status(CP_PLAYING);
    view.position=cp_status(CP_POSITION);
    view.section=cp_status(CP_ACTIVE);
    view.pending=queued_energy<3 ? queued_energy : cp_status(CP_PENDING);
    view.fill=cp_status(CP_FILL_STATE);
    view.audible=cp_status(CP_AUDIBLE) || (!board_piano_selected && (overlays || rack_sequence));
    view.fault=fault;
    view.next_song=queued_song;
    view.next_groove=queued_groove;
    view.clave=(uint8_t)(cp_status(CP_REVERSE)|(queued_clave<<1));
    view.key=(uint8_t)(cp_status(CP_ROOT_NOTE)|(queued_root<<4));
    view.phrases=phrases;
    view.phrases_left=view.playing && !cp_board_counting() && !board_piano_selected && !board_drums_only() &&
        active_part!=PART_INTRO && active_part!=PART_END && remaining_phrases &&
        next_occupied_slot()<SET_SLOTS ? remaining_phrases : 0;
    refresh=refresh || song_dirty;
    song_dirty=0;
    view.song=active_song;
    view.part=active_part;
    view.next_part=queued_part;
    view.bass_style=bass_style;
    view.bass_cut=bass_cut;
    view.bass_bite=bass_bite;
    view.overlays=overlays;
    view.next_bar=next_bar;
    view.speed=speed;
    for(unsigned i=0;i<4;++i) view.overlay_levels[i]=overlay_levels[i];
    view.rack_strength=rack_controls[RACK_HIT_STRENGTH];
    view.rack_accent=rack_controls[RACK_ACCENT_STRENGTH];
    view.rack_level=rack_controls[RACK_LEVEL];
    view.rhythm=active_rhythm;view.next_rhythm=queued_rhythm;
    view.rack_sequence=rack_sequence;
    view.rack_sequence_sound=rack_sequence_sound;
    view.rack_steps=rack_pattern[rack_sequence_sound];
    view.rack_accents=rack_pattern_accents[rack_sequence_sound];
    view.piano=board_piano_selected;
    view.score_percussion=board_piano_selected && piano_has_percussion() ?
        SCORE_PERC_AVAILABLE|(piano_perc_muted?SCORE_PERC_MUTED:0) : 0;
    view.bass_hit=bass_hit;
#ifdef COMPAS_HANAN
    view.hanan_pattern=hanan_pattern;
    view.hanan_next=hanan_next;
    view.hanan_mode=board_hanan_mode;
    board_kumbia_export(view.kumbia);
#endif
    if(board_piano_selected) {
        view.score_ending=queued_part==PART_END;
        view.score_beats=piano_beats();
        view.score_has_guide=piano_has_guide();
        view.score_bar=piano_bar();
        view.score_form=piano_running ? piano_form() : PIANO_FORM_INTRO;
        view.score_hold=piano_hold;
        view.score_guide=piano_guide;
        view.position=piano_running ? piano_position() : 0;
        view.audible=1;
    } else {
        view.overlay_hits=view.playing ? overlay_hits : 0;
        view.rack_live=(uint8_t)(board_roll_mode|(record_armed?8u:0u));
        view.groove_count=(groove_count_enabled ? GROOVE_COUNT_ENABLED : 0u) |
            (cp_board_counting() ? GROOVE_COUNT_ACTIVE : 0u);
    }
    unsigned refresh_role=refresh || requested_role!=shown_role;
    if (refresh_role) {
        shown_role=requested_role;
        view.study=cp_status(CP_STUDY_ID);
        view.bpm=cp_status(CP_TEMPO);
        view.feel=cp_status(CP_FEEL_VALUE);
        view.muted=cp_status(CP_MUTES);
        view.solo=cp_status(CP_SOLO_ROLE);
        view.kit=kit;
        view.master=master;
        view.accent=accent;
        for (unsigned i=0;i<CP_ROLES;++i) {
            view.levels[i]=cp_level(i);
            view.assigned[i]=assigned[i];
        }
        view.steps=0;
        if(shown_role<CP_ROLES)
            for(unsigned i=0;i<32;++i)
                if(cp_step(shown_role,i)) view.steps|=1u<<i;
    }
    if(shown_role==BOARD_BASS_LINE_ROLE && !view.piano) {
        unsigned bar=cp_status(CP_STEP_COUNT)/2u;
        if(refresh_role || ((old_position^view.position)&bar)) {
            unsigned offset=view.position&bar;
            uint32_t notes=0;
            for(unsigned i=0;i<bar;++i) {
                unsigned note=cp_step(5,offset+i);
                if(note) notes|=(note==16u?1u:note==23u?3u:2u)<<(2u*i);
            }
            view.steps=notes;
        }
    }
}

/* Portable C7R1 rhythm: explicit little-endian fields, never a C struct dump.
 * Snapshot under the owner lock; CRC/validation run outside the audio lock. */
static uint32_t rhythm_word(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void rhythm_write(uint8_t *p,uint32_t word)
{ for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(word>>(8*i)); }
static uint32_t rhythm_crc(const uint8_t *p)
{
    uint32_t crc=~0u;
    for(unsigned i=0;i<BOARD_RHYTHM_BYTES-4;++i) {
        crc^=p[i];
        for(unsigned b=0;b<8;++b) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}
unsigned board_rhythm_export(uint8_t *bytes,unsigned size)
{
    if(size!=BOARD_RHYTHM_BYTES) return CP_BAD_PROJECT;
    uint32_t lock=board_lock();
    if(board_piano_selected || fault) { board_unlock(lock); return CP_NO_ROLE; }
    bytes[0]='C';bytes[1]='7';bytes[2]='R';bytes[3]='1';
    bytes[4]=(uint8_t)cp_status(CP_STEP_COUNT);
    bytes[5]=(uint8_t)cp_status(CP_TEMPO);bytes[6]=(uint8_t)cp_status(CP_FEEL_VALUE);
    for(unsigned i=0;i<3;++i) bytes[7+i]=rack_controls[i];
    bytes[10]=rack_sequence;bytes[11]=rack_sequence_sound;
    for(unsigned i=12;i<16;++i) bytes[i]=0;
    uint32_t mask=bytes[4]==16?0xffffu:~0u;
    for(unsigned i=0;i<RACK_SOUNDS;++i) {
        rhythm_write(bytes+16+8*i,rack_pattern[i]&mask);
        rhythm_write(bytes+20+8*i,rack_pattern_accents[i]&mask);
    }
    board_unlock(lock);
    rhythm_write(bytes+136,rhythm_crc(bytes));
    return CP_OK;
}
static unsigned rhythm_validate(const uint8_t *bytes,unsigned size)
{
    if(size!=BOARD_RHYTHM_BYTES || bytes[0]!='C' || bytes[1]!='7' || bytes[2]!='R' || bytes[3]!='1' ||
       (bytes[4]!=16 && bytes[4]!=32) || bytes[5]<60 || bytes[5]>180 || bytes[6]>100 ||
       bytes[7]>127 || bytes[8]>127 || bytes[9]>100 || bytes[10]>2 || bytes[11]>=RACK_SOUNDS ||
       bytes[12] || bytes[13] || bytes[14] || bytes[15] || rhythm_word(bytes+136)!=rhythm_crc(bytes))
        return CP_BAD_PROJECT;
    for(unsigned i=0;i<RACK_SOUNDS;++i) {
        uint32_t hits=rhythm_word(bytes+16+8*i),accents=rhythm_word(bytes+20+8*i);
        if((accents&~hits) || (bytes[4]==16 && (hits>>16))) return CP_BAD_PROJECT;
    }
    return CP_OK;
}
unsigned board_rhythm_import(const uint8_t *bytes,unsigned size)
{
    unsigned checked=rhythm_validate(bytes,size);
    if(checked) return checked;
    uint32_t lock=board_lock();
    unsigned error=fault?BOARD_FAULT:queued || stop?CP_BUSY:
        cp_status(CP_PLAYING)?CP_STOP_FIRST:board_piano_selected?CP_NO_ROLE:
        cp_status(CP_STEP_COUNT)!=bytes[4]?CP_RANGE:CP_OK;
    if(!error) {
        clear_rack();cp_board_clear_backing();
        speed_phase=speed_primed=0;
        active_rhythm=queued_rhythm=BOARD_CUSTOM_RHYTHM;
        rhythm_origin=BOARD_CUSTOM_RHYTHM;
        for(unsigned i=0;i<RACK_SOUNDS;++i) {
            rack_pattern[i]=rhythm_word(bytes+16+8*i);
            rack_pattern_accents[i]=rhythm_word(bytes+20+8*i);
        }
        for(unsigned i=0;i<3;++i) rack_controls[i]=bytes[7+i];
        rack_sequence=bytes[10];rack_sequence_sound=bytes[11];board_rhythm_only=rack_sequence==2;
        cp_command(CP_BPM,bytes[5],0);cp_command(CP_FEEL,bytes[6],0);
        publish_view(1);
    }
    board_unlock(lock);
    return error;
}

static void capture_rhythm(unsigned slot)
{
    RhythmSlot *saved=&rhythm_slots[slot];
    if(board_piano_selected || board_rhythm_export(saved->bytes,BOARD_RHYTHM_BYTES)) {
        for(unsigned i=0;i<sizeof *saved;++i) ((uint8_t *)saved)[i]=0;
        saved->origin=BOARD_CUSTOM_RHYTHM;
    } else {
        saved->origin=rhythm_origin;
        saved->bass_style=bass_style;saved->bass_cut=bass_cut;saved->bass_bite=bass_bite;
        saved->overlays=overlays;saved->accent=accent;saved->part=active_part;
        for(unsigned i=0;i<4;++i) saved->levels[i]=overlay_levels[i];
    }
}
static void recall_rhythm(unsigned slot)
{
    const RhythmSlot *saved=&rhythm_slots[slot];const uint8_t *b=saved->bytes;
    unsigned exact=saved->origin<BOARD_RHYTHM_PATTERNS;
    for(unsigned i=0;i<RACK_SOUNDS;++i) {
        rack_pattern[i]=rhythm_word(b+16+8*i);
        rack_pattern_accents[i]=rhythm_word(b+20+8*i);
        if(exact && (rack_pattern[i]!=rhythm_bank[saved->origin].hits[i] ||
                     rack_pattern_accents[i]!=rhythm_bank[saved->origin].accents[i])) exact=0;
    }
    /* Origin records lineage even after edits. Only an exact saved grid may
     * regain the preset name and its adjacent PRESETS browsing position. */
    active_rhythm=exact?saved->origin:BOARD_CUSTOM_RHYTHM;
    for(unsigned i=0;i<3;++i) rack_controls[i]=b[7+i];
    rack_sequence=b[10];rack_sequence_sound=b[11];board_rhythm_only=rack_sequence==2;
    rhythm_origin=saved->origin;
    bass_style=saved->bass_style;bass_cut=saved->bass_cut;bass_bite=saved->bass_bite;
    cp_bass_set(bass_style,bass_cut,bass_bite);
    overlays=saved->overlays;accent=saved->accent;active_part=saved->part;
    for(unsigned i=0;i<4;++i) overlay_levels[i]=saved->levels[i];
}
unsigned board_rhythm_slots_export(uint8_t *bytes,unsigned size)
{
    if(size!=BOARD_RHYTHM_SLOTS_BYTES) return CP_BAD_PROJECT;
    uint32_t lock=board_lock();
    unsigned error=fault?BOARD_FAULT:cp_status(CP_PLAYING)?CP_STOP_FIRST:queued || stop?CP_BUSY:CP_OK;
    if(!error) for(unsigned i=0;i<size;++i) bytes[i]=((const uint8_t *)rhythm_slots)[i];
    board_unlock(lock);return error;
}
unsigned board_rhythm_slots_load(const uint8_t *bytes,unsigned size)
{
    if(size!=BOARD_RHYTHM_SLOTS_BYTES) return CP_BAD_PROJECT;
    /* Boot/stopped-owner operation. Validate all slots before replacing any. */
    if(cp_status(CP_PLAYING)) return CP_STOP_FIRST;
    if(queued || stop || fault) return CP_BUSY;
    for(unsigned slot=0;slot<SET_SLOTS;++slot) {
        const uint8_t *b=bytes+slot*BOARD_RHYTHM_SLOT_BYTES;const Song *song=song_at(slot);
        if(!b[0]) {
            for(unsigned i=0;i<140;++i) if(b[i]) return CP_BAD_PROJECT;
            if(b[140]!=BOARD_CUSTOM_RHYTHM) return CP_BAD_PROJECT;
            for(unsigned i=141;i<BOARD_RHYTHM_SLOT_BYTES;++i) if(b[i]) return CP_BAD_PROJECT;
        } else {
            if(!song || song->piano || rhythm_validate(b,140) ||
               song->project[6]>=sizeof groove_steps || b[4]!=groove_steps[song->project[6]] ||
               b[5]!=song->project[7] || b[6]!=song->project[8] ||
               (b[140]>=BOARD_RHYTHM_PATTERNS && b[140]!=BOARD_CUSTOM_RHYTHM) ||
               b[141]>BASS_ACID || b[142]>100 || b[143]>100 || b[144]>15 ||
               b[145]>100 || b[146]>100 || b[147]>100 || b[148]>100 || b[149]>1 ||
               b[150]>PART_NONE) return CP_BAD_PROJECT;
        }
    }
    uint32_t lock=board_lock();
    if(cp_status(CP_PLAYING) || queued || stop || fault) { board_unlock(lock);return CP_BUSY; }
    for(unsigned i=0;i<size;++i) ((uint8_t *)rhythm_slots)[i]=bytes[i];
    board_unlock(lock);return CP_OK;
}

BoardView board_view(unsigned role)
{
    uint32_t previous=board_lock();
    requested_role=(uint8_t)role;
    BoardView copy=view;
    copy.result=result;
    board_unlock(previous);
    return copy;
}

RackFeedback board_rack_feedback(void)
{
    uint32_t lock=board_lock();
    RackFeedback copy=rack_feedback;
    board_unlock(lock);
    return copy;
}

static void render_half(int32_t *out)
{
    rack_pending=(RackFeedback){.hits=0};
    unsigned refresh=queued || stop || master_request!=NO_MASTER_REQUEST;
    /* Ramp each new gain over this DMA half. Steady blocks keep exact gain. */
    int32_t gain=(int32_t)master_gain*BOARD_FRAMES;
    int32_t step=0;
    if (master_request!=NO_MASTER_REQUEST) {
        master=master_request;
        master_request=NO_MASTER_REQUEST;
        result=CP_OK;
        unsigned target=master*256u/100u;
        step=(int32_t)target-(int32_t)master_gain;
        master_gain=target;
    }
    service_commands();
    if(board_piano_selected) piano_tempo=cp_status(CP_TEMPO);
    unsigned resample=speed!=100 || speed_primed;
    unsigned source_frames=resample?(speed_phase+BOARD_FRAMES*speed)/100u+(speed_primed?0u:2u):BOARD_FRAMES;
    cp_render(source_frames);
    if(rack_pending.hits) {
        rack_pending.serial=(uint8_t)(rack_feedback.serial+1u);
        rack_feedback=rack_pending;
    }
    publish_view(refresh);
    unsigned rack_count=0, sequence_count=0;
    for (unsigned r=0;r<RACK_VOICES;++r) rack_count += rack[r].remaining != 0;
    for (unsigned r=0;r<CP_ROLES;++r) sequence_count += sequence[r].remaining != 0;
    if (rack_count>board_stats.rack_peak) board_stats.rack_peak=rack_count;
    if (sequence_count>board_stats.sequence_peak) board_stats.sequence_peak=sequence_count;
    if (rack_count+sequence_count>board_stats.total_peak) board_stats.total_peak=rack_count+sequence_count;

    const int16_t *mono=cp_pcm();
    unsigned source=0;
    if(resample && !speed_primed) {
        speed_left=mono[source++];speed_right=mono[source++];speed_primed=1;
    }
    for (unsigned i=0;i<BOARD_FRAMES;++i) {
        int32_t sum;
        if(resample) {
            sum=speed_left+((int32_t)speed_right-speed_left)*speed_phase/100;
            speed_phase=(uint8_t)(speed_phase+speed);
            if(speed_phase>=100) {
                speed_phase-=100;
                speed_left=speed_right;speed_right=mono[source++];
            }
        } else sum=mono[i];
        gain+=step;
        sum=sum*(int32_t)((uint32_t)gain/BOARD_FRAMES)/256;
        if (sum>32767) { sum=32767; ++board_stats.rail_samples; }
        if (sum<-32768) { sum=-32768; ++board_stats.rail_samples; }
        out[2*i]=out[2*i+1]=sum*128;  /* retained Felucca Q15 to codec scaling */
    }
}

void fm1_alnk0_irq(void)
{
    const uint32_t start=board_ticks();
    const uint32_t pending=board_pending();
    board_ack_aux(pending);
    if (!(pending&BOARD_HALF_IRQ)) return;
    const unsigned half=board_free_half();
    if (!fault) render_half(board_dma[half]);
    else silence(board_dma[half]);
    board_ack_half();
    const unsigned late=board_free_half()!=half;
    const uint32_t elapsed=board_ticks()-start;
    const unsigned over=elapsed>=BOARD_TICKS_PER_FRAME*BOARD_FRAMES;
    ++board_stats.callbacks;
    if (elapsed>board_stats.max_ticks) board_stats.max_ticks=elapsed;
    board_stats.late_halves+=late;
    board_stats.deadline_misses+=over;
    if (late || over) {
        board_disable_output();
        fault=1;
        view.fault=1;
        view.playing=0;
        silence(board_dma[0]);
        silence(board_dma[1]);
    }
}
