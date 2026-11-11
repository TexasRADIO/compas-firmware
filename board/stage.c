/* Sonidero stage display. Included by the FM-1 adapter and the host raster test. */
#include "panel.h"
#include "piano.h"
#include "portrait.inc"
#include "wordmark.inc"
#include "subtitle.inc"
#include "stage-burst.inc"

static char face[FACE_LINES][FACE_COLS+1];
static uint16_t stripe[14*240];
static uint32_t cameo_started;
static unsigned cameo_active;
static const uint8_t letters[26][7]={
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static const uint8_t digits[10][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static unsigned glyph(char c,unsigned y)
{
    if (c>='A' && c<='Z') return letters[c-'A'][y];
    if (c>='0' && c<='9') return digits[c-'0'][y];
    if (c=='-') return y==3?31u:0u;
    if (c=='%') { static const uint8_t rows[]={25,26,2,4,8,11,19}; return rows[y]; }
    if (c=='+') return y==3?31u:y>=1&&y<=5?4u:0u;
    if (c==':') return y==2||y==4?4u:0u;
    if (c=='.') return y==6?4u:0u;
    if (c=='/') return 1u<<(y*4u/6u);
    if (c=='>') return 16u>>(y<4?y:6-y);
    if (c=='!') return y<5 || y==6 ? 4u : 0u;
    if (c==39) return y<2 ? 4u : 0u;
    if (c=='b') { static const uint8_t rows[]={16,16,22,25,17,18,28}; return rows[y]; }
    if (c=='#') return y==2||y==4?31u:10u;
    return 0;
}
static uint16_t display_word(uint16_t color)
{
    return (uint16_t)((color>>8)|(color<<8));
}
#define RGB565(r,g,b) (((r)>>3)<<11 | ((g)>>2)<<5 | ((b)>>3))
enum {
    INK=RGB565(11,7,16), PLUM=RGB565(23,16,31), CREAM=RGB565(255,248,236),
    GOLD=RGB565(255,230,0), PINK=RGB565(255,46,136), MINT=RGB565(61,255,122),
    LILAC=RGB565(180,107,255), RED=RGB565(255,60,64), DIM=RGB565(107,92,122),
    LINE=RGB565(44,34,56), RAY=RGB565(20,12,28), GRILL=RGB565(58,42,70), WOOD=RGB565(42,26,18),
    CYAN=RGB565(34,224,255), ORANGE=RGB565(255,122,26), BLUE=RGB565(127,178,255),
    /* Visual audit 2: DIM2 is secondary *text* (4.7:1 on INK; DIM stays for
     * graphics), RING outlines upcoming cones and empty cells, PAPER and
     * INKBLUE are the cartel sign card and headliner ink. */
    DIM2=RGB565(138,122,156), RING=RGB565(75,60,95), PAPER=RGB565(243,230,200),
    INKBLUE=RGB565(29,42,92)
};

#include "lang.inc"
/* Copy a language's format into out (12 bytes), putting digits where '#' is. */
static void lang_fill(char *out, const char *format, const char *digits)
{
    char d[4]; unsigned n=0;
    while(digits[n] && n<3u) { d[n]=digits[n]; ++n; }
    unsigned at=0;
    for(;*format && at<11u;++format) {
        if(*format=='#') for(unsigned i=0;i<n && at<11u;++i) out[at++]=d[i];
        else out[at++]=*format;
    }
    out[at]=0;
}

static void text_line(unsigned x, unsigned y, const char *line, unsigned columns,
                      uint16_t foreground, uint16_t background)
{
    const unsigned width=columns*11u;
    line=panel_word(line);
#ifdef STAGE_PRINT_TRACE
    STAGE_PRINT_TRACE(x,y,width,line,2u);
#endif
    const uint16_t fg=display_word(foreground), bg=display_word(background);
    lcd_sync();
    for(unsigned py=0;py<14;++py) {
        for(unsigned ch=0;ch<columns;++ch) {
            unsigned bits=glyph(line[ch],py/2u);
            for(unsigned px=0;px<11;++px)
                stripe[py*width+ch*11u+px]=px<10 && (bits&(16u>>(px/2)))?fg:bg;
        }
    }
    lcd_blit(x,y,width,14,stripe);
}

static void brand_line(unsigned x, unsigned y, const uint8_t *mask,
                       unsigned width, unsigned height, uint16_t color)
{
    const uint16_t foreground=display_word(color), background=display_word(PLUM);
    for(unsigned row=0;row<height;row+=12) {
        const unsigned rows=height-row<12 ? height-row : 12;
        lcd_sync();
        for(unsigned n=0;n<rows*width;++n) {
            unsigned i=row*width+n;
            stripe[n]=(mask[i/8u]&(128u>>(i&7u))) ? foreground : background;
        }
        lcd_blit(x,y+row,width,rows,stripe);
    }
}

static void portrait(unsigned x,unsigned y,unsigned size)
{
    for(unsigned row=0;row<size;row+=12) {
        const unsigned height=size-row<12 ? size-row : 12;
        lcd_sync();
        for(unsigned n=0;n<height*size;++n) {
            unsigned i=((row+n/size)*TRIBUTE_SIZE/size)*TRIBUTE_SIZE+
                       (n%size)*TRIBUTE_SIZE/size;
            unsigned index=tribute_pixels[i/2u];
            stripe[n]=display_word(tribute_colors[(i&1u)?index&15u:index>>4]);
        }
        lcd_blit(x,y+row,size,height,stripe);
    }
}

/* Batch the full-width bands to keep foreground transfers short at opening. */
static void band(unsigned y,unsigned height,uint16_t color)
{
    lcd_sync();
    for(unsigned i=0;i<sizeof stripe/sizeof *stripe;++i) stripe[i]=display_word(color);
    lcd_window(0,y,239,y+height-1u);
    for(unsigned row=0;row<height;row+=14) {
        unsigned rows=height-row<14 ? height-row : 14;
        lcd_data(stripe,rows*240u*2u);
    }
    lcd_sync();
}
static const uint16_t pad_colors[]={
    ORANGE,ORANGE,PINK,PINK,PINK,GOLD,GOLD,CYAN,CYAN,MINT,MINT,
    BLUE,BLUE,RED,LILAC,CREAM
};
static const char *const energies[]={"BASE","LIFT","BREAK"};
static const char *const upper_labels[]={"CON","MAR","TIM","BEL","BAS"};

static unsigned sign_glyph(unsigned char c,int y)
{
    unsigned accent=0;
    switch(c) {
    case 'a': c='A'; accent=1; break;
    case 'e': c='E'; accent=1; break;
    case 'i': c='I'; accent=1; break;
    case 'o': c='O'; accent=1; break;
    case 'u': c='U'; accent=1; break;
    case 'n': c='N'; accent=2; break;
    case 'v': c='U'; accent=3; break;
    }
    if(y==-2) return accent==1 ? 2 : accent==2 ? 10 : accent==3 ? 10 : 0;
    if(y==-1) return accent==1 ? 4 : accent==2 ? 20 : 0;
    return y>=0 && y<7 ? glyph((char)c,(unsigned)y) : 0;
}

typedef struct { uint8_t x,y,w,h; } Rect;
static Rect tile;

static unsigned max(unsigned a,unsigned b) { return a>b?a:b; }
static unsigned min(unsigned a,unsigned b) { return a<b?a:b; }

static unsigned visible(unsigned x,unsigned y,unsigned w,unsigned h)
{
    return x+w>tile.x && x<tile.x+tile.w && y+h>tile.y && y<tile.y+tile.h;
}

static void span(unsigned x,unsigned y,unsigned w,uint16_t pixel)
{
    unsigned start=max(tile.x,x), end=min(tile.x+tile.w,x+w);
    if(start>=end) return;
    uint16_t *out=stripe+(y-tile.y)*tile.w+start-tile.x;
    for(unsigned i=start;i<end;++i) *out++=pixel;
}

static void paint(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color)
{
    if(!visible(x,y,w,h)) return;
    uint16_t pixel=display_word(color);
    for(unsigned row=max(y,tile.y);row<min(y+h,tile.y+tile.h);++row) span(x,row,w,pixel);
}

static void point(unsigned x,unsigned y,uint16_t pixel)
{
    stripe[(y-tile.y)*tile.w+x-tile.x]=pixel;
}

static unsigned sign_mask(unsigned char c,int y,unsigned scale)
{
    if(y< -2*(int)scale || y>=7*(int)scale || c==' ') return 0;
    unsigned bits=sign_glyph(c,(y+2*(int)scale)/(int)scale-2);
    unsigned mask=0;
    for(unsigned x=0;x<5;++x)
        if(bits&(16u>>x)) mask|=((1u<<scale)-1u)<<(x*scale);
    return mask;
}

/* A glyph row is at most ten pixels, so fill, outline and extrusion fit
 * in one word. The stripe stays shared by all drawing primitives. */
static void print_text(unsigned x,unsigned y,unsigned width,const char *s,unsigned scale,
                       uint16_t fg,uint16_t bg,uint16_t shadow,unsigned translate)
{
    unsigned pitch=scale==1?6u:11u, height=9u*scale+(shadow?3u:0u);
    if(!visible(x,y,width,height)) return;
    if(translate) s=panel_word(s);
#ifdef STAGE_PRINT_TRACE
    STAGE_PRINT_TRACE(x,y,width,s,scale);
#endif
    unsigned count=0;
    while(count<width/pitch && s[count]) ++count;
    paint(x,y,width,height,bg);
    fg=display_word(fg);
    if(shadow) shadow=display_word(shadow);
    const uint16_t outline=display_word(INK);
    unsigned start=max(x,tile.x), end=min(x+width,tile.x+tile.w);
    for(unsigned py=max(y,tile.y);py<min(y+height,tile.y+tile.h);++py) {
        int gy=(int)(py-y)-(int)(2u*scale);
        for(unsigned ch=(start-x)/pitch;ch<count && x+ch*pitch<end;++ch) {
            unsigned char c=(unsigned char)s[ch];
            unsigned fill=sign_mask(c,gy,scale), edge=0, depth=0;
            if(shadow) {
                edge=sign_mask(c,gy-1,scale)|fill|sign_mask(c,gy+1,scale);
                edge|=(edge<<1)|(edge>>1);
                depth=(sign_mask(c,gy-1,scale)<<1)|(sign_mask(c,gy-2,scale)<<2)|
                      (sign_mask(c,gy-3,scale)<<3);
            }
            unsigned left=x+ch*pitch;
            for(unsigned px=max(left,start);px<min(left+pitch,end);++px) {
                unsigned bit=1u<<(px-left);
                if((fill|edge|depth)&bit) point(px,py,fill&bit?fg:edge&bit?outline:shadow);
            }
        }
    }
}

static void print(unsigned x,unsigned y,unsigned width,const char *s,unsigned scale,
                  uint16_t fg,uint16_t bg,uint16_t shadow)
{
    print_text(x,y,width,s,scale,fg,bg,shadow,1);
}

static void decimal(char s[4],unsigned n)
{
    s[0]=(char)('0'+n/100);
    s[1]=(char)('0'+n/10%10);
    s[2]=(char)('0'+n%10);
    s[3]=0;
}

static void circle(unsigned x,unsigned y,unsigned size,uint16_t fg,uint16_t bg,unsigned style,unsigned value)
{
    if(!visible(x,y,size,size)) return;
    static const int8_t dx[]={-4,-5,-5,-5,-4,-3,-2,-1,1,2,3,4,5,5,5,4};
    static const int8_t dy[]={4,2,0,-1,-3,-4,-5,-5,-5,-5,-4,-3,-1,0,2,4};
    unsigned at=(value*15u+50u)/100u;
    int radius=(int)size/2, square=radius*radius;
    uint16_t paper=display_word(bg), ink=display_word(INK), on=display_word(fg);
    for(unsigned py=max(y,tile.y);py<min(y+size,tile.y+tile.h);++py)
        for(unsigned px=max(x,tile.x);px<min(x+size,tile.x+tile.w);++px) {
            int a=(int)(px-x)-radius,b=(int)(py-y)-radius,d=a*a+b*b;
            uint16_t pixel=paper;
            if(d<=square) {
                if(style==2) {
                    pixel=d>square*3/4?display_word(ORANGE):d>square/3?display_word(PINK):
                        d>square/18?display_word(GOLD):ink;
                } else if(style==1) {
                    if(value) pixel=d>=square*3/4 || (d<square/3 && d>5)?on:ink;
                    else pixel=d>=(radius-1)*(radius-1) ||
                        (d>=square/5 && d<=square/3) || d<=4?on:ink;
                } else if(style==3) {
                    /* Greyed woofer: the same cone rings in the off palette. */
                    pixel=d>square*3/4?display_word(LINE):d>square/3?display_word(DIM):
                        d>square/18?display_word(LINE):ink;
                } else if(d>=(radius-1)*(radius-1)) pixel=on;
                else {
                    /* Needle: within 1 px of the centre-to-value line, on the
                     * value side, inside the ring, so the ring cannot hide it. */
                    int cross=a*dy[at]-b*dx[at], along=a*dx[at]+b*dy[at];
                    if(along>=0 && cross*cross<=2*(dx[at]*dx[at]+dy[at]*dy[at]))
                        pixel=display_word(CREAM);
                }
            }
            point(px,py,pixel);
        }
}

static void burst_size(unsigned x,unsigned y,unsigned size,unsigned phase,unsigned value,
                       uint16_t fg,const char *unit)
{
    if(!visible(x,y,size,size)) return;
    const uint8_t (*shape)[6]=stage_burst[phase&3u];
    uint16_t on=display_word(fg), off=display_word(INK);
    for(unsigned py=max(y,tile.y);py<min(y+size,tile.y+tile.h);++py) {
        unsigned row=(py-y)*43u/size;
        for(unsigned px=max(x,tile.x);px<min(x+size,tile.x+tile.w);++px) {
            unsigned j=(px-x)*43u/size;
            point(px,py,shape[row][j/8u]&(1u<<(j&7u))?on:off);
        }
    }
    unsigned mid=size/2u;
    if(value) {
        char s[4]; decimal(s,value);
        print(x+mid-8,y+mid-10,18,s+(value<100),1,INK,fg,0);
    }
    print(x+mid-8,y+mid-(value?0u:6u),18,unit,1,INK,fg,0);
}

static void marquee(const uint16_t lamps[30])
{
    if(!visible(0,0,240,7)) return;
    for(unsigned i=0;i<30;++i) {
        unsigned x=2+i*8u;
        uint16_t color=lamps[i];
        paint(x+1,1,3,1,color);
        paint(x,2,5,3,color);
        paint(x+1,5,3,1,color);
    }
}

/* The sticker is fixed. Its highlight chases the tips on each beat,
 * avoiding a complete badge transfer on the critical clock update. */
static void sticker_step(unsigned beat)
{
    static const uint8_t x[]={223,220,212,204,201,204,212,220};
    static const uint8_t y[]={78,69,66,69,78,87,90,87};
    if(beat<8) paint(x[beat],y[beat],3,3,ORANGE);
}
static void tabs(unsigned page)
{
    static const uint8_t ids[]={PANEL_HOME,PANEL_BASS,PANEL_PERC,PANEL_RACK
#ifdef COMPAS_HANAN
        ,PANEL_HANAN
#endif
    };
    static const char *const names[]={"BAND","BASS","PERC","727"
#ifdef COMPAS_HANAN
        ,"KUMBIA"
#endif
    };
    unsigned width=240u/sizeof ids;
    for(unsigned i=0;i<sizeof ids;++i) {
        uint16_t bg=page==ids[i]?GOLD:PLUM;
        paint(4+i*width,9,width-6,14,bg);
        print((width==60?17u:10u)+i*width,10,width==60?30u:36u,names[i],1,page==ids[i]?INK:DIM2,bg,0);
    }
}

static void background(unsigned rays)
{
    uint16_t ray=display_word(RAY), plain=display_word(INK);
    uint16_t *dst=stripe;
    for(unsigned y=tile.y;y<tile.y+tile.h;++y) {
        if(!rays || y<58 || y>=188) {
            for(unsigned x=0;x<tile.w;++x) *dst++=plain;
            continue;
        }
#ifdef COMPAS_USB
        /* Run ends share the same exact pixels as the lock's bit masks. */
        const uint8_t *run=stage_ray_runs[y-58u];
        unsigned color=*run++, x=tile.x, end=x+tile.w;
        while(*run<=x) { ++run; color^=1u; }
        while(x<end) {
            unsigned next=min(end,*run++);
            uint16_t pixel=color?ray:plain;
            while(x<next) { *dst++=pixel; ++x; }
            color^=1u;
        }
#else
        const uint8_t *mask=stage_rays[y-58u];
        if(tile.x==0 && tile.w==240) {
            for(unsigned x=0;x<30;++x) {
                unsigned bits=mask[x];
                for(unsigned bit=1;bit<256;bit<<=1) *dst++=bits&bit?ray:plain;
            }
        } else for(unsigned x=tile.x;x<tile.x+tile.w;++x) *dst++=mask[x/8u]&(1u<<(x&7u))?ray:plain;
#endif
    }
}

static void cameo_art(void)
{
    if(!visible(209,28,24,24)) return;
    for(unsigned y=max(28,tile.y);y<min(52,tile.y+tile.h);++y)
        for(unsigned x=max(209,tile.x);x<min(233,tile.x+tile.w);++x) {
            unsigned i=(y-28u)*TRIBUTE_SIZE/24u*TRIBUTE_SIZE+(x-209u)*TRIBUTE_SIZE/24u;
            unsigned index=tribute_pixels[i/2u];
            point(x,y,display_word(tribute_colors[(i&1u)?index&15u:index>>4]));
        }
}

/* Offset posters mixed inks on the press: yellow runs through green into blue
 * (dossier 03, UNAM thesis). One colour per column, 3 rows each edge. */
static uint16_t mixed_ink(unsigned x)
{
    unsigned t=x*2u, r,g,b;              /* t: 0..478 */
    if(t<240u) { r=255u-t*194u/240u; g=230u+t*25u/240u; b=t*122u/240u; }      /* yellow -> green */
    else { t-=240u; r=61u-t*19u/240u; g=255u-t*148u/240u; b=122u+t*92u/240u; } /* green -> blue */
    return (uint16_t)RGB565(r,g,b);
}
static void ink_band(unsigned y)
{
    if(!visible(0,y,240,3)) return;
    for(unsigned row=max(y,tile.y);row<min(y+3u,tile.y+tile.h);++row)
        for(unsigned x=tile.x;x<tile.x+tile.w;++x) point(x,row,display_word(mixed_ink(x)));
}
static void title_sign(const char *title,unsigned cameo,unsigned flash,unsigned dim)
{
    /* Cartel card: warm paper and an ink-blue headliner with the stepped pink
     * extrusion. BREAK dims to the night stage; a landing flashes cream. */
    const uint16_t paper=flash ? CREAM : dim ? PLUM : PAPER;
    paint(0,27,240,28,paper);
    if(flash) {
        paint(0,24,240,1,GOLD); paint(0,25,240,1,GOLD); paint(0,26,240,1,ORANGE);
        paint(0,55,240,1,ORANGE); paint(0,56,240,1,GOLD); paint(0,57,240,1,GOLD);
    } else if(dim) {
        paint(0,24,240,3,LILAC); paint(0,55,240,3,LILAC);
    } else { ink_band(24); ink_band(55); }
    print_text(6,31,cameo?198:234,title,2,flash?INK:dim?LILAC:INKBLUE,paper,dim?INK:PINK,0);
    if(cameo) {
        paint(204,26,34,30,PINK);
        paint(208,27,26,26,INK);
        for(unsigned i=0;i<5;++i) {
            paint(215-i,25-i,1,1,DIM);
            paint(228+i,25-i,1,1,DIM);
        }
        cameo_art();
    }
}

static unsigned next_slot(BoardView v)
{
    if(v.next_song<SET_SLOTS) return v.next_song;
    for(unsigned i=v.song<SET_SLOTS?v.song+1u:0u;i<SET_SLOTS;++i)
        if(board_song_exists(i)) return i;
    return SET_SLOTS;
}

static unsigned current_part(BoardView v)
{
    if(v.piano && (v.playing || v.part==PART_NONE)) {
        static const uint8_t form[]={PART_INTRO,PART_INTRO,PART_VERSE_A,PART_CHORUS,PART_END};
        return form[v.score_form];
    }
    return v.part<PART_NONE ? v.part : v.section==2 ? PART_BREAK :
        v.section==1 ? PART_CHORUS : PART_VERSE_A;
}

static unsigned note_color(PanelKeys map,unsigned key)
{
    uint32_t bit=1u<<key;
    return map.queued&bit ? PINK : map.active&bit ? GOLD : map.enabled&bit ? MINT :
        map.parts&bit ? 0xffffu : map.upper&bit ? LILAC : DIM;
}

static void labelled_keys(PanelKeys map,BoardView v,unsigned flash,unsigned pad_flash)
{
    print(1,132,30,"DRUMS",1,CYAN,INK,0);
    print(210,132,30,"DRUMS",1,CYAN,INK,0);
    if(v.piano) print(87,132,66,"SCORE TOOLS",1,LILAC,INK,0);
    else print(102,132,36,"LAYERS",1,MINT,INK,0);
    print(90,175,60,"SONG PARTS",1,CREAM,INK,0);

    for(unsigned i=0;i<15;++i) {
        unsigned x=i<7 ? 2+i*4 : 209+(i-7)*4;
        /* Upper and lower mini keys mirror the two banks on the panel. */
        unsigned upper=(0x4a2au>>i)&1u;
        paint(x,144,3,upper?18:29,GRILL);
        paint(x,144,3,upper?12:21,pad_flash==i+1u?0xffffu:pad_colors[i]);
    }
    /* Family codes: Bongo Conga Timbal | Agogo Shaker Whistle Quijada/chime. */
    print(3,177,24,"BCT",1,DIM2,INK,0);
    print(212,177,24,"ASWQ",1,DIM2,INK,0);
    for(unsigned i=0;i<6;++i) {
        unsigned x=32+i*29;
        unsigned bg=note_color(map,panel_part_keys[i]);
        char n[]={(char)('1'+i),0};
        paint(x,143,28,31,(uint16_t)bg);
        if(flash && (map.active&(1u<<panel_part_keys[i]))) paint(x,143,28,2,0xffffu);
        print(x+11,155,6,n,1,bg==DIM?CREAM:INK,(uint16_t)bg,0);
        const char *label=v.piano && i==5?"COD":v.piano && i==1?"VRS":panel_groove_part_name(v.study,i,1);
        if(v.piano==PIANO_SCORE_CIELITO && i==0) label="P/U";
        if(v.piano==PIANO_SCORE_CIELITO && i==5) label="END";
        if(v.piano==PIANO_SCORE_CUCARACHA && i==0) label="BEG";
        if(v.piano==PIANO_SCORE_JURAME) {
            static const char *const jurame[]={"BEG","REF","","ANS","","CAD"};
            label=jurame[i];
        }
        if(v.piano==PIANO_SCORE_TEODORA) {
            static const char *const teodora[]={"P/U","CAL","","ECH","","END"};
            label=teodora[i];
        }
        print(x+5,164,18,label,1,bg==DIM?CREAM:INK,(uint16_t)bg,0);
    }
    for(unsigned i=0;i<5;++i) {
        unsigned x=49+i*29, bg=note_color(map,panel_layer_keys[i]);
        const char *label=v.piano?(i==0?"GDE":i==1?"HLD":i==2 && v.score_percussion?"BOL":""):upper_labels[i];
        paint(x,143,23,15,INK);
        paint(x+1,143,21,14,(uint16_t)bg);
        print(x+3,142,18,label,1,INK,(uint16_t)bg,0);
        if(map.upper&(1u<<panel_layer_keys[i]))
            print(x+9,150,6,map.enabled&(1u<<panel_layer_keys[i])?"+":"-",1,INK,(uint16_t)bg,0);
    }
}

static void score_shift(char text[4],unsigned root)
{
    text[0]=root>=6 ? '-' : '+';
    text[1]='0'; text[2]=(char)('0'+(root>=6 ? 12-root : root));text[3]=0;
}

static unsigned overlay_average(BoardView v)
{
    return (v.overlay_levels[0]+v.overlay_levels[1]+v.overlay_levels[2]+v.overlay_levels[3]+2u)/4u;
}

#include "stage-bass.inc"

static void footer(const Panel *p, unsigned page,BoardView v,unsigned redraw,BoardView old,unsigned old_slot,unsigned old_sound,unsigned hot)
{
    const char *labels[4]={"TEMPO","FEEL","BASS","PERC"};
    unsigned val[4]={v.bpm,v.feel,v.levels[5],overlay_average(v)};
    unsigned before[4]={old.bpm,old.feel,old.levels[5],overlay_average(old)};
    unsigned max[4]={180,100,100,100};
    if(page==PANEL_HOME && v.piano) {
        if(v.piano==PIANO_SCORE_TEODORA) labels[0]="6/8";
        labels[1]=v.score_has_guide?"GUIDE":""; labels[2]="HOLD"; labels[3]="SHIFT";
        val[1]=v.score_guide; val[2]=v.score_hold; max[1]=max[2]=1;
        before[1]=old.score_guide; before[2]=old.score_hold;
        val[3]=board_root(v); before[3]=board_root(old);max[3]=11;
    } else if(page==PANEL_BASS) {
        labels[0]="TONE"; labels[1]="BITE"; labels[2]="LEVEL"; labels[3]="STYLE";
        val[0]=v.bass_cut; val[1]=v.bass_bite; val[2]=v.levels[5]; val[3]=v.bass_style;
        before[0]=old.bass_cut; before[1]=old.bass_bite; before[3]=old.bass_style;
        max[0]=max[1]=100; max[3]=1;
    } else if(page==PANEL_PERC) {
        const char *const names[]={"CONG","MARA","TIMB",lang_knob_bell[panel_lang]};
        for(unsigned i=0;i<4;++i) {
            labels[i]=names[i]; val[i]=v.overlay_levels[i]; before[i]=old.overlay_levels[i]; max[i]=100;
        }
    } else if(page==PANEL_RACK) {
        labels[0]="SOUND"; labels[1]="HIT"; labels[2]="ACC"; labels[3]="LEVEL";
        val[0]=p->rack_sound+1u; val[1]=v.rack_strength; val[2]=v.rack_accent; val[3]=v.rack_level;
        before[0]=old_sound+1u; before[1]=old.rack_strength; before[2]=old.rack_accent; before[3]=old.rack_level;
        max[0]=15; max[1]=max[2]=127;
        if(p->rack_edit) {
            labels[1]="TEMPO"; labels[2]="HIT";
            val[1]=v.bpm; val[2]=v.rack_strength;
            before[1]=old.bpm; before[2]=old.rack_strength;
            max[1]=180;
        }
    } else if(page==PANEL_MIX || page==PANEL_EDIT) {
        labels[2]="LEVEL"; labels[3]="";
        val[2]=v.levels[p->role];
        before[2]=old.levels[p->role];
        if(page==PANEL_MIX) {
            labels[3]="SPEED";val[3]=v.speed;before[3]=old.speed;
            if(v.piano) labels[1]=labels[2]="";
        }
#ifdef COMPAS_HANAN
    } else if(page==PANEL_HANAN) {
        const char *const names[]={"DRIVE","DECAY","PITCH","CLOCK"};
        static const uint8_t range[]={100,100,24,KUMBIA_CLOCK_MAX};
        for(unsigned i=0;i<4;++i) {
            labels[i]=names[i];
            val[i]=v.kumbia[i]; before[i]=old.kumbia[i]; max[i]=range[i];
        }
#endif
    } else if(page==PANEL_SET) {
        labels[0]="TEMPO"; labels[1]="RPT"; labels[2]="SLOT"; labels[3]=v.piano?"":"COUNT";
        val[0]=v.bpm; val[1]=v.phrases; val[2]=p->slot+1u; val[3]=board_count_enabled(v);
        before[0]=old.bpm; before[1]=old.phrases; before[2]=old_slot+1u; before[3]=board_count_enabled(old);
        max[0]=180; max[1]=64; max[2]=16; max[3]=1;
    }
    if(page==PANEL_HOME && !v.piano && !panel_has_bass(v)) labels[2]="";
    unsigned no_knobs=(page==PANEL_BASS && !panel_has_bass(v)) || (page==PANEL_PERC && v.piano);
    if(no_knobs) for(unsigned i=0;i<4;++i) labels[i]="";
    if(redraw) {
        paint(0,188,240,52,PLUM);
        paint(0,188,240,1,DIM);
        if(no_knobs) {
            print(18,209,210,"SELECT FOR 727 PADS",1,DIM2,PLUM,0);
        }
    }
    for(unsigned i=0;i<4;++i) if(labels[i][0] && (redraw || val[i]!=before[i] || (v.piano && i==3 && v.key!=old.key))) {
        unsigned x=i*60;
        uint16_t paper=i==hot?LINE:PLUM;
        if(i==hot) paint(x+1,190,58,49,paper);
        char s[4]; decimal(s,val[i]);
        const char *value=(page==PANEL_HOME && v.piano && i>0 && i<3) ? (val[i]?"ON":"OFF") :
            (page==PANEL_BASS && i==3) ? (val[i]?"ACD":"ENS") :
            (page==PANEL_SET && i==1 && !val[i]) ? lang_repeat_hold[panel_lang] :
            (page==PANEL_SET && i==3) ? (val[i]?"ON":"OFF") : s;
        unsigned unavailable=page==PANEL_HOME && v.piano && i==2 &&
           (!v.playing || v.score_ending || v.score_form==PIANO_FORM_CODA);
        if(unavailable) value="--";
        unsigned angle=val[i]*100u/max[i];
        if(page==PANEL_HOME && v.piano && i==3) {
            score_shift(s,board_root(v));value=s;
            angle=(val[i]<6 ? val[i]+6 : val[i]-6)*100u/11u;
        }
        circle(x+4,197,13,knob_ink[i],paper,0,angle);
        print(x+21,198,36,labels[i],1,knob_ink[i],paper,0);
#ifdef COMPAS_HANAN
        if(page==PANEL_HANAN) {
            if(i==KUMBIA_CLOCK) {
                unsigned hz=board_kumbia_hz(val[i]);
                char rate[5];rate[0]=(char)('0'+hz/1000u);decimal(rate+1,hz%1000u);
                print(x+7,210,49,rate,2,CREAM,paper,knob_ink[i]);
                print(x+7,230,24,"HZ",1,knob_ink[i],paper,0);
            } else if(i==KUMBIA_PITCH) {
                int shift=(int)val[i]-12;
                char signed_value[4];signed_value[0]=shift<0?'-':shift>0?'+':' ';
                unsigned magnitude=(unsigned)(shift<0?-shift:shift);
                signed_value[1]=magnitude>=10?'1':' ';
                signed_value[2]=(char)('0'+magnitude%10u);signed_value[3]=0;
                print(x+7,213,49,signed_value,2,CREAM,paper,knob_ink[i]);
            } else {
                print(x+7,210,49,value,2,CREAM,paper,knob_ink[i]);
                if(i==KUMBIA_DECAY) print(x+7,230,12,"%",1,knob_ink[i],paper,0);
            }
        } else
#endif
        if(page==PANEL_HOME && v.piano && i==3 && board_next_root(v)<12) {
            char next[4];score_shift(next,board_next_root(v));
            char current[12]={'N','O','W',' ',s[0],s[2],0};
            char cue[12]={'N','E','X','T',' ',next[0],next[2],0};
            if(panel_lang) {
                lang_fill(current,lang_format_now[panel_lang-1u],current+4);
                lang_fill(cue,lang_format_next[panel_lang-1u],cue+5);
            }
            print(x+7,213,49,current,1,CREAM,PLUM,0);
            print(x+7,226,49,cue,1,PINK,PLUM,0);
        } else if(unavailable) print(x+7,213,49,value,2,DIM,PLUM,0);
        else print(x+7,213,49,value,2,CREAM,paper,knob_ink[i]);
    }
}

static void bass_face(BoardView v,unsigned pump,unsigned flip,unsigned hot)
{
    if(!panel_has_bass(v)) {
        /* Same woofer, switched off, with one line saying why. */
        paint(6,75,228,113,INK);
        paint(24,81,112,103,LINE);
        circle(29,82,101,DIM,LINE,3,0);
        print(145,84,92,"BASS",2,DIM,INK,0);
        const char *why=v.piano?"SCORE PLAYS":"DRUMS ONLY";
        print(145,111,90,why,1,LILAC,INK,0);
        print(145,123,90,v.piano?"THE BASS":"NO BASS",1,LILAC,INK,0);
        return;
    }
    bass_woofer(v,pump,flip);
    bass_spectrum(v);
    bass_line(v,pump);
    if(hot<4) {
        static const char *const labels[]={"TONE","BITE","LEVL"};
        if(hot==3) value_pop(52,67,v.bass_style?"ACID!":"ENSEMBLE",hot);
        else {
            unsigned values[]={v.bass_cut,v.bass_bite,v.levels[5]};
            char bubble[9]={0}, value[4];
            decimal(value,values[hot]);
            for(unsigned i=0;i<4;++i) bubble[i]=labels[hot][i];
            bubble[4]=' ';
            for(unsigned i=0;i<3;++i) bubble[5u+i]=value[i];
            value_pop(hot==2?52u:169u,hot==2?132u:82u,bubble,hot);
        }
    }
}

#include "stage-percussion.inc"

static void percussion_face(BoardView v,unsigned phases,unsigned entering,unsigned hot)
{
    if(v.piano) {
        /* Same desk, greyed. A score's bolero (conga + maraca) lanes stay
         * live when it has them; everything else is off. One line explains. */
        static const char *const names[]={"CONGA","MARACA","TIMBAL","BELL"};
        static const uint16_t colors[]={PINK,MINT,GOLD,CYAN};
        paint(6,75,228,113,INK);
        unsigned bolero=board_score_perc_on(v);
        for(unsigned i=0;i<4;++i) {
            unsigned x=6+57*i, live=v.score_percussion && i<2u;
            uint16_t ink=live && bolero?colors[i]:DIM;
            paint(x,81,56,90,PLUM);
            paint(x+21,87,4,62,INK);
            paint(x+13,live && bolero?118u:143u,20,7,live?ink:LINE);
            for(unsigned j=0;j<8;++j) paint(x+39,143u-8u*j,5,6,LINE);
            print(x+8,153,42,names[i],1,live?ink:DIM,PLUM,0);
        }
        print(12,175,216,v.score_percussion?
              (bolero?"TOP 3 MUTES THE BOLERO":"TOP 3 BRINGS IN THE BOLERO"):
              "SCORE ONLY / PLAY 727 PADS",1,v.score_percussion?CYAN:LILAC,INK,0);
        return;
    }
    perc_instruments(v,phases,entering,hot);
    if(board_has_clave(v) && hot>=4u) {
        print(7,79,38,"CLAVE",1,CREAM,INK,0);
        print(46,79,21,board_clave(v)?"2-3":"3-2",1,GOLD,INK,0);
        print(73,79,58,board_clave_linked(v)?"BAND+727":"BAND ONLY",1,LILAC,INK,0);
        if(board_next_clave(v)<2)
            print(149,79,80,board_next_clave(v)?"NEXT 2-3":"NEXT 3-2",1,PINK,INK,0);
        print(53,91,137,"OCT- 3-2 / OCT+ 2-3",1,CYAN,INK,0);
    }
}

#include "stage-rack.inc"

static unsigned rack_bar(const Panel *p,BoardView v)
{
    return panel_beats(v.study)==4?0:p->bar;
}

static void rack_editor(const Panel *p,BoardView v)
{
    if(v.rhythm<BOARD_RHYTHM_PATTERNS || v.next_rhythm<BOARD_RHYTHM_PATTERNS) {
        print(5,27,24,"NOW",1,GOLD,INK,0);
        print(32,27,200,board_rhythm_title(v.rhythm),1,GOLD,INK,0);
        const char *source=v.rhythm<BOARD_CONGA_PATTERNS?"CONGA EXCERPT":
                           v.rhythm==BOARD_ROLAND_LESSON?"727 MANUAL":
                           v.rhythm>=BOARD_COLOMBIAN_RHYTHMS?"CUMBIA 727":"727 ADAPTATION";
        print(5,40,228,v.next_rhythm<BOARD_RHYTHM_PATTERNS?"NEXT":source,1,PINK,INK,0);
        if(v.next_rhythm<BOARD_RHYTHM_PATTERNS) print(38,40,195,board_rhythm_title(v.next_rhythm),1,PINK,INK,0);
    } else print(5,29,230,"727 SEQUENCER",2,GOLD,INK,PINK);
    panel_face(p,v,face);
    print(5,54,216,face[3],1,pad_colors[p->rack_sound],INK,0);
    if(v.piano) {
        print(12,90,216,"CHOOSE A GROOVE",1,GOLD,INK,0);
        print(12,110,216,"PRESETS / THEN SEQ",1,CREAM,INK,0);
        return;
    }
    unsigned bar=rack_bar(p,v);
    print(5,72,108,bar?"STEPS 17-32":"STEPS 01-16",1,CYAN,INK,0);
    print(135,72,96,v.rack_sequence==2?"FX DRUM ONLY":v.rack_sequence?"FX LAYER ON":"FX LAYER OFF",1,v.rack_sequence?MINT:DIM,INK,0);
    for(unsigned i=0;i<16;++i) {
        unsigned step=bar+i,x=4+(i%8u)*29u,y=88+(i/8u)*36u;
        uint32_t bit=1u<<step;
        unsigned selected=p->rack_sound==v.rack_sequence_sound;
        uint16_t bg=v.playing && v.position==step?GOLD:
            selected && (v.rack_accents&bit)?PINK:selected && (v.rack_steps&bit)?MINT:PLUM;
        paint(x,y,27,29,bg);
        paint(x,y,27,2,pad_colors[p->rack_sound]);
        char number[4];decimal(number,step+1);
        print(x+7,y+10,12,number+1,1,bg==PLUM?CREAM:INK,bg,0);
    }
    print(4,155,228,v.accent?"SCL ACCENT / SEQ PADS":"SCL NORMAL / SEQ PADS",1,v.accent?PINK:CREAM,INK,0);
    // All 27 physical positions: only the first sixteen enter steps.
    for(unsigned i=0;i<27;++i) {
        unsigned upper=(0x252a52au>>i)&1u;
        paint(4+i*8u,165,7,upper?7:12,i<16?CREAM:DIM);
    }
    print(4,179,232,panel_beats(v.study)==4?"PRESETS NEEDS 4/4 GROOVE":"PRESETS BANK / SESSION EDIT",1,DIM2,INK,0);
}

/* P4: three rising Energy bars beside NOW. The queued level pulses pink on
 * even beats; returning ALGORITHM to the current level clears it. */
static void energy_gauge(BoardView v,unsigned beat)
{
    static const uint16_t inks[]={CYAN,GOLD,PINK};
    unsigned energy=board_part_energy(v.part,v.section), queued=v.pending<3?v.pending:3u;
    if(energy>2) energy=0;
    paint(118,72,68,22,INK);
    for(unsigned i=0;i<3;++i) {
        unsigned height=7u+4u*i;
        uint16_t ink=i<=energy?inks[energy]:DIM;
        if(i==queued && !(beat&1u)) ink=PINK;
        paint(120+i*10u,92u-height,8,height,ink);
    }
    print(152,79,34,energies[energy],1,inks[energy],INK,0);
}

/* P4: the sound name of a manual Band pad hit, for 400 ms, on its own side. */
static void pad_name(unsigned named)
{
    if(!named || named>RACK_SOUNDS) return;
    unsigned sound=named-1u, length=0;
    const char *name=panel_word(rack_names[sound]);
    while(name[length]) ++length;
    unsigned width=length*6u+4u, x=sound<7u?1u:239u-width;
    paint(x,132,width,9,pad_colors[sound]);
    print(x+2,132,width-2u,name,1,INK,pad_colors[sound],0);
}

enum { ORDER_TOP=40, ORDER_HEIGHT=9 };
/* P5 columns: slot, title (16 with accents), family, BPM, counting note, state.
 * Family ink: CYAN for a source-backed body or score edition, LILAC for an
 * authored study. NOW shows the live tempo and clave; other rows are saved. */
static void order_face(const Panel *p,BoardView v)
{
    print(5,9,176,"THE SHOW",2,GOLD,INK,PINK);
    print(5,30,60,"KEYS 1-16",1,CREAM,INK,0);
    print(71,30,48,"SOURCED",1,CYAN,INK,0);
    print(119,30,36,"STUDY",1,LILAC,INK,0);
    print(167,30,12,lang_name[panel_lang],1,MINT,INK,0); /* ALGORITHM: EN / ES */
    burst_size(194,7,33,0,board_effective_bpm(v),CYAN,"BPM");
    for(unsigned slot=0;slot<SET_SLOTS;++slot) {
        unsigned y=ORDER_TOP+slot*ORDER_HEIGHT;
        if(!visible(0,y,240,ORDER_HEIGHT)) continue;
        unsigned now=slot==v.song, next=slot==v.next_song && !now, cursor=slot==p->slot && !now && !next;
        unsigned study=0,bpm=0,reverse=0, exists=board_song_meta(slot,&study,&bpm,&reverse);
        uint16_t bg=now?GOLD:next?PINK:cursor?LINE:(slot&1u)?PLUM:INK;
        uint16_t fg=now||next?INK:CREAM;
        char n[4]; decimal(n,slot+1);
        paint(0,y,240,ORDER_HEIGHT,bg);
        print(2,y,12,n+1,1,now||next?INK:CYAN,bg,0);
        print_text(17,y,96,exists?board_song_title(slot):panel_word("EMPTY"),1,exists?fg:DIM,bg,0,0);
        if(exists) {
            unsigned score=board_song_is_score(slot), flags=panel_study_flags(study);
            print(116,y,48,score?"SCORE":panel_study_family(study),1,
                  now||next?INK:score || (flags&STUDY_SOURCED)?CYAN:LILAC,bg,0);
            if(now) { bpm=board_effective_bpm(v); reverse=board_clave(v); }
            char tempo[4]; decimal(tempo,bpm);
            print(168+(bpm<100u?6u:0u),y,18,tempo+(bpm<100u),1,now||next?INK:GOLD,bg,0);
            const char *note=score?"":(flags&STUDY_CUT_TIME)?"IN 2":
                (study==2 || study==6)?(reverse?"2-3":"3-2"):"";
            print(189,y,24,note,1,now||next?INK:CYAN,bg,0);
        }
        print(216,y,24,now?"NOW":next?"NEXT":cursor?"SAVE":"",1,now||next?INK:CREAM,bg,0);
    }
}

enum { STATUS, CLOCK, SIGN, FOOTER, BODY, TABS, LIGHTS, INFO,
       CONE0, HORN=CONE0+8, BULB0, BULB1, STICKER, KNOB0, NOTICE=KNOB0+4, MOTION_STATUS, LIFT_BULB0, LIFT_BULB1, LARGE_STICKER, CONES_ALL, RACK_OLD_STEP, RACK_NEW_STEP };
/* Rack playhead and running-order rows live on mutually exclusive pages. */
enum { ENERGY_GAUGE=RACK_OLD_STEP, PAD_FLASH=RACK_NEW_STEP }; /* Band page only */
enum { ORDER_ROW=RACK_NEW_STEP, BASS_CONE=RACK_OLD_STEP, BASS_LINE=RACK_NEW_STEP,
       RACK_HEADER=RACK_OLD_STEP, RACK_PADS=RACK_NEW_STEP, RACK_LOOP=CONE0 };
typedef struct {
    Panel panel;
    BoardView view;
    PanelKeys keys;
    uint8_t cameo, flash, beat, next, pending, notice, old_beat, old_step;
    uint8_t bass_pump, bass_flip, hot, perc_phases, perc_entering;
    uint8_t rack_last, rack_serial;
    uint16_t rack_press, rack_ripple, rack_cut;
    uint8_t pad_flash, pad_name; /* Band finger pads: sound+1 while lit / named, 0 when idle */
} StageModel;
static StageModel model;
#include "stage-motion.inc"
static uint32_t jobs, landed_at, master_at, bass_pump_at, bass_flip_at, hot_at;
static uint32_t perc_hit_at[4], perc_in_at;
static uint32_t rack_hit_at[RACK_SOUNDS], rack_cut_at[RACK_SOUNDS];
static uint16_t order_rows, rack_dirty, rack_batch;
static uint8_t rack_header_pending;
static uint32_t pad_hit_at;
static uint8_t pad_side, pad_dirty; /* Most recent bank and the banks awaiting repaint. */
static uint8_t stripe_row, working_job, measurement, notice_priority;
static uint32_t redraw_started, compute, transfer, pixels, touch_at;
static uint32_t region_compute, region_transfer, region_pixels;
static uint8_t touching, touch_queued;
/* Per gate, retain the maxima for compute ticks, transfer ticks, elapsed
 * ticks and transferred pixels. R2 elapsed starts at the input scan. */
volatile uint32_t compas_display_stats[3][4];
volatile uint32_t compas_animation_ticks[2][4];
static uint32_t effect_jobs, effect_started, effect_compute, effect_transfer, effect_pixels;
static uint8_t effect_kind, previous_phase;

static unsigned current_notice(const StageModel *m,unsigned master_moved)
{
    const BoardView *v=&m->view;
    return v->fault?1u:m->panel.locked || v->result==CP_BUSY?2u:v->result==CP_STOP_FIRST?3u:
        v->result==BOARD_EMPTY?4u:v->result==BOARD_SESSION || v->result==BOARD_SAVED?5u:
        v->result?6u:(v->rack_sequence==2 && !m->panel.rack_edit)?8u:master_moved || (m->panel.page==PANEL_HOME && !v->master)?7u:
        v->speed && v->speed<100?9u:0u;
}

static void clock_face(const StageModel *m)
{
    if(!visible(0,104,240,28)) return;
    BoardView v=m->view;
    unsigned count_in=board_count_active(v);
    unsigned beat=m->beat, beats=v.piano?v.score_beats:panel_beats(v.study);
    if(count_in) beats/=2u;
    paint(4,105,90,26,WOOD);
    paint(5,106,88,24,GRILL);
    paint(98,105,90,26,WOOD);
    paint(99,106,88,24,GRILL);
    for(unsigned i=0;i<8;++i) paint(190+i*5,109-i/2u,5,17+i,WOOD);
    paint(191,107,45,22,INK);
    for(unsigned i=0;i<8;++i) {
        unsigned down=i==0 || (i==beats/2u && !v.piano);
        unsigned color=i>=beats ? LINE : !v.playing ? RING : i==beat ? (count_in || m->flash?PINK:GOLD) :
            (count_in || m->pending) && i>beat ? PINK : i>beat ? RING : down ? CYAN : LILAC;
        color=motion_cone(&motion,beat,i,color);
        unsigned filled=(i==beat && v.playing && !motion.break_mode) || motion.kind==MOTION_BACK ||
            motion.kind==MOTION_FILL || motion.roll>i ||
            (motion.kind==MOTION_LIFT && motion.frame<6 && color==GOLD);
        circle(5+22*i+(i>=4?6:0),107,21,(uint16_t)color,GRILL,1,filled);
    }
    unsigned limit=v.next_bar && !v.fill && beat<beats/2u ? beats/2u : beats;
    unsigned remain=limit>beat ? limit-beat : 1u;
    if(v.fill==1 && beat+1u>=beats && !v.piano) remain+=beats;
    char horn[12]={'I','N',' ',(char)('0'+remain/10u),(char)('0'+remain%10u),0};
    if(remain<10) { horn[3]=horn[4]; horn[4]=0; }
    if(panel_lang) lang_fill(horn,lang_format_in[panel_lang-1u],horn+3);
    const char *label=v.fill && v.next_part<PART_NONE?"FILL":m->pending?(v.piano?"NEXT BAR":v.next_bar?"BAR":horn):"PHRASE";
    uint16_t ink=m->pending?PINK:DIM2;
    /* Auto-advance count: never pink, never "IN n" (that means a cue). */
    char more[12]="00 MORE";
    char lead[12]={'C','O','U','N','T',' ',(char)('1'+beat),0};
    if(panel_lang) lang_fill(lead,lang_format_count[panel_lang-1u],lead+6);
    if(count_in) { label=lead; ink=PINK; }
    else if(!m->pending && v.phrases_left) {
        unsigned after=v.phrases_left-1u;
        more[0]=(char)('0'+after/10u);
        more[1]=(char)('0'+after%10u);
        if(panel_lang) {
            char digits[3]={more[0],more[1],0};
            lang_fill(more,lang_format_more[panel_lang-1u],digits+(after<10u));
        }
        label=after?more+(after<10u && !panel_lang):"LAST";
        ink=CYAN;
    }
    if(label==horn) {
        /* The most time-critical number on screen: 2x digits, 1x unit. */
        char digits[3]={(char)('0'+remain/10u),(char)('0'+remain%10u),0};
        const char *shown=remain<10u?digits+1:digits;
        unsigned width=remain<10u?11u:22u;
        print(193,104,width,shown,2,PINK,INK,0);
        print(195+width,113,236u-(195u+width),remain==1u?"BEAT":"BEATS",1,PINK,INK,0);
    } else print(189,113,48,label,1,ink,INK,0);
}

static void stage_paint(const StageModel *m)
{
    const Panel *p=&m->panel;
    BoardView v=m->view;
    unsigned page=p->page;
    unsigned wide=page==PANEL_RACK || page==PANEL_SET || page==PANEL_HANAN;
    unsigned bandlike=page==PANEL_HOME || page==PANEL_MIX || page==PANEL_EDIT;
    background(page!=PANEL_RACK);
    if(visible(0,0,240,7)) {
        static const uint16_t colors[]={PINK,GOLD,CYAN,MINT};
        uint16_t lamps[30];
        unsigned active=m->beat<8?m->beat*4u:29u;
        for(unsigned i=0;i<30;++i)
            lamps[i]=(uint16_t)motion_lights(&motion,m->beat,i,i==active?CREAM:colors[i&3u]);
        marquee(lamps);
    }
    if(page!=PANEL_SET && visible(0,7,240,17)) tabs(page);
    if(page==PANEL_RACK && visible(0,24,240,164)) {
        if(p->rack_edit) rack_editor(p,v);
        else {
            if(visible(0,24,240,25)) rack_header(v,m->rack_last);
            if(visible(0,49,240,118)) rack_pads(p,v,m->rack_press,m->rack_ripple,m->rack_cut,m->rack_last);
            if(visible(0,169,240,17)) rack_loop(v);
        }
    }
#ifdef COMPAS_HANAN
    if(page==PANEL_HANAN) {
        if(visible(0,24,240,34)) title_sign("KUMBIA",0,0,0);
        if(visible(0,58,240,13)) {
            const char *const modes[]={"OFF","LAYER","DRUMS ONLY"};
            print(6,60,120,v.piano?"SELECT A GROOVE":modes[v.hanan_mode],1,CYAN,INK,0);
            print(174,60,64,"FX MODE",1,DIM2,INK,0);
        }
        if(visible(0,71,240,61)) {
            static const char *const patterns[]={"WHITE","YELLOW","RED","GREEN"};
            static const uint16_t colours[]={CREAM,GOLD,PINK,MINT};
            print(6,76,222,"PRESETS",1,CREAM,INK,0);
            for(unsigned i=0;i<4;++i) {
                unsigned x=i*60u+3u;
                uint16_t bg=v.hanan_next==i?PINK:v.hanan_pattern==i?GOLD:PLUM;
                paint(x,94,54,32,bg);
                paint(x,94,54,3,colours[i]);
                char number[]={(char)('5'+i),0};
                print(x+5,101,7,number,1,bg==PLUM?DIM2:INK,bg,0);
                print(x+3,113,48,patterns[i],1,bg==PLUM?CREAM:INK,bg,0);
            }
        }
        if(visible(0,132,240,56)) {
            static const char *const pads[]={"BOMBO","GUIRO","CAMP","CONGA"};
            static const uint16_t colours[]={GOLD,MINT,CREAM,PINK};
            for(unsigned i=0;i<4;++i) {
                unsigned x=i*60u+3u;
                paint(x,140,54,32,PLUM);
                paint(x,140,54,3,colours[i]);
                char number[]={(char)('1'+i),0};
                print(x+5,147,7,number,1,DIM2,PLUM,0);
                print(x+5,159,44,pads[i],1,CREAM,PLUM,0);
            }
        }
    }
#endif
    if(page==PANEL_SET && visible(0,7,240,181)) order_face(p,v);
    if(!wide) {
        if(visible(0,21,240,37)) {
            const char *title=panel_title(v);
            unsigned cameo=m->cameo, flash=m->flash, frame=motion.frame;
            unsigned dim=motion.break_mode || (motion.ending && m->beat>=5);
            char clipped[19];
            if(motion.ending) {
                unsigned length=0;
                while(length<18 && title[length]) ++length;
                unsigned kept=length>m->beat*2u?length-m->beat*2u:0u;
                for(unsigned i=0;i<kept;++i) clipped[i]=title[i];
                clipped[kept]=0;
                title=clipped;
            }
            if(motion.kind==MOTION_CLOSE) {
                title=frame<8?"":panel_word("THAT'S THE SHOW");
                cameo=flash=frame>=8;
            }
            if(motion.kind==MOTION_SONG) {
                cameo=frame>=12;
                flash=frame>=12 && frame<18;
                if(frame<6) title=board_song_title(motion.previous_song);
            }
            title_sign(title,cameo,flash,dim);
            if(motion.kind==MOTION_SONG && frame<12) {
                unsigned x=(frame%6u)*40u+40u;
                if(frame<6) {
                    paint(0,28,x,26,PAPER);
                    paint(x-4,28,4,26,PINK);
                } else {
                    paint(x,28,240u-x,26,PAPER);
                    paint(x-4,28,4,26,GOLD);
                }
            }
            if(motion.kind==MOTION_CLOSE && frame<8) paint(0,24,240,34,INK);
        }
        if(visible(0,58,191,13)) {
            char n[4]; decimal(n,v.song<SET_SLOTS?v.song+1u:0u);
            unsigned count=0;
            for(unsigned i=0;i<SET_SLOTS;++i) count+=board_song_exists(i)!=0;
            char index[]={n[1],n[2],'/',(char)('0'+count/10u),(char)('0'+count%10u),'>',0};
            print(5,60,36,v.song==SET_SLOTS?"BANK":index,1,CYAN,INK,0);
            unsigned ticker=(motion.kind==MOTION_SONG && motion.frame>=12) ||
                (motion.kind==MOTION_CLOSE && motion.frame>=8);
            const char *subtitle=ticker?"TAKE IT AWAY":
                v.next_groove<BOARD_GROOVE_PRESETS?board_groove_title(v.next_groove):
                v.song==SET_SLOTS?panel_word("RHYTHM PRESET"):
                m->next<SET_SLOTS?board_song_title(m->next):panel_word("END OF SET");
            print_text(43,60,145,subtitle,1,ticker?(m->beat&1u?PINK:GOLD):CREAM,INK,0,0);
        }
        if(bandlike) {
            if(visible(0,71,192,33)) {
                unsigned part=current_part(v);
                const char *current=board_count_active(v) || (v.piano && v.playing && !v.score_form)?"COUNT IN":
                    v.piano?panel_score_part_name(v.piano,part):panel_groove_part_name(v.study,part,0);
                if(!v.piano && !board_count_active(v) && v.part==PART_NONE && v.section!=0) current=energies[v.section];
                const char *cue=v.next_groove<BOARD_GROOVE_PRESETS?"NEXT RHYTHM":v.next_song<SET_SLOTS?"NEXT SONG":v.next_part<PART_NONE?
                    v.piano?panel_score_part_name(v.piano,v.next_part):panel_groove_part_name(v.study,v.next_part,0):
                    v.pending<3?energies[v.pending]:board_next_root(v)<12?(v.piano?"TRANSPOSE":"KEY CHANGE"):board_next_clave(v)<2?"CLAVE CHANGE":"";
                print(5,71,page==PANEL_HOME && !v.piano?112:180,v.playing?current:"READY",2,v.playing?CYAN:GOLD,INK,PINK);
                if(page==PANEL_HOME && !v.piano) energy_gauge(v,m->beat);
                print(5,94,132,motion.kind==MOTION_BACK?"BACK IN!":motion.kind==MOTION_LIFT && motion.frame<6?"LIFT!":m->flash?(part==PART_CHORUS || v.section==1?"LIFT!":"CHANGE!"):
                      m->pending?cue:v.playing?"":current,1,m->flash?GOLD:m->pending?PINK:LILAC,INK,0);
                print(141,94,48,v.fill && (!(m->beat&1u) || !m->pending)?"FILL":v.accent?"ACC":"",1,v.fill?CYAN:PINK,INK,0);
            }
            if(visible(187,53,53,53)) {
                unsigned size=43;
                if(motion.kind==MOTION_LIFT) {
                    static const uint8_t swell[]={53,49,45};
                    size=motion.frame<6?swell[motion.frame/2u]:43u;
                }
                if(motion.break_mode) size=31;
                if(motion.kind==MOTION_BACK || motion.kind==MOTION_FILL) size=53;
                burst_size(214u-size/2u,80u-size/2u,size,motion.kind==MOTION_LIFT && motion.frame<6?motion.phase&3u:0,
                           board_effective_bpm(v),motion.break_mode?DIM:m->flash?PINK:GOLD,"BPM");
                if(size==43 && !m->pending) sticker_step(m->beat);
            }
            clock_face(m);
            if(visible(0,132,240,52)) {
                if(page==PANEL_HOME) {
                    labelled_keys(m->keys,v,m->flash,m->pad_flash);
                    pad_name(m->pad_name);
                }
                else {
                    panel_face(p,v,face);
                    for(unsigned row=3;row<=6;++row)
                        print(10,135+(row-3u)*12u,216,face[row],1,CREAM,INK,0);
                }
            }
        } else {
            if(visible(199,60,38,9)) {
                unsigned effective=board_effective_bpm(v);
                char bpm[4]; decimal(bpm,effective);
                print(199,60,18,bpm+(effective<100),1,GOLD,INK,0);
                print(219,60,18,"BPM",1,GOLD,INK,0);
            }
            if(visible(0,66,240,122)) {
                if(page==PANEL_BASS) bass_face(v,m->bass_pump,m->bass_flip,m->hot);
                else percussion_face(v,m->perc_phases,m->perc_entering,m->hot);
            }
        }
    }
    if(visible(0,188,240,52)) footer(p,page,v,1,v,0,0,m->hot);
    if(m->notice && visible(0,184,240,12)) {
        static const char *const messages[]={"","OUTPUT STOPPED","BUSY / RETRY","STOP TO EDIT",
            "EMPTY SLOT","SET SAVED","ACTION UNAVAILABLE","MASTER","727 DRUMS ONLY","REBAJADA"};
        char volume[4]; decimal(volume,v.master);
        paint(0,184,240,12,DIM);
        print(7,184,114,messages[m->notice],1,m->notice==1?PINK:CREAM,DIM,0);
        if(m->notice==7u) print(126,184,24,volume,1,GOLD,DIM,0);
        if(m->notice==9 || (m->notice==8 && v.speed<100)) {
            char rate[4];decimal(rate,v.speed);
            print(144,184,18,rate,1,GOLD,DIM,0);print(164,184,12,"%",1,GOLD,DIM,0);
        }
    }
}

static Rect job_area(unsigned job,unsigned beat)
{
    static const Rect main_regions[]={{0,71,240,33},{0,104,240,28},{0,24,240,34},
        {0,188,240,52},{0,132,240,56},{0,7,240,17},{0,0,240,7},{0,58,240,13}};
    if(job<8) return main_regions[job];
    if(model.panel.page==PANEL_RACK && !model.panel.rack_edit && job==RACK_LOOP)
        return (Rect){0,169,240,17};
    if(model.panel.page==PANEL_PERC && job>=CONE0 && job<CONE0+4u) {
        static const Rect lanes[]={{9,105,42,33},{67,105,46,37},{129,105,33,33},{186,105,49,40}};
        return lanes[job-CONE0];
    }
    if(job<HORN) {
        unsigned i=job-CONE0;
        return (Rect){(uint8_t)(5+22*i+(i>=4?6:0)),107,21,21};
    }
    if(job==ORDER_ROW && model.panel.page==PANEL_SET) {
        unsigned slot=model.view.next_song;
        if(slot>=SET_SLOTS || !(order_rows&(1u<<slot))) {
            for(slot=0;slot+1u<SET_SLOTS && !(order_rows&(1u<<slot));++slot) {}
        }
        return (Rect){0,(uint8_t)(ORDER_TOP+slot*ORDER_HEIGHT),240,ORDER_HEIGHT};
    }
    if(model.panel.page==PANEL_BASS) {
        if(job==BASS_CONE) return (Rect){22,82,61,61};
        if(job==BASS_LINE) return (Rect){105,160,129,17};
    }
    if(model.panel.page==PANEL_RACK && !model.panel.rack_edit) {
        if(job==RACK_HEADER) return (Rect){0,24,150,25};
        if(job==RACK_PADS) {
            unsigned i=__builtin_ctz((unsigned)rack_batch);
            return (Rect){(uint8_t)(4+i%4u*58u),(uint8_t)(49+i/4u*30u),55,28};
        }
    }
    if(model.panel.page==PANEL_HOME) {
        if(job==ENERGY_GAUGE) return (Rect){118,72,68,22};
        if(job==PAD_FLASH) {
            unsigned side=pad_dirty&(1u<<pad_side)?pad_side:pad_side^1u;
            return (Rect){side?168:0,132,72,42};
        }
    }
    if(job==RACK_OLD_STEP || job==RACK_NEW_STEP) {
        unsigned step=job==RACK_OLD_STEP?model.old_step:model.view.position;
        unsigned i=(step-rack_bar(&model.panel,model.view))&15u;
        return (Rect){(uint8_t)(4+(i%8u)*29u),(uint8_t)(88+(i/8u)*36u),27,29};
    }
    if(job==HORN) return (Rect){189,104,50,25};
    if(job==BULB0 || job==BULB1) return (Rect){(uint8_t)(2+(beat<8?beat*4u:29u)*8u),1,5,5};
    if(job==STICKER) return (Rect){200,65,27,29};
    if(job==LARGE_STICKER) return (Rect){187,53,53,53};
    if(job==CONES_ALL) return (Rect){5,107,181,21};
    if(job==NOTICE) return (Rect){0,184,240,12};
    if(job==MOTION_STATUS) return (Rect){4,93,186,11};
    if(job==LIFT_BULB0 || job==LIFT_BULB1) {
        unsigned phase=job==LIFT_BULB0?previous_phase:motion.phase;
        return (Rect){(uint8_t)(2+(phase*2u)%30u*8u),1,5,5};
    }
    return (Rect){(uint8_t)((job-KNOB0)*60u),196,60,42};
}

static void high_water(unsigned gate,uint32_t cpu,uint32_t spi,uint32_t elapsed,uint32_t size)
{
    uint32_t values[]={cpu,spi,elapsed,size};
    for(unsigned i=0;i<4;++i)
        if(compas_display_stats[gate][i]<values[i]) compas_display_stats[gate][i]=values[i];
}

static void stage_touch(uint32_t now)
{
    if(!touching) { touching=1; touch_queued=0; touch_at=now; }
}

static unsigned order_row_bit(unsigned slot)
{
    return slot<SET_SLOTS?1u<<slot:0u;
}

static void stage_offer(const Panel *p,BoardView v,PanelKeys keys,RackFeedback feedback,unsigned opening,unsigned cameo,uint32_t now)
{
    const BoardView *old=&model.view;
    /* PLAY/STOP during a full transfer supersedes that unfinished frame.
     * Start the new state cleanly instead of accumulating both screens. */
    unsigned transport_reframe=jobs && measurement==3 && v.playing!=old->playing;
    static uint8_t drawn_lang;
    unsigned page=p->page, full=opening || transport_reframe || page!=model.panel.page ||
        v.song!=old->song || v.study!=old->study || p->rack_edit!=model.panel.rack_edit ||
        panel_lang!=drawn_lang;
    drawn_lang=panel_lang;
    unsigned bandlike=page==PANEL_HOME || page==PANEL_MIX || page==PANEL_EDIT;
    unsigned beat=v.playing?v.position/4u:8u;
    if(beat>8) beat=8;
    unsigned pending=board_next_clave(v)<2 || board_next_root(v)<12 || v.next_groove<BOARD_GROOVE_PRESETS || v.next_song<SET_SLOTS || v.next_part<PART_NONE || v.pending<3;
    unsigned flash=model.flash;
    if(!opening && v.playing && (model.pending || v.song!=old->song) &&
       (v.song!=old->song || current_part(v)!=current_part(*old) || v.section!=old->section)) {
        flash=1;
        landed_at=now;
    }
    if(!v.playing || now-landed_at>=7200000u) flash=0;
    if(!opening && v.master!=old->master) master_at=now;
    StageModel next={*p,v,keys,(uint8_t)cameo,(uint8_t)flash,(uint8_t)beat,
        (uint8_t)next_slot(v),(uint8_t)pending,0,model.beat,model.view.position,0,0,4,0,0,
        p->rack_sound,feedback.serial,0,0,0,0,0};
    next.notice=(uint8_t)current_notice(&next,master_at && now-master_at<24000000u);
    StageMotion effects=motion_offer(v,*old,p,opening,beat,model.beat,now);
    /* Finish the page's first image before advancing a decorative frame.
     * A 50 ms sign animation otherwise resends the same 8,160 pixels while
     * a full 12 MHz transfer is still in progress. Musical state stays live;
     * the next offer catches the animation up to the wall clock. */
    if(!full && jobs && measurement==3 && effects.kind==motion.kind)
        effects.frame=motion.frame;
    uint32_t change=0;
    if(full) {
        change=255u|(1u<<NOTICE);
        notice_priority=0;
        order_rows=0;
    }
    else {
        unsigned bands=bandlike || page==PANEL_BASS || page==PANEL_PERC;
        unsigned cue=v.next_groove!=old->next_groove || v.next_song!=old->next_song || v.next_part!=old->next_part || v.pending!=old->pending ||
            v.next_bar!=old->next_bar || v.key!=old->key || v.clave!=old->clave;
        unsigned part=v.playing!=old->playing || v.part!=old->part || v.section!=old->section ||
            v.score_form!=old->score_form || board_count_active(v)!=board_count_active(*old) || flash!=model.flash;
        if(bands && (cameo!=model.cameo || flash!=model.flash || v.study!=old->study))
            change|=1u<<SIGN;
        if(bands && (v.next_groove!=old->next_groove || next.next!=model.next || (!bandlike && v.bpm!=old->bpm))) change|=1u<<INFO;
        if(bandlike && (cue || part || v.fill!=old->fill || v.accent!=old->accent || v.bpm!=old->bpm))
            change|=(1u<<STATUS)|(1u<<CLOCK)|(1u<<INFO);
        if(v.speed!=old->speed) {
            change|=(1u<<NOTICE)|(1u<<STATUS)|(1u<<INFO);
            if(page==PANEL_MIX) change|=(1u<<BODY)|(1u<<(KNOB0+3));
            if(page==PANEL_SET) change|=(1u<<SIGN)|(1u<<TABS);
        }
        if(page==PANEL_HOME && (keys.active!=model.keys.active || keys.queued!=model.keys.queued ||
           keys.enabled!=model.keys.enabled || keys.upper!=model.keys.upper || flash!=model.flash))
            change|=1u<<BODY;
        if(page==PANEL_BASS && (v.key!=old->key || v.bass_cut!=old->bass_cut || v.bass_bite!=old->bass_bite ||
           v.bass_style!=old->bass_style || v.levels[5]!=old->levels[5])) {
            change|=(1u<<STATUS)|(1u<<CLOCK)|(1u<<BODY);
            if(v.bass_cut!=old->bass_cut) change|=1u<<KNOB0;
            if(v.bass_bite!=old->bass_bite) change|=1u<<(KNOB0+1);
            if(v.levels[5]!=old->levels[5]) change|=1u<<(KNOB0+2);
            if(v.bass_style!=old->bass_style) change|=1u<<(KNOB0+3);
        }
        if(page==PANEL_PERC) {
            if(v.score_percussion!=old->score_percussion) change|=(1u<<STATUS)|(1u<<CLOCK)|(1u<<BODY);
            if(v.clave!=old->clave || v.rhythm!=old->rhythm || v.overlays!=old->overlays || v.piano!=old->piano) change|=(1u<<STATUS)|(1u<<CLOCK)|(1u<<BODY);
            for(unsigned i=0;i<4;++i) if(v.overlay_levels[i]!=old->overlay_levels[i])
                change|=(1u<<(KNOB0+i))|(1u<<STATUS)|(1u<<CLOCK)|(1u<<BODY);
        }
        if(page==PANEL_HOME || page==PANEL_MIX || page==PANEL_EDIT) {
            if(v.bpm!=old->bpm) change|=1u<<KNOB0;
            if(v.feel!=old->feel || v.score_guide!=old->score_guide) change|=1u<<(KNOB0+1);
            if(v.score_hold!=old->score_hold) change|=1u<<(KNOB0+2);
            if(page==PANEL_HOME && v.piano && (v.playing!=old->playing ||
               v.score_ending!=old->score_ending || v.score_form!=old->score_form)) change|=1u<<(KNOB0+2);
            if(p->role!=model.panel.role || v.levels[page==PANEL_HOME?5:p->role]!=
               old->levels[page==PANEL_HOME?5:p->role]) change|=1u<<(KNOB0+2);
            if(page==PANEL_HOME && v.piano && v.key!=old->key) change|=1u<<(KNOB0+3);
            if(page==PANEL_HOME && overlay_average(v)!=overlay_average(*old)) change|=1u<<(KNOB0+3);
            if(page!=PANEL_HOME && (v.steps!=old->steps || v.muted!=old->muted || v.solo!=old->solo ||
                p->bar!=model.panel.bar || p->role!=model.panel.role)) change|=1u<<BODY;
        }
#ifdef COMPAS_HANAN
        if(page==PANEL_HANAN) {
            if(v.hanan_pattern!=old->hanan_pattern || v.hanan_next!=old->hanan_next)
                change|=(1u<<STATUS)|(1u<<CLOCK);
            if(v.hanan_mode!=old->hanan_mode || v.piano!=old->piano) change|=1u<<INFO;
            for(unsigned i=0;i<4;++i)
                if(v.kumbia[i]!=old->kumbia[i]) change|=1u<<(KNOB0+i);
        }
#endif
        if(page==PANEL_RACK && p->rack_edit) {
            if(p->rack_sound!=model.panel.rack_sound || p->bar!=model.panel.bar ||
               v.rack_steps!=old->rack_steps || v.rack_accents!=old->rack_accents ||
               v.rack_sequence_sound!=old->rack_sequence_sound || v.rack_sequence!=old->rack_sequence ||
               v.accent!=old->accent || v.piano!=old->piano || v.rhythm!=old->rhythm || v.next_rhythm!=old->next_rhythm)
                change|=(1u<<SIGN)|(1u<<INFO)|(1u<<STATUS)|(1u<<CLOCK)|(1u<<BODY)|(1u<<KNOB0);
            if(v.position!=old->position || v.playing!=old->playing) {
                if(old->position>=rack_bar(p,v) && old->position<rack_bar(p,v)+16u) change|=1u<<RACK_OLD_STEP;
                if(v.position>=rack_bar(p,v) && v.position<rack_bar(p,v)+16u) change|=1u<<RACK_NEW_STEP;
            }
            if(v.bpm!=old->bpm) change|=1u<<(KNOB0+1);
            if(v.rack_strength!=old->rack_strength) change|=1u<<(KNOB0+2);
            if(v.rack_level!=old->rack_level) change|=1u<<(KNOB0+3);
        } else if(page==PANEL_RACK) {
            if(v.next_groove!=old->next_groove || v.accent!=old->accent ||
               board_roll_rate(v)!=board_roll_rate(*old) ||
               board_recording(v)!=board_recording(*old)) change|=1u<<SIGN;
            if((v.rack_sequence || old->rack_sequence || board_recording(v) || board_recording(*old)) &&
               (v.position!=old->position || v.playing!=old->playing || v.rack_steps!=old->rack_steps ||
                v.rack_sequence_sound!=old->rack_sequence_sound || v.rack_sequence!=old->rack_sequence ||
                board_recording(v)!=board_recording(*old))) change|=1u<<RACK_LOOP;
            if(v.accent!=old->accent) rack_dirty|=1u<<15;
            if(p->rack_sound!=model.panel.rack_sound) {
                rack_dirty|=(1u<<p->rack_sound)|(1u<<model.panel.rack_sound);
                change|=1u<<KNOB0;
            }
            if(v.rack_strength!=old->rack_strength) change|=1u<<(KNOB0+1);
            if(v.rack_accent!=old->rack_accent) change|=1u<<(KNOB0+2);
            if(v.rack_level!=old->rack_level) change|=1u<<(KNOB0+3);
        }
        if(page==PANEL_SET) {
            unsigned dirty=0;
            if(v.next_song!=old->next_song)
                dirty=order_row_bit(v.next_song)|order_row_bit(old->next_song);
            if(p->slot!=model.panel.slot) {
                dirty|=(order_row_bit(p->slot)|order_row_bit(model.panel.slot))&
                    ~(order_row_bit(v.song)|order_row_bit(v.next_song));
                change|=1u<<(KNOB0+2);
            }
            if(dirty) {
                order_rows|=(uint16_t)dirty;
                change|=1u<<ORDER_ROW;
            }
            if(v.bpm!=old->bpm) change|=(1u<<KNOB0)|(1u<<TABS)|(1u<<SIGN);
            if((v.bpm!=old->bpm || v.speed!=old->speed || board_clave(v)!=board_clave(*old)) && v.song<SET_SLOTS) {
                order_rows|=(uint16_t)order_row_bit(v.song);
                change|=1u<<ORDER_ROW;
            }
            if(v.phrases!=old->phrases) change|=1u<<(KNOB0+1);
            if(board_count_enabled(v)!=board_count_enabled(*old)) change|=1u<<(KNOB0+3);
        }
        if(next.notice!=model.notice || (next.notice==7 && v.master!=old->master)) {
            change|=1u<<NOTICE;
            notice_priority=1;
        }
        /* The full frame owns the banner separately from these bands. When
         * it is transparent, updates behind it need those rows too. */
        if(measurement==3 && jobs && !next.notice && (change&((1u<<BODY)|(1u<<FOOTER))))
            change|=1u<<NOTICE;
    }
    /* The full frame already paints the woofer and the line. */
    if(page==PANEL_BASS && panel_has_bass(v) && !full && !(jobs && measurement==3)) {
        if(v.bass_hit!=old->bass_hit) bass_pump_at=now;
        if(v.bass_style!=old->bass_style) bass_flip_at=now;
        if(v.bass_cut!=old->bass_cut || v.bass_bite!=old->bass_bite ||
           v.levels[5]!=old->levels[5] || v.bass_style!=old->bass_style) {
            next.hot=v.bass_style!=old->bass_style?3u:v.levels[5]!=old->levels[5]?2u:
                v.bass_bite!=old->bass_bite?1u:0u;
            hot_at=now;
        } else if(hot_at && now-hot_at<14400000u) next.hot=model.hot;
        if(v.playing && bass_pump_at && now-bass_pump_at<2520000u)
            next.bass_pump=(uint8_t)(3u-(now-bass_pump_at)/840000u);
        if(bass_flip_at && now-bass_flip_at<7680000u)
            next.bass_flip=(uint8_t)(1u+(now-bass_flip_at)/960000u);
        if(next.bass_pump!=model.bass_pump || next.bass_flip!=model.bass_flip)
            change|=1u<<BASS_CONE;
        if(v.position!=old->position || v.playing!=old->playing || v.steps!=old->steps ||
           next.bass_pump!=model.bass_pump) change|=1u<<BASS_LINE;
    } else if(full || page!=PANEL_BASS) bass_pump_at=bass_flip_at=0;
    if(page==PANEL_PERC && !v.piano && !full) {
        unsigned entered=v.overlays&~old->overlays;
        if(!v.playing) for(unsigned i=0;i<4;++i) perc_hit_at[i]=0;
        for(unsigned lane=0;lane<4;++lane) {
            unsigned bit=1u<<lane;
            if(v.overlay_levels[lane]!=old->overlay_levels[lane]) {
                next.hot=(uint8_t)lane;
                hot_at=now;
            }
            if(entered&bit) { perc_in_at=now; next.perc_entering=(uint8_t)(lane+1u); }
            if(!(v.overlays&bit)) perc_hit_at[lane]=0;
            else if(v.playing && v.overlay_hits&bit && (v.position!=old->position || !old->playing))
                perc_hit_at[lane]=now;
            unsigned elapsed=now-perc_hit_at[lane];
            unsigned phase=perc_hit_at[lane] && elapsed<2400000u?1u+elapsed/1200000u:0u;
            next.perc_phases|=(uint8_t)(phase<<(lane*2u));
            if(phase!=((model.perc_phases>>(lane*2u))&3u)) change|=1u<<(CONE0+lane);
        }
        if(!next.perc_entering && perc_in_at && now-perc_in_at<4800000u && model.perc_entering)
            next.perc_entering=(uint8_t)(((model.perc_entering-1u)&3u)+1u+
                (now-perc_in_at>=1200000u?4u:0u));
        if(next.perc_entering!=model.perc_entering) {
            if(model.perc_entering) change|=1u<<(CONE0+((model.perc_entering-1u)&3u));
            if(next.perc_entering) change|=1u<<(CONE0+((next.perc_entering-1u)&3u));
        }
        if(next.hot>=4 && hot_at && now-hot_at<14400000u) next.hot=model.hot;
    } else if(full || page!=PANEL_PERC) {
        perc_in_at=0;
        for(unsigned i=0;i<4;++i) perc_hit_at[i]=0;
    }
    if(full || (page!=PANEL_BASS && page!=PANEL_PERC)) hot_at=0;
    if(!full && next.hot!=model.hot) {
        change|=(1u<<STATUS)|(1u<<CLOCK)|(1u<<BODY);
        if(model.hot<4) change|=1u<<(KNOB0+model.hot);
        if(next.hot<4) change|=1u<<(KNOB0+next.hot);
    }
    if(page==PANEL_RACK && !p->rack_edit && !full) {
        if(p->rack_sound==model.panel.rack_sound) next.rack_last=model.rack_last;
        if(feedback.serial!=model.rack_serial && feedback.hits) {
            next.rack_last=feedback.sound;
            for(unsigned i=0;i<RACK_SOUNDS;++i) {
                if(feedback.hits&(1u<<i)) rack_hit_at[i]=now;
                if(feedback.cuts&(1u<<i)) rack_cut_at[i]=now;
                else if(feedback.hits&(1u<<i)) rack_cut_at[i]=0;
            }
        }
        if(p->stop_epoch!=model.panel.stop_epoch)
            for(unsigned i=0;i<RACK_SOUNDS;++i) rack_hit_at[i]=rack_cut_at[i]=0;
        for(unsigned i=0;i<RACK_SOUNDS;++i) {
            unsigned bit=1u<<i;
            if(rack_hit_at[i] && now-rack_hit_at[i]<2880000u) next.rack_ripple|=bit;
            if(rack_hit_at[i] && now-rack_hit_at[i]<1440000u) next.rack_press|=bit;
            if(rack_cut_at[i] && now-rack_cut_at[i]<6000000u) next.rack_cut|=bit;
        }
        rack_dirty|=(next.rack_press^model.rack_press)|(next.rack_ripple^model.rack_ripple)|
            (next.rack_cut^model.rack_cut);
        if((next.rack_last&15u)!=(model.rack_last&15u)) rack_header_pending=1;
        if(rack_header_pending && !(change&(1u<<RACK_LOOP)) && !(jobs&(1u<<RACK_PADS))) {
            change|=1u<<RACK_HEADER;
            rack_header_pending=0;
        }
        if(!(jobs&(1u<<RACK_PADS)) && rack_dirty) {
            unsigned limit=(change&((1u<<RACK_HEADER)|(1u<<RACK_LOOP)))?1u:3u;
            rack_batch=0;
            for(unsigned i=0;i<limit && rack_dirty;++i) {
                unsigned pressing=rack_dirty&next.rack_press;
                unsigned first=__builtin_ctz(pressing?pressing:(unsigned)rack_dirty);
                rack_batch|=1u<<first;
                rack_dirty&=(uint16_t)~(1u<<first);
            }
            change|=1u<<RACK_PADS;
        }
    } else if(full) {
        rack_dirty=rack_batch=rack_header_pending=0;
        for(unsigned i=0;i<RACK_SOUNDS;++i) rack_hit_at[i]=rack_cut_at[i]=0;
    }
    if(page==PANEL_HOME && !full) {
        /* Manual hits only: automatic overlays and sequenced 727 lanes never
         * light the Band finger pads. */
        if(feedback.serial!=model.rack_serial && feedback.manual) {
            unsigned sound=feedback.sound&15u;
            if(sound>=RACK_SOUNDS || !(feedback.manual&(1u<<sound)))
                sound=(unsigned)__builtin_ctz(feedback.manual);
            unsigned side=sound>=7u;
            if(model.pad_name && side!=pad_side) pad_dirty|=(uint8_t)(1u<<pad_side);
            pad_side=(uint8_t)side;
            pad_hit_at=now;
            next.pad_name=(uint8_t)(sound+1u);
        } else if(pad_hit_at && now-pad_hit_at<9600000u) next.pad_name=model.pad_name;
        if(next.pad_name && now-pad_hit_at<2880000u) next.pad_flash=next.pad_name;
        if(next.pad_name!=model.pad_name || next.pad_flash!=model.pad_flash) {
            pad_dirty|=(uint8_t)(1u<<pad_side);
            change|=1u<<PAD_FLASH;
        }
        if(!v.piano && beat!=model.beat && (v.pending<3 || old->pending<3)) change|=1u<<ENERGY_GAUGE;
        /* The gauge lands with the cue, even while the LIFT animation holds STATUS. */
        if(!v.piano && (board_part_energy(v.part,v.section)!=board_part_energy(old->part,old->section) ||
                        v.pending!=old->pending)) change|=1u<<ENERGY_GAUGE;
    } else if(full) { pad_hit_at=0; pad_dirty=0; }
    if(bandlike && !full && (pending!=model.pending || v.phrases_left!=old->phrases_left ||
                            v.next_bar!=old->next_bar || v.fill!=old->fill))
        change|=1u<<HORN;
    if(beat!=model.beat && !full) {
        change|=(1u<<BULB0)|(1u<<BULB1);
        if(bandlike) {
            unsigned lo=min(beat,model.beat), hi=max(beat,model.beat);
            if(hi-lo>2u) change|=1u<<CONES_ALL;
            else for(unsigned i=lo;i<=hi && i<8;++i) change|=1u<<(CONE0+i);
            if(pending || model.pending || board_count_active(v)) change|=1u<<HORN;
            if(!pending || !model.pending) change|=1u<<STICKER;
        }
    }
    if(page==PANEL_HOME || page==PANEL_BASS || page==PANEL_PERC) {
        unsigned cones=255u<<CONE0;
        unsigned kind=effects.kind, previous=motion.kind;
        if(kind==MOTION_SONG && (previous!=kind || (effects.frame!=motion.frame && effects.frame<=18)))
            change|=(1u<<SIGN)|(effects.frame==12?1u<<INFO:0u);
        if((kind==MOTION_SONG || kind==MOTION_CLOSE) && beat!=model.beat) change|=1u<<INFO;
        if(kind==MOTION_CLOSE && (previous!=kind || (effects.frame==8 && motion.frame!=8)))
            change|=(1u<<SIGN)|(1u<<LIGHTS)|(1u<<INFO)|(1u<<CLOCK);
        if((previous==MOTION_SONG || previous==MOTION_CLOSE) && kind!=previous)
            change|=(1u<<SIGN)|(1u<<INFO)|(1u<<LIGHTS)|(1u<<CLOCK);
        if(effects.ending!=motion.ending || (effects.ending && beat!=model.beat))
            change|=(1u<<SIGN)|(1u<<LIGHTS);
        if(effects.break_mode!=motion.break_mode) change|=(1u<<SIGN)|(1u<<STATUS)|(1u<<CLOCK)|(1u<<LIGHTS);
        if(kind==MOTION_BACK || kind==MOTION_FILL || previous==MOTION_BACK || previous==MOTION_FILL) {
            if(kind!=previous) change|=(1u<<STATUS)|(1u<<CLOCK)|(1u<<LIGHTS)|(1u<<LARGE_STICKER);
        }
        if(kind==MOTION_LIFT && (previous!=kind || (effects.frame/2u!=motion.frame/2u && effects.frame<=6))) {
            if(page==PANEL_HOME && !full) {
                change&=~((1u<<STATUS)|(1u<<CLOCK));
                if(previous!=MOTION_SONG && previous!=MOTION_CLOSE && next.next==model.next)
                    change&=~(1u<<INFO);
                change|=(1u<<MOTION_STATUS)|(1u<<CONES_ALL)|(1u<<LIGHTS)|(1u<<LARGE_STICKER);
                change&=~(cones|(1u<<STICKER));
                if(effects.frame>=6) change|=1u<<STATUS;
            }
        }
        if(page==PANEL_HOME && kind!=previous && (kind==MOTION_SONG || kind==MOTION_LIFT)) {
            effect_kind=(uint8_t)(kind==MOTION_SONG?0u:1u);
            effect_jobs=kind==MOTION_SONG?1u<<SIGN:
                (1u<<MOTION_STATUS)|(1u<<CONES_ALL)|(1u<<LIGHTS)|(1u<<LARGE_STICKER);
            effect_started=now;
            effect_compute=effect_transfer=effect_pixels=0;
        }
        if(kind!=previous && kind!=MOTION_SONG && kind!=MOTION_LIFT) effect_jobs=0;
        if(previous==MOTION_LIFT && kind!=previous)
            change|=(1u<<SIGN)|(1u<<INFO)|(1u<<STATUS)|(1u<<CLOCK)|(1u<<LIGHTS);
        if(kind==MOTION_LIFT && effects.frame>=6 && effects.phase!=motion.phase) {
            previous_phase=motion.phase;
            change|=(1u<<LIFT_BULB0)|(1u<<LIFT_BULB1);
        }
        if(effects.roll!=motion.roll) {
            unsigned lo=min(effects.roll,motion.roll), hi=max(effects.roll,motion.roll);
            for(unsigned i=lo;i<hi;++i) change|=1u<<(CONE0+i);
        }
        if(effects.break_mode && beat!=model.beat) change|=1u<<LIGHTS;
    }
    if(change&(1u<<CONES_ALL)) change&=~(255u<<CONE0);
    if(change&(1u<<LARGE_STICKER)) change&=~(1u<<STICKER);
    if(change&(1u<<LIGHTS)) change&=~((1u<<BULB0)|(1u<<BULB1)|(1u<<LIFT_BULB0)|(1u<<LIFT_BULB1));
    if(measurement==1 && ((change&255u) || (change&((1u<<MOTION_STATUS)|(1u<<LARGE_STICKER))) ||
       (page==PANEL_SET && (change&(1u<<ORDER_ROW))) ||
       (page==PANEL_HOME && (change&(1u<<PAD_FLASH))))) measurement=0;
    motion=effects;
    if(!change) {
        if(touching && !touch_queued && now-touch_at>384000u) touching=0;
        model.view=v;
        model.rack_serial=feedback.serial;
        return;
    }
    if(touching && change&~((1u<<(STICKER+1))-256u)) touch_queued=1;
    if(beat==model.beat) next.old_beat=model.old_beat;
    if(full || !jobs) {
        jobs=change;
        stripe_row=0;
        compute=transfer=pixels=0;
        redraw_started=now;
        uint32_t beat_jobs=(((1u<<(STICKER+1))-1u)&~255u)|(1u<<LIFT_BULB0)|(1u<<LIFT_BULB1)|(1u<<CONES_ALL)|(1u<<RACK_OLD_STEP)|(1u<<RACK_NEW_STEP);
        if(page==PANEL_SET) beat_jobs&=~(1u<<ORDER_ROW);
        if(page==PANEL_HOME) beat_jobs&=~(1u<<PAD_FLASH);
        measurement=full?3u:(!(change&~beat_jobs)?1u:0u);
    } else {
        jobs|=change;
        if(change&(1u<<working_job)) stripe_row=0;
    }
    model=next;
}

static unsigned stage_work(void)
{
    if(!jobs) return 0;
    static const uint8_t first[]={KNOB0,KNOB0+1,KNOB0+2,KNOB0+3,NOTICE,RACK_OLD_STEP,RACK_NEW_STEP,STATUS,MOTION_STATUS,CLOCK,SIGN,INFO,FOOTER,BODY,TABS,LIGHTS};
    unsigned live_keys=measurement!=3 && model.panel.page==PANEL_HOME;
    unsigned job=working_job;
    if(!stripe_row) {
        region_compute=region_transfer=region_pixels=0;
        uint32_t ready=jobs;
        if(measurement==3 && !notice_priority && (jobs&~(1u<<NOTICE)))
            ready&=~(1u<<NOTICE);
        job=0;
        while(!(ready&(1u<<job))) ++job;
        for(unsigned i=0;i<sizeof first;++i) if(ready&(1u<<first[i])) { job=first[i]; break; }
        /* A queued part or changed layer should light its key before the
         * countdown and decoration. A full page still draws its whole body. */
        if(live_keys && (ready&(1u<<BODY))) job=BODY;
        if(touching && model.panel.page==PANEL_HOME && (ready&(1u<<PAD_FLASH))) job=PAD_FLASH;
        if(touching && model.panel.page==PANEL_RACK && !model.panel.rack_edit &&
           (ready&(1u<<RACK_PADS))) job=RACK_PADS;
        if(model.panel.page==PANEL_SET && (ready&(1u<<ORDER_ROW))) job=ORDER_ROW;
        working_job=(uint8_t)job;
    }
    Rect rect=job_area(job,job==BULB0?model.old_beat:model.beat);
    if(live_keys && job==BODY) rect=(Rect){32,142,174,32};
    if(measurement==3) {
        /* The dedicated banner is y=184..195, crossing both bands. */
        if(job==BODY) rect.h=52;
        if(job==FOOTER) { rect.y=196; rect.h=44; }
    }
    unsigned rows=min(rect.h-stripe_row,3360u/rect.w);
    tile=(Rect){rect.x,(uint8_t)(rect.y+stripe_row),rect.w,(uint8_t)rows};
    uint32_t start=board_ticks();
    stage_paint(&model);
    uint32_t sent=board_ticks();
    lcd_blit(tile.x,tile.y,tile.w,tile.h,stripe);
#ifdef STAGE_TRANSFER_POLL
    STAGE_TRANSFER_POLL(tile.w*tile.h);
#endif
    lcd_sync();
    uint32_t done=board_ticks();
    compute+=sent-start;
    transfer+=done-sent;
    pixels+=tile.w*tile.h;
    if(effect_jobs&(1u<<job)) {
        effect_compute+=sent-start;
        effect_transfer+=done-sent;
        effect_pixels+=tile.w*tile.h;
    }
    region_compute+=sent-start;
    region_transfer+=done-sent;
    region_pixels+=tile.w*tile.h;
    stripe_row=(uint8_t)(stripe_row+rows);
    if(stripe_row==rect.h) {
        if(job==ORDER_ROW && model.panel.page==PANEL_SET)
            order_rows&=(uint16_t)~order_row_bit((rect.y-ORDER_TOP)/ORDER_HEIGHT);
        if(job==RACK_PADS && model.panel.page==PANEL_RACK && !model.panel.rack_edit)
            rack_batch&=(uint16_t)(rack_batch-1u);
        if(job==PAD_FLASH && model.panel.page==PANEL_HOME)
            pad_dirty&=(uint8_t)~(1u<<(rect.x!=0));
        if(!((job==PAD_FLASH && model.panel.page==PANEL_HOME && pad_dirty) ||
             (job==ORDER_ROW && model.panel.page==PANEL_SET && order_rows) ||
             (job==RACK_PADS && model.panel.page==PANEL_RACK && !model.panel.rack_edit && rack_batch)))
            jobs&=~(1u<<job);
        if(job==NOTICE) notice_priority=0;
        if(effect_jobs&(1u<<job)) {
            effect_jobs&=~(1u<<job);
            if(!effect_jobs) {
                uint32_t receipt[]={effect_compute,effect_transfer,done-effect_started,effect_pixels};
                for(unsigned i=0;i<4;++i)
                    if(compas_animation_ticks[effect_kind][i]<receipt[i])
                        compas_animation_ticks[effect_kind][i]=receipt[i];
            }
        }
        stripe_row=0;
        if(touching && touch_queued && (job<8 || job>=KNOB0)) {
            high_water(1,region_compute,region_transfer,done-touch_at,region_pixels);
            touching=0;
        }
    }
    if(!jobs && measurement) high_water(measurement==1?0:2,compute,transfer,done-redraw_started,pixels);
    return 1;
}

static void splash(void)
{
    band(0,240,PLUM);
    lcd_fill(0,0,80,6,PINK);
    lcd_fill(80,0,80,6,GOLD);
    lcd_fill(160,0,80,6,MINT);
    brand_line((240-WORD_WIDTH)/2,19,wordmark,WORD_WIDTH,WORD_HEIGHT,GOLD);
    brand_line((240-SUBTITLE_WIDTH)/2,50,subtitle,SUBTITLE_WIDTH,SUBTITLE_HEIGHT,MINT);
    portrait(66,78,108);
    text_line(43,196,"JOHNNY CANALES",14,CREAM,PLUM);
    text_line(32,217,"TAKE IT AWAY    ",16,PINK,PLUM);
}
