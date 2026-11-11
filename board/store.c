#include "store.h"

enum { SONG_MAGIC=0x31535043, SHOW_MAGIC=0x32535043, NO_COPY=2 };
#ifdef COMPAS_HANAN
enum { KIT_MAGIC=0x33535043, RECORD_MAGIC=KIT_MAGIC, RECORD_BYTES=STORE_SHOW_BYTES+KUMBIA_SETTINGS };
#else
enum { RECORD_MAGIC=SHOW_MAGIC, RECORD_BYTES=STORE_SHOW_BYTES };
#endif
typedef struct { uint32_t magic, generation, crc; } Record;
static uint8_t payload[RECORD_BYTES];
static unsigned current;
static uint32_t generation;

_Static_assert(RECORD_BYTES<=STORE_SECTOR-STORE_PAGE,"Compás show fits one NOR sector");

static uint32_t checksum(uint32_t sequence,unsigned size)
{
    uint32_t crc=~0u;
    for(unsigned i=0;i<4u+size;++i) {
        crc^=i<4 ? (sequence>>(8u*i))&255u : payload[i-4];
        for(unsigned bit=0;bit<8;++bit)
            crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}

static unsigned address(unsigned copy) { return copy ? STORE_B : STORE_A; }
static unsigned record_size(const Record *record)
{
#ifdef COMPAS_HANAN
    if(record->magic==KIT_MAGIC) return RECORD_BYTES;
#endif
    return record->magic==SHOW_MAGIC ? STORE_SHOW_BYTES : record->magic==SONG_MAGIC ? BOARD_SET_BYTES : 0;
}

void store_boot(void)
{
    /* Boot-only: no audio IRQ or user commands exist yet. A failed candidate
     * may have loaded its songs before its attachments fail validation, so
     * reset the owner before each attempt and after both candidates fail. */
    Record records[2]={{0,0,0},{0,0,0}};
    unsigned sizes[2],first=0;
    current=NO_COPY;generation=0;
    for(unsigned i=0;i<2;++i)
        sizes[i]=store_read(address(i),&records[i],sizeof records[i]) ? 0 : record_size(&records[i]);
    if(sizes[1]) {
        uint32_t advance=records[1].generation-records[0].generation;
        if(!sizes[0] || (advance && advance<0x80000000u)) first=1;
    }
    for(unsigned attempt=0;attempt<2;++attempt) {
        unsigned copy=first^attempt,size=sizes[copy];
        const Record *record=&records[copy];
        if(!size || store_read(address(copy)+STORE_PAGE,payload,size) ||
           checksum(record->generation,size)!=record->crc) continue;
        board_init();
        if(!board_store_load(payload)) continue;
        if(size>=STORE_SHOW_BYTES && board_rhythm_slots_load(payload+BOARD_SET_BYTES,BOARD_RHYTHM_SLOTS_BYTES)) continue;
#ifdef COMPAS_HANAN
        if(record->magic==KIT_MAGIC && !board_kumbia_restore(payload+STORE_SHOW_BYTES)) continue;
#endif
        current=copy;generation=record->generation;
        return;
    }
    board_init();
}

unsigned store_commit(void)
{
    if(!board_store_pull(payload)) return 0;
    /* Materialize untouched factory slots too: a later factory bank must not
     * silently change the running order in an already saved show. */
    if(!board_store_snapshot(payload) ||
       board_rhythm_slots_export(payload+BOARD_SET_BYTES,BOARD_RHYTHM_SLOTS_BYTES)) return BOARD_SESSION;
#ifdef COMPAS_HANAN
    board_kumbia_export(payload+STORE_SHOW_BYTES);
#endif
    unsigned copy=current==0 ? 1u : 0u;
    unsigned base=address(copy);
    Record record={RECORD_MAGIC,generation+1u,checksum(generation+1u,sizeof payload)};
    if(store_erase(base)) return BOARD_SESSION;
    for(unsigned off=0;off<sizeof payload;off+=STORE_PAGE) {
        unsigned size=sizeof payload-off;
        if(size>STORE_PAGE) size=STORE_PAGE;
        if(store_program(base+STORE_PAGE+off,payload+off,size)) return BOARD_SESSION;
    }
    if(store_read(base+STORE_PAGE,payload,sizeof payload) || checksum(record.generation,sizeof payload)!=record.crc ||
       store_program(base,&record,sizeof record)) return BOARD_SESSION;
    Record check;
    if(store_read(base,&check,sizeof check) || check.magic!=record.magic ||
       check.generation!=record.generation || check.crc!=record.crc) return BOARD_SESSION;
    current=copy;generation=record.generation;
    return BOARD_SAVED;
}
