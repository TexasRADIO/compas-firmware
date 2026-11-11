/* V3 Percussion icons. Moving heads and sparks stay inside per-lane jobs. */
static uint16_t shaded(uint16_t ink,unsigned amount)
{
    return (uint16_t)((((ink>>11)*amount/100u)<<11) |
        ((((ink>>5)&63u)*amount/100u)<<5) | ((ink&31u)*amount/100u));
}

static void perc_conga(unsigned cx,unsigned scale,uint16_t ink,unsigned on,unsigned hit)
{
    unsigned top=140-28u*scale/100u, w=16u*scale/100u;
    for(unsigned y=top+4;y<160;++y) {
        unsigned taper=(y-top)*w/100u;
        paint(cx-w+taper,y,2u*(w-taper)+1u,1,shaded(ink,75));
    }
    paint(cx-w+2,139,2u*w-3,2,INK);
    paint(cx-w+4,150,2u*w-7,2,INK);
    if(hit) ellipse(cx,top+4,w+hit*2u,5u+hit,hit==1?CREAM:ink);
    ellipse(cx,top+4,w,4,hit==1?CREAM:on?RGB565(227,188,139):DIM);
}

static void perc_maracas(unsigned cx,unsigned scale,uint16_t ink,unsigned on,unsigned hit)
{
    unsigned radius=9u*scale/100u;
    for(unsigned side=0;side<2;++side) {
        unsigned x=cx+(side?12:-12);
        unsigned top=133-15u*scale/100u+(hit==1 && side?3u:0u);
        unsigned handle=cx+(side?6:-6);
        unsigned stem=139-15u*scale/100u;
        if(side) line_between(handle,155,x-handle,stem,on?WOOD:DIM);
        else line_between(x,stem,handle-x,155,on?WOOD:DIM);
        ellipse(x,top,radius,radius+1u,shaded(ink,85));
        ellipse(x-2,top-2,2,2,hit?CREAM:ink);
        if(hit) {
            paint(x+(side?9:-10),top-6,2,2,CREAM);
            paint(x+(side?11:-12),top+2,2,2,ink);
        }
    }
}

static void perc_timbal(unsigned cx,unsigned scale,uint16_t ink,unsigned on,unsigned hit)
{
    unsigned top=137-10u*scale/100u, w=9u*scale/100u;
    for(unsigned side=0;side<2;++side) {
        unsigned x=cx+(side?11:-11);
        paint(x-w,top,2u*w+1,153u-top,shaded(ink,70));
        ellipse(x,top,w,3,hit==1 && !side?CREAM:on?RGB565(227,188,139):DIM);
        paint(x-1,153,2,7,DIM);
    }
    line_between(cx-11,top-(hit?1u:8u),14,top-(hit?13u:20u),on?CREAM:DIM);
    if(hit) {
        paint(cx-19,top-7,2,2,GOLD);
        paint(cx-15,top-10,2,3,GOLD);
        paint(cx-8,top-8,3,2,GOLD);
    }
}

static void perc_bell(unsigned cx,unsigned scale,uint16_t ink,unsigned on,unsigned hit)
{
    unsigned top=143-24u*scale/100u;
    for(unsigned y=top;y<157;++y) {
        unsigned w=(8u+(y-top)/5u)*scale/100u;
        paint(cx-w,y,2u*w+1u,1,shaded(ink,80));
    }
    paint(cx-3,top-5,6,5,DIM);
    paint(cx-4,top+4,2,153u-top,on?ink:LINE);
    if(hit) {
        unsigned x=12u+3u*hit, y=top+6u;
        line_between(cx-x-2u,y-6,3,y-1,CREAM);
        line_between(cx-x-2u,y+7,3,y+2,CREAM);
        line_between(cx+x,y-1,3,y-6,CREAM);
        line_between(cx+x,y+2,3,y+7,CREAM);
    }
}

static void perc_instruments(BoardView v,unsigned phases,unsigned entering,unsigned hot)
{
    static const char *const names[]={"CONGA","MARACA","TIMBAL","BELL"};
    paint(0,72,240,116,INK);
    for(unsigned lane=0;lane<4;++lane) {
        unsigned cx=30u+lane*60u;
        if(!visible(cx-30u,74,60,113)) continue;
        if(lane) paint(cx-30u,76,1,108,LINE);
        unsigned level=v.overlay_levels[lane], scale=55u+level/2u;
        unsigned on=(v.overlays>>lane)&1u, hit=(phases>>(lane*2u))&3u;
        uint16_t ink=on?knob_ink[lane]:DIM;
        if(on && level) ellipse(cx,162,26u*level/100u,4,shaded(ink,35u+level*2u/5u));
        if(lane==0) perc_conga(cx,scale,ink,on,hit);
        else if(lane==1) perc_maracas(cx,scale,ink,on,hit);
        else if(lane==2) perc_timbal(cx,scale,ink,on,hit);
        else perc_bell(cx,scale,ink,on,hit);
        paint(cx-22,170,44,3,LINE);
        if(on) paint(cx-22,170,44u*level/100u,3,ink);
        centered(cx,174,on?names[lane]:"OUT",ink,INK);
        if(lane==hot) {
            char n[4]; decimal(n,level);
            paint(cx-29,74,58,3,ink);
            value_pop(cx,78,n,lane);
        }
        if(entering && ((entering-1u)&3u)==lane)
            centered(cx,106,"IN",entering>4u?ink:CREAM,INK);
    }
}
