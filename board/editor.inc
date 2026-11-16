/* SPDX-License-Identifier: GPL-3.0-only
 * Compás 1.2 foreground adapter for the shared Big Beat Editor protocol.
 * Include after usb/runtime.inc so it can share the framed SysEx queue.
 */
#include "editor_gen.h"

enum {
    ED_FIELD_BPM=0x1001, ED_FIELD_FEEL=0x1002,
    ED_FIELD_GROOVE=0x1100, ED_FIELD_SECTION=0x1101,
    ED_FIELD_ROOT=0x1104, ED_FIELD_CLAVE=0x1105,
    ED_FIELD_LEVEL_BASE=0x1200, ED_FIELD_MUTE_BASE=0x1210,
    ED_FIELD_FX_BASE=0x1300
};
typedef struct {
    uint8_t connected,tx_open,tx_domain,staged_valid,staged_noop;
    uint16_t tx_id;
    uint32_t revision;
    BoardCommand staged;
    uint8_t reply[136];
    uint16_t reply_len;
} CompasEditor;
static CompasEditor editor;

static uint16_t ed_read14(const uint8_t *p)
{ return (uint16_t)(((uint16_t)(p[0]&127u)<<7)|(p[1]&127u)); }
static uint32_t ed_read28(const uint8_t *p)
{ return (uint32_t)(p[0]&127u)<<21|(uint32_t)(p[1]&127u)<<14|(uint32_t)(p[2]&127u)<<7|(p[3]&127u); }
static uint32_t ed_crc_byte(uint32_t crc,uint8_t value)
{
    crc^=value;
    for(unsigned i=0;i<8;++i) crc=(crc>>1)^(0xEDB88320u&(uint32_t)-(int32_t)(crc&1u));
    return crc;
}
static uint32_t ed_crc(const uint8_t *p,unsigned n)
{
    uint32_t crc=0xFFFFFFFFu;
    while(n--) crc=ed_crc_byte(crc,*p++);
    return crc^0xFFFFFFFFu;
}
static void ed_begin(uint8_t type,uint16_t seq)
{
    static const uint8_t prefix[]={0xF0,BB_ED_MFR_0,BB_ED_MFR_1,BB_ED_MFR_2,BB_ED_DEVICE,BB_ED_FAMILY};
    editor.reply_len=0;
    for(unsigned i=0;i<sizeof prefix;++i) editor.reply[editor.reply_len++]=prefix[i];
    editor.reply[editor.reply_len++]=type;
    editor.reply[editor.reply_len++]=(uint8_t)(seq>>7); editor.reply[editor.reply_len++]=(uint8_t)(seq&127u);
}
static int ed_byte(uint8_t value)
{
    if(value>127u || editor.reply_len>=sizeof editor.reply-1u) return 0;
    editor.reply[editor.reply_len++]=value; return 1;
}
static void ed14(uint16_t value) { ed_byte((uint8_t)(value>>7));ed_byte((uint8_t)(value&127u)); }
static void ed32(uint32_t value)
{
    ed_byte((uint8_t)((value>>28)&15u));ed_byte((uint8_t)((value>>21)&127u));
    ed_byte((uint8_t)((value>>14)&127u));ed_byte((uint8_t)((value>>7)&127u));ed_byte((uint8_t)(value&127u));
}
static void ed_end(void)
{
    editor.reply[editor.reply_len++]=0xF7u;
    if(ota_wire_send(editor.reply,editor.reply_len)) editor.reply_len=0;
}
static void ed_result(uint8_t type,uint16_t seq,uint8_t result)
{ ed_begin(type,seq);ed_byte(result);ed_end(); }

