#include "provision-transfer.h"
static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
uint32_t provision_crc_update(uint32_t crc,const void *bytes,unsigned size)
{
    const uint8_t *p=bytes;
    while(size--) {
        crc^=*p++;
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    }
    return crc;
}
static int receive(uint32_t offset,uint8_t *dest,unsigned size)
{
    while(size) {
        unsigned n=size>512?512:size;
        if(provision_fetch(offset,dest,n))return -1;
        offset+=n;dest+=n;size-=n;
    }
    return 0;
}
int provision_receive(uint32_t head_crc,uint32_t app_crc,ProvisionInputs *out)
{
    uint8_t h[PROVISION_WIRE_HEADER];
    static const uint8_t magic[8]={'C','P','P','R','V','0','0','1'};
    if(provision_fetch(PROVISION_WIRE_OFFSET,h,sizeof h))return -1;
    for(unsigned i=0;i<8;++i)if(h[i]!=magic[i])return -2;
    if(rd32(h+8)!=1 || rd32(h+12)!=PROVISION_WIRE_BYTES ||
       rd32(h+16)!=PROVISION_BASE || rd32(h+20)!=PROVISION_BYTES ||
       rd32(h+24)!=PROVISION_PAYLOAD || rd32(h+28)!=head_crc ||
       rd32(h+32)!=app_crc || rd32(h+48)!=0x4000 ||
       rd32(h+52)!=0x90000 || rd32(h+60) ||
       rd32(h+56)!=~provision_crc_update(~0u,h,56))return -2;
    if(receive(PROVISION_WIRE_OFFSET+sizeof h,out->original,PROVISION_BYTES) ||
       receive(PROVISION_WIRE_OFFSET+sizeof h+PROVISION_BYTES,out->payload,PROVISION_PAYLOAD))return -3;
    uint32_t a=provision_crc_update(~0u,out->original,PROVISION_BYTES);
    uint32_t b=provision_crc_update(~0u,out->payload,PROVISION_PAYLOAD);
    if(rd32(h+40)!=~a || rd32(h+44)!=~b ||
       rd32(h+36)!=~provision_crc_update(a,out->payload,PROVISION_PAYLOAD))return -4;
    return 0;
}
