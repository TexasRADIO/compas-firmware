/* V3 pad rack. The icon and cut-pair order follow the 727 voice map. */
static const char *const rack_names[]={
    "HI BONGO","LO BONGO","MUTE CONGA","OPEN CONGA","LOW CONGA","HI TIMBAL","LO TIMBAL",
    "HI AGOGO","LO AGOGO","CABASA","MARACAS","SHORT WHSTL","LONG WHSTL","QUIJADA","STAR CHIME"
};
static const uint8_t rack_family[]={0,0,1,1,1,2,2,3,3,4,4,5,5,6,7,8};
static const char rack_groups[]="AABB   CCDDEE   ";
static const char rack_shortcuts[]="ZXCVBNMAWSEDFTGY";

static void outline(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t ink)
{
    paint(x,y,w,1,ink); paint(x,y+h-1u,w,1,ink);
    paint(x,y,1,h,ink); paint(x+w-1u,y,1,h,ink);
}

static void rack_icon(unsigned sound,unsigned x,unsigned y,uint16_t ink,uint16_t bg)
{
    switch(rack_family[sound]) {
    case 0:
        paint(x-9,y,9,7,ink); paint(x+1,y,8,6,ink);
        ellipse(x-5,y,5,2,CREAM); ellipse(x+5,y,4,2,CREAM);
        break;
    case 1:
        ellipse(x,y,7,3,CREAM);
        for(unsigned i=2;i<=8;++i) paint(x-7+i/3u,y+i,15u-2u*(i/3u),1,ink);
        break;
    case 2:
        paint(x-8,y,17,6,ink); ellipse(x,y,8,2,CREAM);
        line_between(x+1,y-7,9,y-3,CREAM);
        break;
    case 3:
        paint(x-7,y-4,5,11,ink); paint(x+2,y-2,5,9,ink);
        paint(x-6,y+7,12,1,CREAM);
        break;
    case 4:
        ellipse(x,y-1,6,6,ink); paint(x-1,y+4,2,6,CREAM);
        for(unsigned i=0;i<3;++i) paint(x-4+i*3u,y-3+(i&1u)*3u,2,2,CREAM);
        break;
    case 5:
        paint(x-9,y-2,13,5,ink); ellipse(x+4,y,5,4,ink);
        paint(x-5,y-2,2,2,bg);
        break;
    case 6:
        line_between(x-9,y-5,6,y+4,CREAM);
        paint(x-3,y+4,8,2,CREAM);
        line_between(x+5,y+4,6,y-5,CREAM);
        for(unsigned i=0;i<3;++i) paint(x-4+i*4u,y,2,3,ink);
        break;
    case 7:
        for(unsigned i=0;i<5;++i) {
            paint(x-i,y-7+i,2u*i+1u,1,ink);
            paint(x-i,y+7-i,2u*i+1u,1,ink);
        }
        paint(x-8,y-2,17,5,ink);
        break;
    default:
        line_between(x-5,y-5,10,y,PINK);
        line_between(x-5,y+5,10,y,PINK);
        break;
    }
}

static void rack_ring(unsigned cx,unsigned cy,unsigned rx,unsigned ry,uint16_t ink)
{
    unsigned ys=ry*ry, xs=rx*rx;
    for(unsigned row=0;row<=ry;++row) {
        unsigned x=rx;
        while(x*x*ys>xs*(ys-row*row)) --x;
        for(unsigned side=0;side<2;++side) {
            unsigned y=side?cy+row:cy-row;
            if(x<=2) paint(cx-x,y,2u*x+1u,1,ink);
            else {
                paint(cx-x,y,1,1,ink);
                paint(cx+x,y,1,1,ink);
            }
        }
    }
}

