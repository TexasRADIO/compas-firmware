/* Offline RAM fixture only. No USB identity or installation entry point. */
#include "fm1_time.h"
#include "fm1_sys.h"
#define FL_RANGE_OK(off,n) FL_IN(off,n,0x90000u,0x93000u)
#include "fm1_flash.h"
#include "provision.h"
#include "fixture.h"

volatile int32_t provision_result;
volatile uint32_t provision_done,provision_reads,provision_erases,provision_programs;
static uint32_t fail_read,corrupt_read;
static uint8_t page[256];
extern uint32_t _bss_start[],_bss_end[];

int provision_ready(void)
{
    if(fl_jedec_ram()!=0x856014u)return 0;
    uint32_t crc=~0u;
    for(uint32_t off=0x4000;off<0x90000;off+=sizeof page) {
        if(fl_read_ram(off,page,sizeof page))return 0;
        for(unsigned i=0;i<sizeof page;++i) {
            crc^=page[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
        }
        fm1_wdt_feed();
    }
    return ~crc==FIXTURE_APP_CRC;
}
int provision_read(uint32_t off,void *out,unsigned n)
{
    int result=fl_read_ram(off,out,n);++provision_reads;
    if(provision_reads==fail_read)return -1;
    if(provision_reads==corrupt_read && n)((uint8_t *)out)[0]^=1;
    return result;
}
int provision_erase(uint32_t off)
{uint32_t took;++provision_erases;return fl_erase4k_ram(off,&took);}
int provision_program(uint32_t off,const void *p,unsigned n)
{uint32_t took;++provision_programs;return fl_prog_ram(off,p,n,&took);}
void ldr_cstart(void)
{
    for(uint32_t *p=_bss_start;p<_bss_end;++p)*p=0;
    fm1_wdt_stop();fm1_time_init();
    uint32_t fault[3];
    if(!fl_read_ram(0xF1000,(uint8_t *)fault,sizeof fault) && fault[0]==0x46525043u) {
        fail_read=fault[1];corrupt_read=fault[2];
    }
    provision_result=-99;
    provision_result=provision_run(fixture_original,fixture_payload);
    provision_done=0x43505256u;
    for(;;)fm1_wdt_feed();
}
