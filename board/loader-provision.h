/* Optional synthetic first-show transaction. Never enabled in a live loader. */
#include "provision.h"
#ifdef COMPAS_FIRST_SHOW_WIRE
#include "provision-transfer.h"
static ProvisionInputs provision_inputs;
static uint32_t provision_incoming_crc,provision_head_crc,provision_app_crc;
#define fixture_original provision_inputs.original
#define fixture_payload provision_inputs.payload
#define FIXTURE_HEAD_CRC provision_head_crc
#define FIXTURE_APP_CRC provision_app_crc
int provision_fetch(uint32_t off,void *out,unsigned n)
{ return ota_read(off,out,n); }
static void ldr_provision_app_bytes(uint32_t off,const void *data,unsigned n)
{
    if(off==0)provision_incoming_crc=~0u;
    if(off>=0x4000 && off<0x90000)
        provision_incoming_crc=provision_crc_update(provision_incoming_crc,data,n);
}
#else
#include "fixture.h"
#endif
static uint8_t provision_page[256],provision_armed;
static int provision_flash_crc(uint32_t start,uint32_t end,uint32_t *result)
{
    uint32_t crc=~0u;
    for(uint32_t off=start;off<end;off+=sizeof provision_page) {
        if(fl_read_ram(off,provision_page,sizeof provision_page))return 0;
        for(unsigned i=0;i<sizeof provision_page;++i) {
            crc^=provision_page[i];
            for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
        }
        ldr_poll();
    }
    *result=~crc;return 1;
}
static int provision_crc(uint32_t start,uint32_t end,uint32_t expected)
{ uint32_t crc;return provision_flash_crc(start,end,&crc) && crc==expected; }
int provision_ready(void)
{
    return provision_armed && fl_jedec_ram()==0x856014u &&
        provision_crc(0,0x4000,FIXTURE_HEAD_CRC) &&
        provision_crc(0x4000,0x90000,FIXTURE_APP_CRC);
}
int provision_read(uint32_t off,void *out,unsigned n)
{
    if(!compas_first_show_range(off,n))return -1;
    int result=fl_read_ram(off,out,n);ldr_poll();return result;
}
int provision_erase(uint32_t off)
{
    if(!provision_armed || (off&4095u) || !compas_first_show_range(off,4096))return -1;
    uint32_t took;int result=fl_erase4k_ram(off,&took);ldr_poll();return result;
}
int provision_program(uint32_t off,const void *p,unsigned n)
{
    if(!provision_armed || !compas_first_show_range(off,n) || n>256u-(off&255u))return -1;
    uint32_t took;int result=fl_prog_ram(off,p,n,&took);ldr_poll();return result;
}
static int ldr_prepare_provision(void)
{
#ifdef COMPAS_FIRST_SHOW_WIRE
    if(!provision_flash_crc(0,0x4000,&provision_head_crc))return -1;
    provision_app_crc=~provision_incoming_crc;
    if(provision_receive(provision_head_crc,provision_app_crc,&provision_inputs))return -1;
#endif
    if(fl_jedec_ram()!=0x856014u || !provision_crc(0,0x4000,FIXTURE_HEAD_CRC))return -1;
    return provision_check(fixture_original,fixture_payload)>0?0:-1;
}
static int ldr_commit_provision(void)
{
    provision_armed=1;
    int result=provision_run(fixture_original,fixture_payload);
    provision_armed=0;
    return result;
}
