#include "provision.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static uint8_t nor[PROVISION_BYTES],original[PROVISION_BYTES],payload[PROVISION_PAYLOAD],target[PROVISION_BYTES];
static unsigned ready=1,budget=UINT_MAX,writes,ops,reads,fail_read,corrupt_read,fail_op;
int provision_ready(void){return ready;}
int provision_read(uint32_t off,void *out,unsigned n)
{
    assert(off>=PROVISION_BASE && n<=PROVISION_BYTES-(off-PROVISION_BASE));
    ++reads;if(reads==fail_read)return -1;
    memcpy(out,nor+(off-PROVISION_BASE),n);
    if(reads==corrupt_read)((uint8_t *)out)[0]^=1;
    return 0;
}
static int write_bytes(uint32_t off,const uint8_t *p,unsigned n)
{
    assert(off>=PROVISION_BASE && n<=PROVISION_BYTES-(off-PROVISION_BASE));
    ++ops;if(ops==fail_op)return -1;
    for(unsigned i=0;i<n;++i) {
        if(writes==budget)return -1;
        if(p)assert((nor[off-PROVISION_BASE+i]&p[i])==p[i]);
        nor[off-PROVISION_BASE+i]=p?p[i]:255;++writes;
    }
    return 0;
}
int provision_erase(uint32_t off){assert(!(off%4096));return write_bytes(off,0,4096);}
int provision_program(uint32_t off,const void *p,unsigned n)
{assert(n && n<=256-off%256);return write_bytes(off,p,n);}
static void reset(void){writes=ops=reads=fail_read=corrupt_read=fail_op=0;budget=UINT_MAX;ready=1;}
static void safe(void){if(!memcmp(nor+8192,"CMPASIM1",8))assert(!memcmp(nor,target,sizeof nor));}
int main(void)
{
    for(unsigned i=0;i<sizeof original;++i)original[i]=(uint8_t)(i*17+3);
    for(unsigned i=0;i<sizeof payload;++i)payload[i]=(uint8_t)(i*7+2);
    memcpy(nor,original,sizeof nor);reset();
    assert(provision_check(original,payload)==1 && ops==0 && reads==1);
    nor[4200]^=1;assert(provision_check(original,payload)==0 && ops==0);
    memcpy(nor,original,sizeof nor);reset();assert(!provision_run(original,payload));
    assert(ops==20 && writes==16084);unsigned full_reads=reads;memcpy(target,nor,sizeof target);
    reset();assert(provision_check(original,payload)==2 && ops==0);
    assert(!provision_run(original,payload) && ops==0);
    for(unsigned cut=0;cut<16084;++cut) {
        memcpy(nor,original,sizeof nor);reset();budget=cut;
        assert(provision_run(original,payload)<0);safe();
        assert(memcmp(nor+8192,"CMPASIM1",8));
        reset();assert(!provision_run(original,payload));assert(!memcmp(nor,target,sizeof nor));
    }
    for(unsigned i=1;i<=full_reads;++i)for(unsigned corrupt=0;corrupt<2;++corrupt) {
        memcpy(nor,original,sizeof nor);reset();
        if(corrupt)corrupt_read=i;else fail_read=i;
        assert(provision_run(original,payload)<0);safe();
        reset();assert(!provision_run(original,payload));assert(!memcmp(nor,target,sizeof nor));
    }
    for(unsigned i=1;i<=20;++i) {
        memcpy(nor,original,sizeof nor);reset();fail_op=i;
        assert(provision_run(original,payload)<0);safe();reset();assert(!provision_run(original,payload));
    }
    memcpy(nor,original,sizeof nor);reset();ready=0;assert(provision_run(original,payload)==-1 && ops==0);
    reset();nor[4200]^=1;assert(provision_run(original,payload)==-4 && ops==0);
    printf("{\"byteCuts\":16084,\"readFailuresAndCorruptions\":%u,\"writeFailures\":20,\"idempotent\":true}\n",full_reads*2);
}
