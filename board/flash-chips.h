/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef COMPAS_FLASH_CHIPS_H
#define COMPAS_FLASH_CHIPS_H
#include <stdint.h>
/* The FM-1 ships with either flash chip; both take the same SPI NOR
 * commands. Puya P25Q80 (0x856014) and, on units sold since autumn 2026,
 * UC25HQ80 (0xB36014). Keep every JEDEC check on this one test.
 * (board/usb/runtime.inc's usb_supported_flash() already accepts both.) */
#define COMPAS_JEDEC_P25Q80   0x856014u
#define COMPAS_JEDEC_UC25HQ80 0xB36014u
#define COMPAS_JEDEC_OK(id) ((uint32_t)(id) == COMPAS_JEDEC_P25Q80 || (uint32_t)(id) == COMPAS_JEDEC_UC25HQ80)
#endif
