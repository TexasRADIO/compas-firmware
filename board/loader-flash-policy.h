/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef COMPAS_LOADER_FLASH_POLICY_H
#define COMPAS_LOADER_FLASH_POLICY_H

#include <stdint.h>

#ifdef COMPAS_PRESERVE_SHOW
#define COMPAS_LOADER_APP_END 0x90000u
#else
#define COMPAS_LOADER_APP_END 0x93000u
#endif

#ifdef COMPAS_FIRST_SHOW_FIXTURE
#ifndef COMPAS_PRESERVE_SHOW
#error First-show fixture requires the compact application profile
#endif
static inline int compas_first_show_range(uint32_t off,uint32_t n)
{ return off>=0x90000u && off<0x93000u && n && n<=0x93000u-off; }
#endif

static inline int compas_loader_app_range(uint32_t offset, uint32_t length)
{
    return offset >= 0x4000u && offset < COMPAS_LOADER_APP_END && length && length <= COMPAS_LOADER_APP_END - offset;
}

static inline int compas_loader_erase_range(uint32_t offset, uint32_t length)
{
    return length == 0x1000u && !(offset & 0xFFFu) &&
           (compas_loader_app_range(offset, length) || offset == 0xE4000u || offset == 0xE8000u);
}

static inline int compas_loader_program_range(uint32_t offset, uint32_t length)
{
    return compas_loader_app_range(offset, length) && length <= 256u - (offset & 0xFFu);
}

#endif
