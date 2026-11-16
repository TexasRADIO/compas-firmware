/* Native replay of the golden-frame sessions.
 *
 * tools/golden-frames.mjs records every state-changing call the compiled
 * browser board receives (build/golden-trace.txt). This program links the
 * same board, panel, engine and stage sources natively, replays each session
 * from stdin (one trace line) and writes every kept framebuffer to stdout (115,200 bytes per frame, in
 * trace order). tools/golden-host.mjs hashes them against
 * tests/golden-frames.json, so gcc, clang and the wasm build must agree
 * pixel for pixel.
 *
 * Tokens: P<hex set> S<boot> A R D<tick> I<buttons>,<keys>,<tick> E<enc>,<delta>
 *         M<level> F (emit frame); "<token>*<n>" repeats a token n times. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* board-bench.c defines its own memcpy/memset for the freestanding wasm build.
 * macOS (and fortified glibc) define them as macros first, so clear those. */
#undef memcpy
#undef memset
#define memcpy bench_memcpy
#define memset bench_memset
#include "../web/board-bench.c"
#undef memcpy
#undef memset

static int hex(int c) { return c<='9'?c-'0':(c|32)-'a'+10; }

static int run(char *token)
{
    unsigned a=0,b=0,c=0;
    switch(token[0]) {
    case 'P': {
        size_t n=strlen(token+1)/2u;
        if(n!=sizeof prepared_set) { fprintf(stderr,"set size %zu\n",n); return 0; }
        for(size_t i=0;i<n;++i) prepared_set[i]=(uint8_t)(hex(token[1+2*i])<<4|hex(token[2+2*i]));
        return 1; }
    case 'S': return bench_boot((unsigned)strtoul(token+1,0,10))==1;
    case 'A': bench_audio(); return 1;
    case 'R': bench_read_view(); return 1;
    case 'D': bench_draw((uint32_t)strtoul(token+1,0,10)); return 1;
    case 'M': bench_master((unsigned)strtoul(token+1,0,10)); return 1;
    case 'I': if(sscanf(token+1,"%u,%u,%u",&a,&b,&c)!=3) return 0; bench_input(a,b,c); return 1;
    case 'E': { int index, delta; if(sscanf(token+1,"%d,%d",&index,&delta)!=2 || index<0 || index>6) return 0;
                enc[index]=delta; return 1; }
    case 'F': return fwrite(glass,sizeof glass,1,stdout)==1;
    }
    return 0;
}

int main(void)
{
    /* One session on stdin. A fresh process per session keeps the board's
     * static state identical to a fresh WebAssembly instance. */
    static char line[1<<20];
    if(!fgets(line,sizeof line,stdin) || !strchr(line,'\n')) { fprintf(stderr,"golden-replay: one session line on stdin\n"); return 2; }
    char *save=0;
    char *name=strtok_r(line," \n",&save);
    for(char *token=strtok_r(0," \n",&save);token;token=strtok_r(0," \n",&save)) {
        unsigned repeat=1;
        char *star=strchr(token,'*');
        if(star) { *star=0; repeat=(unsigned)strtoul(star+1,0,10); }
        for(unsigned i=0;i<repeat;++i)
            if(!run(token)) { fprintf(stderr,"%s: bad token %s\n",name,token); return 1; }
    }
    return 0;
}
