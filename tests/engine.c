#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CMD(cmd,a,b) assert(cp_command(CP_##cmd,a,b)==CP_OK)
#define S(k) cp_status(CP_##k)
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
    CMD(START,0,0); assert(cp_command(CP_ROOT,0,0)==CP_STOP_FIRST); assert(cp_restore(sizeof bytes)==CP_STOP_FIRST);
    int16_t audio[1024];
    for(unsigned i=0;i<4;i++){cp_render(256);memcpy(audio+256*i,cp_pcm(),512);}
    CMD(STOP,0,0); CMD(START,0,0);
    for(unsigned i=0;i<1024;i+=128){cp_render(128);assert(!memcmp(audio+i,cp_pcm(),256));}
}
int main(void){timing();performance();project();puts("native: rational clock, phase and backpressure, mute/solo, project and deterministic PCM pass");}
