/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "recovery.h"

int main(void)
{
    RecoveryGuard guard = { 0 };
    assert(!recovery_begin(&guard, 1));
    assert(guard.pending == 1 && guard.failures == 0);
    assert(!recovery_begin(&guard, 1));
    assert(guard.pending == 1 && guard.failures == 1);
    assert(recovery_begin(&guard, 1));
    assert(guard.pending == 0 && guard.failures == 2);

    assert(!recovery_begin(&guard, 0));
    assert(!recovery_begin(&guard, 1));
    assert(!recovery_begin(&guard, 0));
    assert(guard.pending == 1 && guard.failures == 0);

    assert(!recovery_begin(&guard, 1));
    recovery_healthy(&guard);
    assert(!recovery_begin(&guard, 1));
    assert(guard.pending == 1 && guard.failures == 0);
    /* Corrupt or unrelated retained RAM cannot request ROM mode. */
    guard = (RecoveryGuard){.magic=0, .pending=1, .failures=1};
    assert(!recovery_begin(&guard, 1));
    assert(guard.magic == RECOVERY_GUARD_MAGIC && guard.failures == 0);
    guard.pending=2;
    guard.failures=1;
    assert(!recovery_begin(&guard, 1));
    assert(guard.pending == 1 && guard.failures == 0);
    guard.failures=RECOVERY_FAILED_BOOTS;
    assert(!recovery_begin(&guard, 1));
    assert(guard.pending == 1 && guard.failures == 0);
    guard = (RecoveryGuard){0};
    assert(!recovery_usb_begin(&guard, 0));
    assert(!recovery_usb_begin(&guard, 1));
    assert(recovery_usb_begin(&guard, 1));
    assert(recovery_usb_begin(&guard, 1));
    assert(guard.pending == 0 && guard.failures == 2);
    recovery_healthy(&guard);
    assert(!recovery_usb_begin(&guard, 1));
    return 0;
}
