#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void le(FILE *f, uint32_t x, unsigned bytes) {
    for(unsigned i=0;i<bytes;i++) fputc((int)((x>>(i*8))&255),f);
}
static uint64_t ns(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) exit(2);
    return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;
}
int main(int argc,char **argv) {
    if(argc!=2 && argc!=3) { fprintf(stderr,"Usage: %s STUDY [audition.wav]\n",argv[0]); return 2; }
    char *end;
    long id=strtol(argv[1],&end,10);
    if(end==argv[1] || *end || id<0 || id>255) return 2;
    const uint32_t rate=44100, total=44100*30;
    cp_init(rate);
    if(cp_command(CP_STUDY,(uint32_t)id,0)!=CP_OK) return 2;
    cp_command(CP_START,0,0);
    FILE *file=NULL;
    if(argc==3) {
        file=fopen(argv[2],"wb"); if(!file) return 2;
        fputs("RIFF",file); le(file,36+total*2,4); fputs("WAVEfmt ",file);
        le(file,16,4); le(file,1,2); le(file,1,2); le(file,rate,4);
        le(file,rate*2,4); le(file,2,2); le(file,16,2); fputs("data",file); le(file,total*2,4);
    }
    uint32_t peak=0, rails=0, blocks=0;
    uint64_t sum=0, max=0;
    for(uint32_t done=0;done<total;) {
        uint32_t n=total-done<128 ? total-done : 128;
        if(done/rate==6 && cp_status(CP_ACTIVE)==0 && cp_status(CP_PENDING)==3) cp_command(CP_SECTION,1,0);
        if(done/rate==13 && (done-n)/rate==12) cp_command(CP_FILL,0,0);
        if(done/rate==19 && (done-n)/rate==18) cp_command(CP_SECTION,2,0);
        if(done/rate==25 && (done-n)/rate==24) cp_command(CP_SECTION,0,0);
        uint64_t t=ns(); cp_render(n); t=ns()-t;
        sum+=t; blocks++; if(t>max) max=t;
        for(uint32_t i=0;i<n;i++){
            int x=cp_pcm()[i];
            uint32_t mag=(uint32_t)(x<0 ? -x : x);
            if(mag>peak) peak=mag;
            if(x==32767 || x==-32768) rails++;
            if(file) le(file,(uint16_t)x,2);
        }
        done+=n;
    }
    if(file) {
        int err=ferror(file);
        if(fclose(file) || err) return 2;
    }
    printf("{\"study\":%ld,\"frames\":%u,\"block\":128,\"stateBytes\":%u,\"meanNs\":%llu,\"peakNs\":%llu,\"peakPCM\":%u,\"railSamples\":%u}\n",
           id,total,cp_state_bytes(),(unsigned long long)(sum/blocks),(unsigned long long)max,peak,rails);
    return rails ? 1 : 0;
}
