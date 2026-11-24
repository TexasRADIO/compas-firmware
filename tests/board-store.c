/* Exercise the real board/store code against byte-interruptible NOR. */
#include "store.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[2*STORE_SECTOR],previous[sizeof flash],latest[sizeof flash];
static unsigned budget=UINT_MAX,written,operations,fail_read,corrupt_read;
static uint8_t old_set[BOARD_SET_BYTES],old_rhythms[BOARD_RHYTHM_SLOTS_BYTES];
static uint8_t new_set[BOARD_SET_BYTES],new_rhythms[BOARD_RHYTHM_SLOTS_BYTES];
static uint8_t old_v1_set[BOARD_SET_V1_BYTES];

uint32_t board_ticks(void) { return 0; }
uint32_t board_pending(void) { return BOARD_HALF_IRQ; }
void board_ack_aux(uint32_t p) { (void)p; }
unsigned board_free_half(void) { return 0; }
void board_ack_half(void) {}
void board_disable_output(void) {}
uint32_t board_lock(void) { return 0; }
void board_unlock(uint32_t p) { (void)p; }
static unsigned offset(uint32_t off,unsigned size)
{
    assert(off>=STORE_A && off+size<=STORE_B+STORE_SECTOR);
    return off-STORE_A;
}
int store_read(uint32_t off,void *out,unsigned size)
{
    unsigned start=offset(off,size);
    if(fail_read) {--fail_read;return 1;}
    memcpy(out,flash+start,size);
    if(corrupt_read && size==STORE_SHOW_BYTES) {((uint8_t *)out)[0]^=1;--corrupt_read;}
    return 0;
}
int store_erase(uint32_t off)
{
    unsigned start=offset(off,STORE_SECTOR);
    assert(!(off%STORE_SECTOR));++operations;
    for(unsigned i=0;i<STORE_SECTOR;++i) {
        if(written==budget) return 1;
        flash[start+i]=255;++written;
    }
    return 0;
}
int store_program(uint32_t off,const void *in,unsigned size)
{
    unsigned start=offset(off,size);const uint8_t *p=in;
    assert(size && size<=STORE_PAGE-off%STORE_PAGE);++operations;
    for(unsigned i=0;i<size;++i) {
        if(written==budget) return 1;
        assert((flash[start+i]&p[i])==p[i]);flash[start+i]&=p[i];++written;
    }
    return 0;
}
static void put(unsigned command,unsigned a,unsigned b)
{
    assert(board_submit((BoardCommand){command,a,b})==CP_OK);fm1_alnk0_irq();
}
static void boot(void) { board_init();store_boot(); }
static void export_state(uint8_t *set,uint8_t *rhythms)
{
    assert(board_store_snapshot(set));
    assert(board_rhythm_slots_export(rhythms,BOARD_RHYTHM_SLOTS_BYTES)==CP_OK);
}
static void verify_state(const uint8_t *set,const uint8_t *rhythms)
{
    uint8_t actual_set[BOARD_SET_BYTES],actual_rhythms[BOARD_RHYTHM_SLOTS_BYTES];
    export_state(actual_set,actual_rhythms);
    assert(!memcmp(set,actual_set,sizeof actual_set));
    assert(!memcmp(rhythms,actual_rhythms,sizeof actual_rhythms));
}
static void prepare_change(void)
{
    put(BOARD_CUE,1,0);put(BOARD_RHYTHM_CUE,5,0);put(BOARD_SEQ_STEP,0,7);
    put(BOARD_SEQ_ENABLE,2,0);put(BOARD_BASS_STYLE,BASS_ACID,0);
    put(BOARD_BASS_SHAPE,71,38);put(BOARD_OVERLAY,1,0);put(BOARD_OVERLAY_LEVEL,1,67);
    put(BOARD_SAVE,1,0);assert(board_view(0).result==BOARD_SESSION);
}
static uint32_t crc(const uint8_t *p,unsigned size,uint32_t gen)
{
    uint32_t c=~0u;
    for(unsigned i=0;i<size+4;++i) {
        c^=i<4?(gen>>(8*i))&255:p[i-4];
        for(unsigned bit=0;bit<8;++bit)c=(c>>1)^((c&1)?0xedb88320u:0u);
    }
    return ~c;
}
static uint32_t project_crc(const uint8_t *p,unsigned size)
{
    uint32_t c=~0u;
    for(unsigned i=0;i<size-4;++i) {
        c^=p[i];for(unsigned bit=0;bit<8;++bit)c=(c>>1)^((c&1)?0xedb88320u:0u);
    }
    return ~c;
}
static void make_v1_set(uint8_t *out,const uint8_t *in)
{
    for(unsigned slot=0;slot<SET_SLOTS;++slot) {
        const uint8_t *source=in+slot*(BOARD_SET_BYTES/SET_SLOTS);
        uint8_t *target=out+slot*(BOARD_SET_V1_BYTES/SET_SLOTS);
        memcpy(target,source,19);
        if(!source[0]) continue;
        if(source[0]==127) continue;
        uint8_t *project=target+19;
        memcpy(project,source+19,21);project[4]=1;
        for(unsigned role=0;role<6;++role)
            memcpy(project+21+4*role,source+19+21+6*role,4);
        project[45]=source[19+57];
        uint32_t sum=project_crc(project,55);
        for(unsigned i=0;i<4;++i) project[51+i]=(uint8_t)(sum>>(8*i));
        for(unsigned role=0;role<6;++role)
            target[74+role]=(source[81+role/2]>>(4*(role&1)))&15;
        unsigned options=source[84]|source[85]<<8;
        target[80]=options&1;target[81]=options>>1&3;target[82]=options>>3&127;
        target[83]=options>>10&31;target[84]=options>>15;
    }
}
static void header(unsigned start,const char *magic,uint32_t gen,unsigned size)
{
    uint32_t sum=crc(flash+start+STORE_PAGE,size,gen);
    memcpy(flash+start,magic,4);
    for(unsigned i=0;i<4;++i) {
        flash[start+4+i]=(uint8_t)(gen>>(8*i));flash[start+8+i]=(uint8_t)(sum>>(8*i));
    }
}
int main(void)
{
    memset(flash,255,sizeof flash);boot();
    put(BOARD_CUE,1,0);put(BOARD_RHYTHM_CUE,1,0);put(BOARD_SAVE,1,0);
    assert(store_commit()==BOARD_SAVED && operations==17);
    export_state(old_set,old_rhythms);memcpy(previous,flash,sizeof flash);
    assert(!memcmp(flash,"CPS4",4));
    boot();verify_state(old_set,old_rhythms);
    put(BOARD_CUE,1,0);assert(board_view(0).rack_sequence==1);
    prepare_change();export_state(new_set,new_rhythms);
    written=operations=0;assert(store_commit()==BOARD_SAVED);
    assert(operations==17 && written==STORE_SECTOR+STORE_SHOW_BYTES+12);
    memcpy(latest,flash,sizeof flash);boot();verify_state(new_set,new_rhythms);
    put(BOARD_CUE,1,0);assert(board_view(0).rack_sequence==2 && board_view(0).bass_style==BASS_ACID);

    /* Every byte-prefix interruption, including a partial erase and header. */
    const unsigned interruptions=STORE_SECTOR+STORE_SHOW_BYTES+12;
    for(unsigned direction=0;direction<2;++direction) for(unsigned cut=0;cut<interruptions;++cut) {
        const uint8_t *baseline=direction?latest:previous;
        unsigned kept=direction?STORE_SECTOR:0;
        memcpy(flash,baseline,sizeof flash);budget=UINT_MAX;boot();prepare_change();
        budget=cut;written=0;assert(store_commit()==BOARD_SESSION);
        assert(!memcmp(flash+kept,baseline+kept,STORE_SECTOR));
        budget=UINT_MAX;boot();verify_state(direction?new_set:old_set,direction?new_rhythms:old_rhythms);
    }
    memcpy(flash,latest,sizeof flash);flash[STORE_SECTOR+STORE_PAGE+BOARD_SET_BYTES+151+16]^=1;
    boot();verify_state(old_set,old_rhythms);
    /* Even a valid outer CRC must not make an invalid attachment load its songs. */
    memcpy(flash,latest,sizeof flash);flash[STORE_SECTOR+STORE_PAGE+STORE_SHOW_BYTES-1]=1;
    header(STORE_SECTOR,"CPS4",2,STORE_SHOW_BYTES);boot();verify_state(old_set,old_rhythms);
    memset(flash,255,sizeof flash);boot();
    assert(!board_store_pending() && board_song_exists(15));
    assert(store_commit()==0);

    /* Legacy song-only migration keeps the older sector until the new commit. */
    make_v1_set(old_v1_set,old_set);
    memset(flash,255,sizeof flash);memcpy(flash+STORE_PAGE,old_v1_set,sizeof old_v1_set);
    header(0,"CPS1",UINT_MAX,BOARD_SET_V1_BYTES);boot();
    uint8_t empty[BOARD_RHYTHM_SLOTS_BYTES];assert(board_rhythm_slots_export(empty,sizeof empty)==CP_OK);
    for(unsigned i=0;i<sizeof empty;++i)assert(empty[i]==(i%151==140?255:0));
    put(BOARD_CUE,1,0);assert(!board_view(0).rack_sequence);
    prepare_change();written=0;assert(store_commit()==BOARD_SAVED);
    assert(!memcmp(flash,"CPS1",4) && !memcmp(flash+STORE_SECTOR,"CPS4",4));
    for(unsigned i=4;i<8;++i)assert(flash[STORE_SECTOR+i]==0); /* generation wrap */
    export_state(new_set,new_rhythms);boot();verify_state(new_set,new_rhythms);

    /* Payload readback faults never publish a commit header. */
    memcpy(flash,previous,sizeof flash);boot();prepare_change();corrupt_read=1;
    written=0;assert(store_commit()==BOARD_SESSION);boot();verify_state(old_set,old_rhythms);
    memcpy(flash,previous,sizeof flash);boot();prepare_change();fail_read=1;
    written=0;assert(store_commit()==BOARD_SESSION);boot();verify_state(old_set,old_rhythms);

    /* Copy a named arrangement into an occupied slot, then replace the long
     * title with a short one. Reboot must retain both the name and its music. */
    memset(flash,255,sizeof flash);boot();
    put(BOARD_CUE,10,0);put(BOARD_SAVE,15,0);
    assert(store_commit()==BOARD_SAVED);boot();
    assert(!strcmp(board_song_title(15),"CDMX SONIDERA"));
    put(BOARD_CUE,15,0);assert(board_view(0).study==10);
    export_state(new_set,new_rhythms);
    memset(new_set,0,19);memcpy(new_set,"MY SHOW",7);
    assert(board_store_load(new_set));put(BOARD_CUE,0,0);
    put(BOARD_SPEED,75,0);assert(board_view(0).speed==75);
    put(BOARD_SAVE,15,0);put(BOARD_SAVE,15,0); /* in-place after copy */
    assert(store_commit()==BOARD_SAVED);boot();export_state(new_set,new_rhythms);
    assert(board_view(0).speed==100); /* live master treatment is not stored */
    assert(!strcmp(board_song_title(0),"MY SHOW") && !strcmp(board_song_title(15),"MY SHOW"));
    for(unsigned i=7;i<19;++i)assert(!new_set[15*(BOARD_SET_BYTES/SET_SLOTS)+i]);
    put(BOARD_CUE,15,0);assert(board_view(0).study==0);
    printf("{\"showBytes\":%u,\"bytePrefixInterruptions\":%u,\"legacyMigration\":true,\"generationWrap\":true,\"atomicInvalidAttachment\":true,\"readbackFaults\":true}\n",STORE_SHOW_BYTES,interruptions*2);
}
