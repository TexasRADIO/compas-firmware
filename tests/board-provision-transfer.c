#include "provision-transfer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t record[PROVISION_WIRE_BYTES],saved[PROVISION_WIRE_BYTES];
static unsigned calls,fail_at;
static struct { uint32_t before;ProvisionInputs inputs;uint32_t after; } guarded;
static void wr32(unsigned off,uint32_t n)
{ for(unsigned i=0;i<4;++i)record[off+i]=(uint8_t)(n>>(8*i)); }
int provision_fetch(uint32_t off,void *out,unsigned n)
{
    assert(off>=PROVISION_WIRE_OFFSET && n && n<=512);
    unsigned p=off-PROVISION_WIRE_OFFSET;
    assert(p<=sizeof record && n<=sizeof record-p);
    if(++calls==fail_at)return -1;
    memcpy(out,record+p,n);return 0;
}
static int run(void)
{
    calls=0;guarded.before=0x12345678;guarded.after=0x87654321;
    int r=provision_receive(0x11223344,0x55667788,&guarded.inputs);
    assert(guarded.before==0x12345678 && guarded.after==0x87654321);
    return r;
}
int main(int argc,char **argv)
{
    (void)argc;(void)argv;
    memcpy(record,"CPPRV001",8);
    wr32(8,1);wr32(12,sizeof record);wr32(16,0x90000);
    wr32(20,PROVISION_BYTES);wr32(24,PROVISION_PAYLOAD);
    wr32(28,0x11223344);wr32(32,0x55667788);
    for(unsigned i=64;i<sizeof record;++i)record[i]=(uint8_t)(i*17+9);
    wr32(36,~provision_crc_update(~0u,record+64,sizeof record-64));
    wr32(40,~provision_crc_update(~0u,record+64,PROVISION_BYTES));
    wr32(44,~provision_crc_update(~0u,record+64+PROVISION_BYTES,PROVISION_PAYLOAD));
    wr32(48,0x4000);wr32(52,0x90000);
    wr32(56,~provision_crc_update(~0u,record,56));
    memcpy(saved,record,sizeof record);
    assert(!run() && calls==33);
    assert(!memcmp(guarded.inputs.original,record+64,PROVISION_BYTES));
    assert(!memcmp(guarded.inputs.payload,record+64+PROVISION_BYTES,PROVISION_PAYLOAD));
    for(fail_at=1;fail_at<=33;++fail_at)assert(run()<0 && calls==fail_at);
    fail_at=0;
    /* Every header byte, and every body-page boundary, rejects corruption. */
    for(unsigned i=0;i<64;++i) {
        memcpy(record,saved,sizeof record);record[i]^=1;assert(run()<0 && calls==1);
    }
    for(unsigned i=64;i<sizeof record;i+=256) {
        memcpy(record,saved,sizeof record);record[i]^=1;assert(run()<0 && calls==33);
    }
    /* Well-checksummed but unsupported dimensions/identities must also fail. */
    static const unsigned fields[]={8,12,16,20,24,28,32,48,52,60};
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];++i) {
        memcpy(record,saved,sizeof record);record[fields[i]]^=1;
        wr32(56,~provision_crc_update(~0u,record,56));assert(run()<0 && calls==1);
    }
    for(unsigned i=36;i<=44;i+=4) {
        memcpy(record,saved,sizeof record);record[i]^=1;
        wr32(56,~provision_crc_update(~0u,record,56));assert(run()<0 && calls==33);
    }
    memcpy(record,saved,sizeof record);assert(!run());
    puts("provision transfer: 33 read failures, 64 header bytes, 63 body boundaries and semantic header checks passed");
}
