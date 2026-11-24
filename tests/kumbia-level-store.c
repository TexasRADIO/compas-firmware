/* Real board/journal code, virtual NOR only. Check CPS5 migration and cuts
 * exactly at the new level byte and commit header. */
#include "store.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static uint8_t flash[STORE_SECTOR*2],baseline[sizeof flash];
static unsigned budget=UINT_MAX,written;
uint32_t board_ticks(void){return 0;}
uint32_t board_pending(void){return BOARD_HALF_IRQ;}
void board_ack_aux(uint32_t p){(void)p;}
unsigned board_free_half(void){return 0;}
void board_ack_half(void){}
void board_disable_output(void){}
uint32_t board_lock(void){return 0;}
void board_unlock(uint32_t p){(void)p;}
int store_read(uint32_t off,void *p,unsigned n){assert(off>=STORE_A&&off+n<=STORE_B+STORE_SECTOR);memcpy(p,flash+(off-STORE_A),n);return 0;}
int store_erase(uint32_t off){assert(off==STORE_A||off==STORE_B);for(unsigned i=0;i<STORE_SECTOR;++i){if(written==budget)return 1;flash[off-STORE_A+i]=255;++written;}return 0;}
int store_program(uint32_t off,const void *p,unsigned n){const uint8_t *in=p;assert(off>=STORE_A&&off+n<=STORE_B+STORE_SECTOR);for(unsigned i=0;i<n;++i){if(written==budget)return 1;flash[off-STORE_A+i]&=in[i];++written;}return 0;}
static void boot(void){board_init();store_boot();}
static void put(unsigned c,unsigned a,unsigned b){assert(board_submit((BoardCommand){c,a,b})==CP_OK);fm1_alnk0_irq();}
static void save(unsigned level){put(BOARD_HANAN_LEVEL,0,level);put(BOARD_HANAN_SAVE,0,0);}
static uint32_t checksum(const uint8_t *p,unsigned n,unsigned generation){uint32_t c=~0u;for(unsigned i=0;i<n+4;++i){c^=i<4?(generation>>(8*i))&255:p[i-4];for(unsigned j=0;j<8;++j)c=c>>1^((c&1)?0xedb88320u:0);}return ~c;}
static void header(unsigned offset,const char *magic,unsigned n,unsigned gen){uint32_t crc=checksum(flash+offset+STORE_PAGE,n,gen);memcpy(flash+offset,magic,4);for(unsigned i=0;i<4;++i){flash[offset+4+i]=(uint8_t)(gen>>(8*i));flash[offset+8+i]=(uint8_t)(crc>>(8*i));}}
int main(void){
  memset(flash,255,sizeof flash);boot();assert(board_kumbia_level()==100);
  save(75);assert(store_commit()==BOARD_SAVED);assert(!memcmp(flash,"CPS6",4));
  boot();assert(board_kumbia_level()==75);memcpy(baseline,flash,sizeof flash);
  save(25);assert(store_commit()==BOARD_SAVED);boot();assert(board_kumbia_level()==25);
  /* A valid outer CRC cannot bless an invalid bus level. Fall back atomically. */
  flash[STORE_SECTOR+STORE_PAGE+STORE_SHOW_BYTES+KUMBIA_SETTINGS]=101;
  header(STORE_SECTOR,"CPS6",STORE_SHOW_BYTES+KUMBIA_SETTINGS+1,2);
  boot();assert(board_kumbia_level()==75);
  const unsigned cuts[]={0,1,4095,4096,4097,4096+STORE_SHOW_BYTES+KUMBIA_SETTINGS,
      4096+STORE_SHOW_BYTES+KUMBIA_SETTINGS+1,4096+STORE_SHOW_BYTES+KUMBIA_SETTINGS+12};
  for(unsigned i=0;i<sizeof cuts/sizeof *cuts;++i){
    memcpy(flash,baseline,sizeof flash);budget=UINT_MAX;boot();save(0);
    written=0;budget=cuts[i];assert(store_commit()==BOARD_SESSION);
    assert(!memcmp(flash,baseline,STORE_SECTOR));budget=UINT_MAX;boot();assert(board_kumbia_level()==75);
  }
  /* CPS5 retains the original four controls, adding the missing level at 100. */
  memcpy(flash,baseline,sizeof flash);header(0,"CPS5",STORE_SHOW_BYTES+KUMBIA_SETTINGS,1);
  boot();assert(board_kumbia_level()==100);
  const uint8_t controls[KUMBIA_SETTINGS]={17,63,8,24};assert(board_kumbia_restore(controls));
  save(0);assert(store_commit()==BOARD_SAVED);boot();assert(board_kumbia_level()==0);
  uint8_t actual[KUMBIA_SETTINGS];board_kumbia_export(actual);assert(!memcmp(actual,controls,sizeof actual));
  puts("Kumbia CPS6 level: save/reboot zero and nonzero, CPS5 default, invalid-level fallback and eight byte-cut checkpoints pass.");
}
