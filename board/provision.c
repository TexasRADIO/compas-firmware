#include "provision.h"

static uint8_t observed[PROVISION_BYTES],state[PROVISION_BYTES],buffer[256],header[12];
static const uint8_t marker[8]={'C','M','P','A','S','I','M','1'}; /* offline contract */

static void copy(uint8_t *d,const uint8_t *s,unsigned n) { while(n--) *d++=*s++; }
static int equal(const uint8_t *a,const uint8_t *b,unsigned n)
{ while(n--) if(*a++!=*b++) return 0;return 1; }
static void word(uint8_t *p,uint32_t x)
{ for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(x>>(i*8)); }
static unsigned operation(unsigned step,unsigned *size)
{
    if(step<3) {*size=4096;return (2-step)*4096;}
    if(step<18) {unsigned off=(step-3)*256;*size=PROVISION_PAYLOAD-off; if(*size>256)*size=256;return 256+off;}
    *size=step==18?12:8;return step==18?0:8192;
}
static uint8_t value(unsigned step,unsigned i,const uint8_t *payload)
{ return step<3?255:step<18?payload[(step-3)*256+i]:step==18?header[i]:marker[i]; }
static int classify(const uint8_t *original,const uint8_t *payload)
{
    copy(state,original,sizeof state);
    for(unsigned step=0;step<20;++step) {
        unsigned size,off=operation(step,&size),n=0;
        if(equal(observed,state,off) && equal(observed+off+size,state+off+size,sizeof state-off-size)) {
            while(n<size && observed[off+n]==value(step,n,payload)) ++n;
            if(equal(observed+off+n,state+off+n,size-n)) return step==19 && n==size?2:1;
        }
        for(unsigned i=0;i<size;++i) state[off+i]=value(step,i,payload);
    }
    return 0;
}
static int verify_region(unsigned step,const uint8_t *payload)
{
    unsigned size,off=operation(step,&size);
    for(unsigned n=0;n<size;n+=sizeof buffer) {
        unsigned count=size-n;if(count>sizeof buffer)count=sizeof buffer;
        if(provision_read(PROVISION_BASE+off+n,buffer,count)) return 0;
        for(unsigned i=0;i<count;++i) if(buffer[i]!=value(step,n+i,payload)) return 0;
    }
    return 1;
}
static int verify_all(const uint8_t *payload,unsigned committed)
{
    for(unsigned i=0;i<sizeof state;++i)state[i]=255;
    copy(state+256,payload,PROVISION_PAYLOAD);copy(state,header,sizeof header);
    if(committed)copy(state+8192,marker,sizeof marker);
    return !provision_read(PROVISION_BASE,observed,sizeof observed) && equal(state,observed,sizeof state);
}
int provision_check(const uint8_t original[PROVISION_BYTES],const uint8_t payload[PROVISION_PAYLOAD])
{
    if(equal(original+8192,marker,sizeof marker))return -2;
    uint32_t crc=~0u;
    for(unsigned i=0;i<4+PROVISION_PAYLOAD;++i) {
        crc^=i<4?(i==0?1:0):payload[i-4];
        for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0);
    }
    word(header,0x32535043);word(header+4,1);word(header+8,~crc);
    if(provision_read(PROVISION_BASE,observed,sizeof observed))return -3;
    return classify(original,payload);
}
int provision_run(const uint8_t original[PROVISION_BYTES],const uint8_t payload[PROVISION_PAYLOAD])
{
    if(!provision_ready())return -1;
    int match=provision_check(original,payload);
    if(match<=0)return match?match:-4;
    if(match==2)return 0; /* Already complete: no repeat erases. */
    for(unsigned step=0;step<20;++step) {
        unsigned size,off=operation(step,&size);
        /* Re-read the complete uncommitted reservation before publication. */
        if(step==19 && !verify_all(payload,0))return -5;
        if(step<3) {
            if(provision_erase(PROVISION_BASE+off))return -6;
        } else {
            for(unsigned i=0;i<size;++i)buffer[i]=value(step,i,payload);
            if(provision_program(PROVISION_BASE+off,buffer,size))return -7;
        }
        if(!verify_region(step,payload))return -8;
    }
    return verify_all(payload,1)?0:-9;
}
