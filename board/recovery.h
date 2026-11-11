/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef COMPAS_RECOVERY_H
#define COMPAS_RECOVERY_H

#include <stdint.h>

enum { RECOVERY_GUARD_MAGIC = 0x43504744u, RECOVERY_FAILED_BOOTS = 2 };
typedef struct {
    uint32_t magic;
    unsigned pending;
    unsigned failures;
} RecoveryGuard;

/* A healthy start, cold start or intentional reset breaks the failure streak. */
static inline unsigned recovery_begin(volatile RecoveryGuard *guard, unsigned watchdog_reset)
{
    unsigned failures=0;
    if(watchdog_reset && guard->magic==RECOVERY_GUARD_MAGIC && guard->pending==1 &&
       guard->failures<RECOVERY_FAILED_BOOTS)
        failures=guard->failures+1;
    guard->magic=RECOVERY_GUARD_MAGIC;
    guard->failures=failures;
    guard->pending=failures<RECOVERY_FAILED_BOOTS;
    return failures>=RECOVERY_FAILED_BOOTS;
}

/* A watchdog reset in safe mode must not retry the failed application. */
static inline unsigned recovery_usb_begin(volatile RecoveryGuard *guard, unsigned watchdog_reset)
{
    if(watchdog_reset && guard->magic==RECOVERY_GUARD_MAGIC &&
       guard->pending==0 && guard->failures==RECOVERY_FAILED_BOOTS)
        return 1;
    return recovery_begin(guard,watchdog_reset);
}

static inline void recovery_healthy(volatile RecoveryGuard *guard)
{
    guard->pending=0;
    guard->failures=0;
}

#endif