typedef struct { uint8_t bytes[64];uint16_t length; } EditorSnapshot;
static int ed_put8(EditorSnapshot *s,uint8_t value)
{ if(s->length>=sizeof s->bytes) return 0;s->bytes[s->length++]=value;return 1; }
static int ed_put16(EditorSnapshot *s,uint16_t value)
{ return ed_put8(s,(uint8_t)(value>>8))&&ed_put8(s,(uint8_t)value); }
static int ed_put32(EditorSnapshot *s,uint32_t value)
{ return ed_put8(s,(uint8_t)(value>>24))&&ed_put8(s,(uint8_t)(value>>16))&&ed_put8(s,(uint8_t)(value>>8))&&ed_put8(s,(uint8_t)value); }
static int ed_section(EditorSnapshot *s,uint8_t scope)
{ if(!ed_put8(s,scope)||!ed_put8(s,0)||!ed_put8(s,0)) return -1;return (int)s->length-2; }
static void ed_section_end(EditorSnapshot *s,int at)
{ uint16_t n=(uint16_t)(s->length-(unsigned)at-2u);s->bytes[at]=(uint8_t)(n>>8);s->bytes[at+1]=(uint8_t)n; }
static int ed_snapshot(EditorSnapshot *s)
{
    BoardView view=board_view(0);uint8_t fx[FX_PARAMS];int at;
    s->length=0;board_fx_view(fx);
    if(!ed_put32(s,editor.revision)) return 0;
    at=ed_section(s,1);
    if(at<0||!ed_put16(s,(uint16_t)(view.bpm*10u))||!ed_put8(s,view.feel)) return 0;
    ed_section_end(s,at);
    at=ed_section(s,9);
    if(at<0||!ed_put8(s,view.playing)||!ed_put8(s,view.section)||!ed_put8(s,view.pending)||
       !ed_put8(s,view.fill)||!ed_put16(s,view.position)||!ed_put16(s,(uint16_t)cp_status(CP_PHRASE))) return 0;
    ed_section_end(s,at);
    at=ed_section(s,0x12);if(at<0||!ed_put8(s,view.study)||!ed_put8(s,(uint8_t)board_root(view))||!ed_put8(s,(uint8_t)board_clave(view))) return 0;
    for(unsigned role=0;role<CP_ROLES;++role) if(!ed_put8(s,view.levels[role])) return 0;
    if(!ed_put8(s,view.muted)) return 0;
    ed_section_end(s,at);
    at=ed_section(s,5);if(at<0) return 0;
    for(unsigned i=0;i<FX_PARAMS;++i) if(!ed_put8(s,fx[i])) return 0;
    ed_section_end(s,at);return 1;
}
static unsigned ed_pack7(const uint8_t *in,unsigned n,uint8_t *out,unsigned capacity)
{
    unsigned at=0,pos=0;
    while(pos<n) {
        unsigned count=n-pos>7u?7u:n-pos;uint8_t msb=0;
        if(at+count+1u>capacity) return 0;
        for(unsigned i=0;i<count;++i) if(in[pos+i]&128u) msb|=(uint8_t)(1u<<i);
        out[at++]=msb;
        for(unsigned i=0;i<count;++i) out[at++]=in[pos+i]&127u;
        pos+=count;
    }
    return at;
}
static void ed_send_snapshot(uint16_t seq)
{
    EditorSnapshot snapshot;uint8_t packed[76];
    if(!ed_snapshot(&snapshot)) { ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return; }
    unsigned n=ed_pack7(snapshot.bytes,snapshot.length,packed,sizeof packed);
    if(!n) { ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return; }
    ed_begin(BB_ED_MSG_SNAPSHOT_CHUNK,seq);ed14(0);ed14(1);
    for(unsigned i=0;i<n;++i) ed_byte(packed[i]);ed32(ed_crc(snapshot.bytes,snapshot.length));ed_end();
    ed_begin(BB_ED_MSG_SNAPSHOT_END,seq);ed_end();
}

static int ed_map(uint8_t scope,uint8_t target,uint16_t field,uint16_t value)
{
    BoardCommand command={0,0,0};editor.staged_noop=0;
    if(scope==7u && field==ED_FIELD_BPM && value>=600u&&value<=1800u&&value%10u==0u)
        command=(BoardCommand){CP_BPM,(uint8_t)(value/10u),0};
    else if(scope==7u && field==ED_FIELD_FEEL && value<=100u)
        command=(BoardCommand){CP_FEEL,(uint8_t)value,0};
    else if(scope==0x10u && field==ED_FIELD_GROOVE && value<BOARD_GROOVE_PRESETS)
        command=(BoardCommand){BOARD_GROOVE_CUE,(uint8_t)value,0};
    else if(scope==0x10u && field==ED_FIELD_SECTION && value<3u)
        command=(BoardCommand){CP_SECTION,(uint8_t)value,0};
    else if(scope==0x10u && field==ED_FIELD_ROOT && value<12u)
        command=(BoardCommand){CP_ROOT,(uint8_t)value,0};
    else if(scope==0x10u && field==ED_FIELD_CLAVE && value<2u)
        command=(BoardCommand){CP_CLAVE,(uint8_t)value,0};
    else if(scope==0x12u && target<CP_ROLES && field==ED_FIELD_LEVEL_BASE+target && value<=100u)
        command=(BoardCommand){CP_LEVEL,target,(uint8_t)value};
    else if(scope==0x12u && target<CP_ROLES && field==ED_FIELD_MUTE_BASE+target && value<2u) {
        unsigned current=(cp_status(CP_MUTES)>>target)&1u;
        if(current==value) editor.staged_noop=1;
        else command=(BoardCommand){CP_MUTE,target,0};
    }
    else if(scope==5u && target<FX_PARAMS && field==ED_FIELD_FX_BASE+target &&
            value<=(target==FX_TIEMPO?FX_DIVISIONS-1u:target==FX_REPITE?90u:100u))
        command=(BoardCommand){BOARD_FX,target,(uint8_t)value};
    else return 0;
    editor.staged=command;return 1;
}