static void rack_header(BoardView v,unsigned sound)
{
    unsigned last=sound&15u, roll=board_roll_rate(v);
    uint16_t color=pad_colors[last];
    paint(0,24,240,23,PLUM);
    rack_icon(last,14,33,color,PLUM);
    static const char *const rates[]={"ARP","1/8","1/8T","1/16","1/32"};
    static const char *const times[]={"","x8","x8T","x16","x32"};
    /* The subtitle goes first: its blank accent rows (y 40-41) lie under the
     * name's last two rows, so the name must paint after it. */
    if(roll) print(5,40,144,times[roll],1,CYAN,PLUM,0);
    else if(v.next_groove<BOARD_GROOVE_PRESETS)
        print(5,40,144,board_groove_title(v.next_groove),1,PINK,PLUM,0);
    else print(5,40,144,"PRESETS / SEQ EDIT",1,DIM2,PLUM,0);
    print(29,24,121,rack_names[last],2,color,PLUM,0);
    static const char *const labels[]={"ROLL","REC","ACC"};
    for(unsigned i=0;i<3;++i) {
        uint16_t paper=i==0 && roll?CYAN:i==1 && board_recording(v)?RED:i==2 && v.accent?PINK:LINE;
        paint(152+i*29u,27,27,10,paper);
        centered(165+i*29u,26,labels[i],paper==LINE?DIM2:INK,paper);
    }
    print(153,39,24,rates[roll],1,roll?CYAN:DIM,PLUM,0);
    print(182,39,24,"REC",1,DIM2,PLUM,0);
    print(211,39,24,"SCL",1,DIM2,PLUM,0);
}

static void rack_pads(const Panel *p,BoardView v,unsigned press,unsigned ripple,unsigned cuts,unsigned last)
{
    static const char *const top[]={"HI","LO","MUTE","OPEN","LOW","HI","LO","HI",
        "LO","CABA","MARA","SHORT","LONG","QUIJA","STAR","ACC"};
    static const char *const bottom[]={"BONGO","BONGO","CONGA","CONGA","CONGA","TIMBL","TIMBL","AGOGO",
        "AGOGO","SA","CAS","WHSTL","WHSTL","DA","CHIME","ENT"};
    for(unsigned i=0;i<16;++i) {
        unsigned x=4+i%4u*58u, y=49+i/4u*30u;
        if(!visible(x,y,55,28)) continue;
        unsigned bit=1u<<i, down=(press&bit)!=0, cut=(cuts&bit)!=0;
        unsigned inset=down?2u:0u;
        uint16_t ink=cut?DIM:pad_colors[i], paper=cut?INK:PLUM;
        if(down) paper=(last&128u) && (last&15u)==i?CREAM:ink;
        if(i==15 && v.accent) paper=PINK;
        uint16_t text=paper==PLUM?CREAM:paper==INK?DIM:INK;
        paint(x,y,55,28,INK);
        paint(x+inset,y+inset,55u-2u*inset,28u-2u*inset,paper);
        paint(x+inset,y+inset,55u-2u*inset,2,ink);
        if(ripple&bit)
            rack_ring(x+27u,y+14u,down?17u:25u,down?9u:12u,down?CREAM:shaded(ink,70));
        rack_icon(i,x+12u,y+14u+inset,down?INK:ink,paper);
        print(x+24,y+4+inset,30,i==0?lang_bongo_hi[panel_lang]:i==1?lang_bongo_lo[panel_lang]:top[i],1,text,paper,0);
        print(x+24,y+13+inset,30,bottom[i],1,text,paper,0);
        char key[]={rack_shortcuts[i],0};
        print(x+3,y+19,6,key,1,down?INK:DIM,paper,0);
        if(rack_groups[i]!=' ') {
            char group[]={rack_groups[i],0};
            print(x+47,y+1,6,group,1,paper==ink?INK:ink,paper,0);
        }
        if(cut) {
            paint(x+14,y+10,27,10,PINK);
            centered(x+27u,y+10,"CUT",INK,PINK);
        }
        if(!down && i==p->rack_sound) outline(x,y,55,28,CREAM);
    }
}

static void rack_loop(BoardView v)
{
    paint(0,169,240,17,INK);
    if(v.piano || !(v.rack_sequence || board_recording(v))) return;
    unsigned count=16, offset=v.position&count;
    unsigned step=v.position-offset, width=208u/count;
    uint16_t ink=pad_colors[v.rack_sequence_sound];
    print(4,173,24,"LOOP",1,board_recording(v)?RED:DIM,INK,0);
    for(unsigned i=0;i<count;++i) {
        unsigned x=30+i*width, bit=1u<<(offset+i);
        paint(x+1,171,width-2u,12,v.rack_steps&bit?ink:(i&3u)?LINE:DIM);
        if(v.playing && !board_count_active(v) && step==i)
            outline(x,170,width,14,CREAM);
    }
}
