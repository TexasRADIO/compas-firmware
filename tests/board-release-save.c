/* SPDX-License-Identifier: GPL-3.0-only
 * A set save must not block updates in a build that keeps no show.
 * With -DCOMPAS_NO_SHOW_STORE (the 1.0 release) a save stays in RAM and the
 * updater still finds the instrument idle; with the emulator store the save is
 * pending until the store commits it, as before.
 *   cc -std=c11 -DCOMPAS_BOARD -DCOMPAS_EMU_STORE -DCOMPAS_USB [-DCOMPAS_NO_SHOW_STORE] \
 *      -Iboard -Isrc -Ibuild -Ibuild/board -Ibuild/standin tests/board-release-save.c \
 *      src/engine.c board/board.c board/panel.c board/piano.c build/standin/browser-bank.c
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#undef memcpy
#undef memset
#define memcpy bench_memcpy
#define memset bench_memset
#include "../web/board-bench.c"
#undef memcpy
#undef memset

int main(void)
{
    if(!bench_boot(0)) { puts("boot failed"); return 1; }
    for(unsigned i=0;i<2000 && !board_ota_idle();++i) bench_audio(); /* let the boot cue land */
    int before=board_ota_idle();
    unsigned saved=board_submit((BoardCommand){BOARD_SAVE,1,0});
    for(unsigned i=0;i<64;++i) bench_audio();                         /* the mailbox runs the save */
    BoardView v=board_view(0);
    int after=board_ota_idle();
    if(v.result!=BOARD_SESSION && v.result!=BOARD_SAVED) { printf("save did not run (result %u)\n",v.result); return 1; }
#ifdef COMPAS_NO_SHOW_STORE
    const int expect=1;
#else
    const int expect=0;
#endif
    printf("idle before %d, save result %u, idle after save %d (expected %d)\n",before,saved,after,expect);
    return before==1 && after==expect ? 0 : 1;
}
