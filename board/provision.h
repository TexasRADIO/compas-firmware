#ifndef COMPAS_PROVISION_H
#define COMPAS_PROVISION_H
#include <stdint.h>
/* Installer-side core, not linked into the instrument app. */
enum { PROVISION_BASE=0x90000, PROVISION_BYTES=12288, PROVISION_PAYLOAD=3776 };
int provision_ready(void);
int provision_read(uint32_t offset,void *dest,unsigned size);
int provision_erase(uint32_t offset);
int provision_program(uint32_t offset,const void *data,unsigned size);
/* Caller validates musical data and binds both inputs to its migration plan.
 * ready() must establish the expected completed app and the target device. */
/* Read-only preflight: 0/negative refuses, 1 resumable, 2 already complete. */
int provision_check(const uint8_t original[PROVISION_BYTES],const uint8_t payload[PROVISION_PAYLOAD]);
int provision_run(const uint8_t original[PROVISION_BYTES],const uint8_t payload[PROVISION_PAYLOAD]);
#endif
