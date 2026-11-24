#include "engine.h"
#include "score-test.inc"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CMD(cmd,a,b) assert(cp_command(CP_##cmd,a,b)==CP_OK)
#define S(k) cp_status(CP_##k)
static void put32(uint8_t *bytes,unsigned at,uint32_t value)
{ for(unsigned i=0;i<4;i++) bytes[at+i]=(uint8_t)(value>>(8u*i)); }
static uint32_t crc32(const uint8_t *bytes,unsigned count)
{
    uint32_t crc=~0u;
    for(unsigned i=0;i<count;i++) {
        crc^=bytes[i]; for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}
static void until(unsigned phrase,unsigned step) {
    for(unsigned i=0;i<4800 && (S(PHRASE)!=phrase || S(POSITION)!=step);i++) assert(cp_render(128)==CP_OK);
    assert(S(PHRASE)==phrase && S(POSITION)==step);
}
static void timing(void) {
    assert(cp_init(32000)==CP_RANGE);
    assert(cp_init(48000)==CP_OK);
    CMD(STUDY,3,0); CMD(BPM,123,0); CMD(START,0,0);
    for(unsigned n=0;n<900000;n++){
        assert(cp_render(1)==CP_OK);
        unsigned sixteenth=(unsigned)((uint64_t)n*123*4/(48000*60));
        assert(S(POSITION)==sixteenth%16 && S(PHRASE)==sixteenth/16);
    }
    assert(cp_render(257)==CP_RANGE);
}
static void compound_timing(unsigned id,unsigned steps) {
    assert(cp_init(48000)==CP_OK);
    CMD(STUDY,id,0); CMD(BPM,72,0); CMD(START,0,0);
    for(unsigned n=0;n<720000;n++) {
        assert(cp_render(1)==CP_OK);
        unsigned sixteenth=(unsigned)((uint64_t)n*72*6/(48000*60));
        assert(S(POSITION)==sixteenth%steps && S(PHRASE)==sixteenth/steps);
    }
    CMD(STOP,0,0); CMD(STUDY,id,0); CMD(START,0,0);
    until(0,steps-5); CMD(FILL,0,0);
    until(0,steps-4); assert(S(FILL_STATE)==2);
    until(1,0); assert(S(FILL_STATE)==0);
}
static void compound(void) {
    compound_timing(CP_TEST_STUDY_COMPOUND_6_8_ENGINE_FIXTURE,24);
    compound_timing(CP_TEST_STUDY_COMPOUND_6_8_HEMIOLA_ENGINE_FIXTURE,24);
    assert(cp_init(44100)==CP_OK); CMD(STUDY,CP_TEST_STUDY_COMPOUND_12_8_ENGINE_FIXTURE,0);
    assert(S(STEP_COUNT)==48 && cp_step(0,47)==0);
    CMD(STEP,0,47); assert(cp_step(0,47)==1);
    compound_timing(CP_TEST_STUDY_COMPOUND_12_8_ENGINE_FIXTURE,48);
}
static void performance(void) {
    assert(cp_init(44100)==CP_OK);
    assert(cp_command(CP_FILL,0,0)==CP_STOPPED);
    assert(cp_command(CP_MUTE,5,0)==CP_NO_ROLE);
    CMD(STUDY,2,0);
    unsigned clave=0, reverse=0, shell=0, shell_reverse=0;
    for(unsigned i=0;i<32;i++) if(cp_step(0,i)) clave|=1u<<i;
    assert(clave==((1u<<0)|(1u<<6)|(1u<<12)|(1u<<20)|(1u<<24)));
    for(unsigned i=0;i<32;i++) if(cp_step(2,i)) shell|=1u<<i;
    /* Steinholtz, Cuban Percussion Section, p. 4. The fill is a separate roll. */
    assert((shell&0xaaaaaaaau)==0);
    assert(shell==((1u<<0)|(1u<<4)|(1u<<6)|(1u<<10)|(1u<<14)|
                   (1u<<16)|(1u<<20)|(1u<<24)|(1u<<26)|(1u<<30)));
    CMD(CLAVE,1,0);
    for(unsigned i=0;i<32;i++) {
        if(cp_step(0,i)) reverse|=1u<<i;
        if(cp_step(2,i)) shell_reverse|=1u<<i;
    }
    assert((shell_reverse&0xaaaaaaaau)==0 && shell_reverse==((shell<<16)|(shell>>16)));
    assert(reverse==((clave<<16)|(clave>>16)));
    assert(cp_command(CP_STEP,0,0)==CP_RANGE);
    CMD(START,0,0); assert(cp_command(CP_CLAVE,0,0)==CP_STOP_FIRST);
    CMD(FILL,0,0);
    assert(S(FILL_STATE)==1 && cp_command(CP_SECTION,1,0)==CP_BUSY);
    until(0,27); assert(S(FILL_STATE)==1);
    until(0,28); assert(S(FILL_STATE)==2);
    /* Newest energy request wins; the playing section cancels. */
    CMD(SECTION,2,0); assert(S(PENDING)==2);
    CMD(SECTION,0,0); assert(S(PENDING)==3);
    CMD(SECTION,1,0); assert(S(PENDING)==1);
    until(1,0); assert(S(FILL_STATE)==0 && S(ACTIVE)==1);
    until(1,29); CMD(FILL,0,0);
    until(2,0); assert(S(FILL_STATE)==1);
    until(2,28); assert(S(FILL_STATE)==2);
    CMD(SOLO,2,0); for(unsigned i=0;i<16;i++) cp_render(256); assert(S(AUDIBLE)==0); /* shell rests at Lift */
    CMD(HIT,2,105); assert(S(AUDIBLE)==4); /* manual hit is audible */
    CMD(SOLO,3,0); assert(S(AUDIBLE)==8);
    CMD(MUTE,3,0); assert(S(AUDIBLE)==0);
    cp_render(256); for(unsigned i=0;i<256;i++) assert(cp_pcm()[i]==0);
    CMD(STOP,0,0); assert(S(POSITION)==0 && S(FILL_STATE)==0 && S(PENDING)==3);
    cp_render(256); for(unsigned i=0;i<256;i++) assert(cp_pcm()[i]==0);
}
static void project(void) {
    assert(cp_init(44100)==CP_OK);
    CMD(STUDY,3,0); CMD(ROOT,11,0); CMD(STEP,5,7); CMD(LEVEL,3,43); CMD(MUTE,2,0); CMD(FEEL,67,0);
    uint8_t bytes[CP_PROJECT_BYTES]; assert(cp_snapshot()==sizeof bytes);
    memcpy(bytes,cp_blob(),sizeof bytes);
    CMD(STUDY,1,0);
    memcpy(cp_blob(),bytes,sizeof bytes);
    cp_blob()[16]^=4; assert(cp_restore(sizeof bytes)==CP_BAD_PROJECT);
    assert(S(STUDY_ID)==1 && cp_level(3)==100);
    memcpy(cp_blob(),bytes,sizeof bytes);
    assert(cp_restore(sizeof bytes)==CP_OK && S(STUDY_ID)==3 && cp_step(5,7)==16 && cp_level(3)==43 && S(ROOT_NOTE)==11);
    cp_snapshot(); assert(memcmp(cp_blob(),bytes,sizeof bytes)==0);
    uint8_t legacy[CP_PROJECT_V1_BYTES]={0};
    memcpy(legacy,bytes,21); legacy[4]=1;
    for(unsigned role=0;role<CP_ROLES;role++)
        memcpy(legacy+21+4*role,bytes+21+6*role,4);
    legacy[45]=bytes[57];
    put32(legacy,CP_PROJECT_V1_BYTES-4,crc32(legacy,CP_PROJECT_V1_BYTES-4));
    CMD(STUDY,1,0); memcpy(cp_blob(),legacy,sizeof legacy);
    assert(cp_restore(sizeof legacy)==CP_OK && S(STUDY_ID)==3 && cp_step(5,7)==16 && S(ROOT_NOTE)==11);
    assert(cp_snapshot()==CP_PROJECT_BYTES && cp_blob()[4]==2);
    CMD(START,0,0); assert(cp_command(CP_ROOT,0,0)==CP_STOP_FIRST); assert(cp_restore(sizeof bytes)==CP_STOP_FIRST);
    int16_t audio[1024];
    for(unsigned i=0;i<4;i++){cp_render(256);memcpy(audio+256*i,cp_pcm(),512);}
    CMD(STOP,0,0); CMD(START,0,0);
    for(unsigned i=0;i<1024;i+=128){cp_render(128);assert(!memcmp(audio+i,cp_pcm(),256));}
}
int main(void){timing();compound();performance();project();puts("native: straight and compound clocks, phase and backpressure, mute/solo, project and deterministic PCM pass");}
