/* SPDX-License-Identifier: GPL-3.0-only
 * IRQ save/restore sequence based on Felucca by Leo Kuroshita and Hügelton Instruments.
 */
#define isr_alnk0 isr_alnk0_unmeasured
#include "fm1_isr.S"
#undef isr_alnk0

    .section .text.isr_alnk0,"ax",@progbits
    .globl isr_alnk0
isr_alnk0:
    [--sp] = {psr, rets, reti}
    [--sp] = {r3-r0}
    r1 = 0x10804                   /* TIMER4 CNT at 24 MHz */
    r0 = [r1]
    sp += -4
    [sp+0] = r0
    call fm1_alnk0_irq
    r0 = [sp+0]
    sp += 4
    r1 = 0x10804
    r1 = [r1]
    r0 = r1 - r0
    r1 = board_stats + 36          /* BoardStats.irq_max_ticks */
    r2 = [r1]
    if (r0 <= r2) goto 1f
    [r1] = r0
1:  {r3-r0} = [sp++]
    {psr, rets, reti} = [sp++]
    csync
    rti