static void ed_hello(const uint8_t *payload,unsigned length,uint16_t seq)
{
    if(!length||payload[0]!=BB_ED_PROTOCOL_VERSION) { ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_NOT_SUPPORTED);return; }
    ed_begin(BB_ED_MSG_HELLO_REPLY,seq);ed_byte(BB_ED_PROTOCOL_VERSION);ed_byte(BB_ED_PRODUCT_COMPAS);ed14(12);
    ed32(0x434F4D50u);ed_byte(112);ed_byte(0x45);ed14(29);ed14(0);ed14(0);ed_end();editor.connected=1;
}
static void ed_rx(const uint8_t *data,unsigned length)
{
    if(length<3u) return;
    uint8_t type=data[0];uint16_t seq=ed_read14(data+1);const uint8_t *p=data+3;unsigned n=length-3u;
    if(type==BB_ED_MSG_HELLO) { ed_hello(p,n,seq);return; }
    if(type==BB_ED_MSG_SNAPSHOT_BEGIN) { ed_send_snapshot(seq);return; }
    if(type==BB_ED_MSG_PING) { ed_begin(BB_ED_MSG_PONG,seq);ed_end();return; }
    if(type==BB_ED_MSG_TX_BEGIN) {
        if(!editor.connected||n<7u||editor.tx_open) { ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return; }
        uint32_t base=ed_read28(p);
        if(base&&base!=editor.revision) { ed_begin(BB_ED_MSG_CONFLICT,seq);ed32(editor.revision);ed_end();return; }
        editor.tx_domain=p[4];editor.tx_id=ed_read14(p+5);editor.tx_open=1;editor.staged_valid=0;
        ed_result(BB_ED_MSG_ACK,seq,BB_ED_RESULT_OK);return;
    }
    if(type==BB_ED_MSG_SET) {
        if(!editor.tx_open||editor.staged_valid||n<6u||p[0]!=editor.tx_domain||
           !ed_map(p[0],p[1],ed_read14(p+2),ed_read14(p+4))) {
            ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_UNKNOWN_FIELD);return;
        }
        editor.staged_valid=1;ed_result(BB_ED_MSG_APPLIED,seq,BB_ED_RESULT_OK);return;
    }
    if(type==BB_ED_MSG_TX_COMMIT) {
        if(!editor.tx_open||!editor.staged_valid||n<2u||ed_read14(p)!=editor.tx_id) { ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return; }
        if(!editor.staged_noop&&board_submit(editor.staged)!=CP_OK) { ed_result(BB_ED_MSG_BUSY,seq,BB_ED_RESULT_BUSY);return; }
        ++editor.revision;editor.tx_open=editor.staged_valid=0;ed_result(BB_ED_MSG_ACK,seq,BB_ED_RESULT_QUEUED);return;
    }
    if(type==BB_ED_MSG_TX_ABORT) { editor.tx_open=editor.staged_valid=0;ed_result(BB_ED_MSG_ACK,seq,BB_ED_RESULT_OK);return; }
    if(type==BB_ED_MSG_PLAY||type==BB_ED_MSG_STOP) {
        BoardCommand command={(uint8_t)(type==BB_ED_MSG_PLAY?CP_START:CP_STOP),0,0};
        unsigned result=board_submit(command);
        ed_result(result==CP_OK?BB_ED_MSG_ACK:BB_ED_MSG_BUSY,seq,
                  result==CP_OK?BB_ED_RESULT_QUEUED:BB_ED_RESULT_BUSY);return;
    }
    if(type==BB_ED_MSG_SETTLE) { ed_begin(BB_ED_MSG_SETTLE_REPLY,seq);ed32(editor.revision);ed_byte(editor.tx_open);ed_end();return; }
    ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_NOT_SUPPORTED);
}

static void compas_editor_init(void)
{
    for(unsigned i=0;i<sizeof editor;++i) ((uint8_t *)&editor)[i]=0;
    editor.revision=1;
}
static void compas_editor_service(void)
{
    const uint8_t *frame;uint32_t length;
    if(ota_frame_get(&frame,&length)&&length>=8u&&frame[0]==BB_ED_MFR_0&&frame[1]==BB_ED_MFR_1&&
       frame[2]==BB_ED_MFR_2&&frame[3]==BB_ED_DEVICE&&frame[4]==BB_ED_FAMILY) {
        ed_rx(frame+5u,(unsigned)(length-5u));ota_frame_done();
    }
}
