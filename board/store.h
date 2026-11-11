#ifndef COMPAS_STORE_H
#define COMPAS_STORE_H
#include "board.h"

/* Emulator fixture only. These offsets have no hardware allocation. */
enum { STORE_A=0x90000, STORE_B=0x91000, STORE_FIXTURE=0x92000, STORE_SECTOR=4096, STORE_PAGE=256 };
enum { STORE_SHOW_BYTES=BOARD_SET_BYTES+BOARD_RHYTHM_SLOTS_BYTES };
#define STORE_MARKER "CMPASIM1"

int store_read(uint32_t off, void *dest, unsigned size);
int store_erase(uint32_t off);
int store_program(uint32_t off, const void *src, unsigned size);
void store_boot(void);
unsigned store_commit(void);
#endif
