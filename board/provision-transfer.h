#ifndef COMPAS_PROVISION_TRANSFER_H
#define COMPAS_PROVISION_TRANSFER_H
#include "provision.h"
enum { PROVISION_WIRE_OFFSET=0x100000, PROVISION_WIRE_HEADER=64,
       PROVISION_WIRE_BYTES=64+PROVISION_BYTES+PROVISION_PAYLOAD };
typedef struct {
    uint8_t original[PROVISION_BYTES],payload[PROVISION_PAYLOAD];
} ProvisionInputs;
/* Existing bounded OTA read; no write operation belongs in this callback. */
int provision_fetch(uint32_t offset,void *dest,unsigned size);
uint32_t provision_crc_update(uint32_t crc,const void *bytes,unsigned size);
/* Caller supplies head CRC from flash and app CRC from the checked package.
 * CRCs detect accidental corruption; they are not an authentication scheme.
 * Host must validate musical content before constructing this record. */
int provision_receive(uint32_t head_crc,uint32_t app_crc,ProvisionInputs *out);
#endif
