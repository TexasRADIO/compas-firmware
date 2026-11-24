#ifndef COMPAS_STORE_H
#define COMPAS_STORE_H
#include "board.h"

#ifdef COMPAS_UNIT_STORE
/* Compás 1.3 release: the show is kept on the unit. BB1 r85's loader owns the
 * app slot to flash 0x93000; the 1.3 app must end before 0x91000 (checked by
 * the build and board-emu.py), which leaves 0x91000-0x92FFF as Compás's own
 * two 4 KiB sectors for the A/B records. No fixture marker: an erased or
 * damaged pair simply boots the factory show. (The private 727 provisioning
 * tool's transfer window, 0x90000+12 KiB, is never used on a release unit.) */
enum { STORE_A=0x91000, STORE_B=0x92000, STORE_SECTOR=4096, STORE_PAGE=256, STORE_APP_END=0x91000 };
#else
/* Private emulator fixture only. These offsets have no hardware allocation. */
enum { STORE_A=0x90000, STORE_B=0x91000, STORE_FIXTURE=0x92000, STORE_SECTOR=4096, STORE_PAGE=256 };
#endif
enum { STORE_SHOW_BYTES=BOARD_SET_BYTES+BOARD_RHYTHM_SLOTS_BYTES };
#define STORE_MARKER "CMPASIM1"

int store_read(uint32_t off, void *dest, unsigned size);
int store_erase(uint32_t off);
int store_program(uint32_t off, const void *src, unsigned size);
void store_boot(void);
unsigned store_commit(void);
#endif
