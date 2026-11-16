/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "loader-flash-policy.h"

int main(void)
{
    for (uint32_t sector = 0; sector < 0x100000; sector += 0x1000) {
        int app = sector >= 0x4000 && sector < COMPAS_LOADER_APP_END;
        int staging = sector == 0xE4000 || sector == 0xE8000;
        assert(compas_loader_erase_range(sector, 4096) == (app || staging));
        assert(!compas_loader_erase_range(sector + 1, 4096));
        for (uint32_t page = sector; page < sector + 4096; page += 256) {
            assert(compas_loader_program_range(page, 256) == app);
            assert(compas_loader_program_range(page + 255, 1) == app);
            assert(!compas_loader_program_range(page + 1, 256));
        }
    }
    assert(!compas_loader_program_range(0x4000, 0));
    assert(!compas_loader_program_range(0x4000, UINT32_MAX));
    assert(!compas_loader_program_range(UINT32_MAX, 1));
    assert(!compas_loader_erase_range(0x4000, UINT32_MAX));
    assert(!compas_loader_erase_range(0xFFFFF000, 4096));
#ifdef COMPAS_FIRST_SHOW_FIXTURE
    for(uint32_t off=0;off<0x100000;off+=256)
        assert(compas_first_show_range(off,256)==(off>=0x90000 && off<0x93000));
    assert(!compas_first_show_range(0x90000,0));
    assert(!compas_first_show_range(0x90000,UINT32_MAX));
    assert(!compas_first_show_range(UINT32_MAX,1));
    assert(!compas_first_show_range(0x92fff,2));
#endif
    puts("256 erase sectors and 4096 program pages checked");
}
