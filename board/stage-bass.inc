/* V3 Bass page. All coordinates are in the shared 240-pixel stripe. */
#include "stage-response.inc"

static unsigned clipped(unsigned low,unsigned high,int value)
{
    return value<(int)low?low:value>(int)high?high:(unsigned)value;
}

static void ellipse(unsigned cx,unsigned cy,unsigned rx,unsigned ry,uint16_t color)
{
    if(!visible(cx-rx,cy-ry,2u*rx+1u,2u*ry+1u)) return;
    if(!ry) { paint(cx-rx,cy,2u*rx+1u,1,color); return; }
    unsigned square=rx*rx, ys=ry*ry;
    uint16_t pixel=display_word(color);
    for(unsigned y=max(cy-ry,tile.y); y<min(cy+ry+1u,tile.y+tile.h); ++y) {
        int dy=(int)y-(int)cy;
        unsigned limit=square*(ys-(unsigned)(dy*dy));
        unsigned x=rx;
        while(x*x*ys>limit) --x;
        span(cx-x,y,2u*x+1u,pixel);
    }
}

static void line_between(unsigned x,unsigned y,unsigned width,unsigned end,uint16_t ink)
{
    int dy=(int)end-(int)y;
    for(unsigned i=0;i<width;++i) {
        unsigned a=(unsigned)((int)y+dy*(int)i/(int)width);
        unsigned b=(unsigned)((int)y+dy*(int)(i+1u)/(int)width);
        paint(x+i,min(a,b),1,max(a,b)-min(a,b)+1u,ink);
    }
}

static void centered(unsigned center,unsigned y,const char *text,uint16_t ink,uint16_t paper)
{
    text=panel_word(text);
    unsigned chars=0;
    while(text[chars]) ++chars;
    unsigned w=chars*6u;
    print(center-w/2u,y,w,text,1,ink,paper,0);
}

static const uint16_t knob_ink[]={GOLD,PINK,CYAN,MINT};

static void value_pop(unsigned center,unsigned y,const char *text,unsigned knob)
{
    text=panel_word(text);
    unsigned chars=0;
    while(text[chars]) ++chars;
    unsigned w=chars*6u+8u;
    uint16_t ink=knob_ink[knob];
    unsigned x=clipped(0,240u-w,(int)center-(int)(w/2u));
    paint(x,y,w,12,ink);
    paint(x+w/2u-2u,y+12,4,3,ink);
    centered(x+w/2u,y,text,INK,ink);
}

static void bass_woofer(BoardView v,unsigned pump,unsigned flip)
{
    enum { AMBER=RGB565(200,116,42), TAN=RGB565(255,210,122) };
    unsigned acid=v.bass_style;
    unsigned squash=100;
    if(flip) {
        static const uint8_t squeeze[]={92,71,38,0,0,38,71,92};
        squash=squeeze[flip-1u];
        if(flip<=4) acid=!acid;
    }
    unsigned rings=20u+v.levels[5]/25u+pump;
    uint16_t warm=acid?PINK:ORANGE;
    paint(8,66,88,92,WOOD);
    paint(10,68,84,88,GRILL);
    for(unsigned i=0;i<4;++i)
        ellipse(i&1u?78u:26u,i&2u?138u:86u,2,2,DIM);
    ellipse(52,112,41,41,INK);
    ellipse(52,112,39,39,v.bass_style?PINK:ORANGE);
    ellipse(52,112,34,34,INK);
    if(pump) {
        ellipse(52,112,30,30,LINE);
        ellipse(52,112,29,29,warm);
        ellipse(52,112,28,28,INK);
    }
    ellipse(52,112,rings,rings*squash/100u,acid?LILAC:AMBER);
    ellipse(52,112,19,19u*squash/100u,INK);
    ellipse(52,112,15u+pump,(15u+pump)*squash/100u,acid?GOLD:TAN);
    ellipse(52,112,6,6u*squash/100u,INK);
    paint(10,149,84,9,INK);
    centered(52,149,v.bass_style?"ACID 303":"ENSEMBLE",v.bass_style?PINK:ORANGE,INK);
}

static unsigned bass_response(BoardView v,unsigned harmonic)
{
    unsigned cutoff=12u+104u*v.bass_cut/100u;
    unsigned f=harmonic*8u;
    unsigned delta=max(f,cutoff)-min(f,cutoff);
    unsigned resonance=(v.bass_style?4u:2u)*v.bass_bite;
    return stage_lowpass[v.bass_cut][harmonic-1u]+(delta<16u?resonance*(16u-delta)/16u:0u);
}

static void bass_spectrum(BoardView v)
{
    if(!visible(102,67,134,79)) return;
    uint16_t ink=v.bass_style?PINK:GOLD;
    if(visible(102,67,134,46)) {
        paint(102,67,134,46,INK);
        print(105,67,30,"TONE",1,GOLD,INK,0);
        print(137,67,30,"BITE",1,PINK,INK,0);
        unsigned last=0;
        for(unsigned k=1;k<=16;++k) {
            unsigned gain=bass_response(v,k), x=108+(k-1u)*8u;
            unsigned amp=v.bass_style || (k&1u) ? 48u : 17u;
            unsigned h=min(28u,amp*gain/(256u*k));
            paint(x,109u-h,6,h,k*8u>17u+137u*v.bass_cut/100u?DIM:ink);
            unsigned y=109u-min(32u,11u*gain/256u);
            if(k>1) line_between(x-5u,last,8,y,CREAM);
            last=y;
        }
        paint(112+104u*v.bass_cut/100u,79,1,30,CYAN);
    }
    if(visible(102,116,134,30)) {
        paint(102,116,134,30,INK);
        print(105,116,30,"WAVE",1,CYAN,INK,0);
        unsigned last=134;
        int8_t wave[64];
        cp_bass_preview(wave,v.bass_style,v.bass_cut,v.bass_bite);
        for(unsigned i=0;i<64;++i) {
            unsigned y=clipped(125,144,134-wave[i]/9);
            if(i) line_between(104+(i-1u)*2u,last,2,y,ink);
            last=y;
        }
    }
}

static void bass_line(BoardView v,unsigned pump)
{
    if(!visible(102,149,134,38)) return;
    paint(102,149,134,38,INK);
    print(105,149,29,"LINE",1,MINT,INK,0);
    print(137,149,24,"KEY",1,DIM2,INK,0);
    print(161,149,18,panel_root_name(board_root(v)),1,DIM2,INK,0);
    unsigned count=panel_beats(v.study)*2u;
    unsigned pos=v.position&(count-1u);
    for(unsigned i=0;i<count;++i) {
        unsigned note=(v.steps>>(i*2u))&3u, x=106+i*8u;
        paint(x,183,7,1,(i&3u)?LINE:DIM);
        if(note) paint(x,161+(3u-note)*5u,7,4,
                       i==pos && pump?CREAM:v.bass_style?PINK:ORANGE);
    }
    if(v.playing && !board_count_active(v)) paint(106+pos*8u,160,1,17,CYAN);
}
