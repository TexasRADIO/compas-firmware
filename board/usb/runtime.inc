/* SPDX-License-Identifier: GPL-3.0-only
 * Compás adapter for the BB1 r85 USB MIDI device and M-UPGRADE core.
 * In this profile the foreground owns USB; ALNK0 can interrupt USB work.
 */
#include "usb/identity.h"
#include "usb/src/usb.c"
#include "usb/src/ota.c"

_Static_assert(OTA_AREA == COMPAS_OTA_START && OTA_AREA + OTA_AREA_LEN == COMPAS_OTA_END,
               "USB staging and the NOR driver must agree");

static uint32_t usb_time_ticks, usb_time_ms, usb_last_poll;
static uint8_t usb_flash_ok;

static uint32_t ota_now_ms(void)
{
    uint32_t elapsed = (fm1_ticks() - usb_time_ticks) / 24000u;
    usb_time_ticks += elapsed * 24000u;
    usb_time_ms += elapsed;
    return usb_time_ms;
}

static void compas_usb_poll(void)
{
    uint32_t now = fm1_ticks();
    if (now - usb_last_poll < 12000u)
        return;
    usb_last_poll = now;
    usb_retry(ota_now_ms());
    usb_poll();
}

static void ota_idle(void)
{
    fm1_wdt_feed();
    compas_usb_poll();
}

static __attribute__((noinline)) int usb_supported_flash(void)
{
    const uint32_t jedec = FL_FAR(fl_jedec_ram)();
    return jedec == 0x856014u || jedec == 0xB36014u;
}

static int ota_erase(uint32_t off)
{
    uint32_t elapsed;
    if (!FL_IN(off, 0x1000u, COMPAS_OTA_START, COMPAS_OTA_END) || (off & 0xFFFu))
        return -8;
    fm1_wdt_feed();
    return fl_erase4k(off, &elapsed);
}

static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!FL_IN(off, n, COMPAS_OTA_START, COMPAS_OTA_END))
        return -8;
    return fl_write(off, p, n);
}

static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    if (!(off <= 0x4000u && n <= 0x4000u - off) &&
        !FL_IN(off, n, COMPAS_OTA_START, COMPAS_OTA_END))
        return -8;
    uint32_t irq=board_lock();
    int rc=FL_FAR(fl_read_ram)(off, p, n);
    board_unlock(irq);
    return rc;
}

static void ota_show(uint32_t step, int32_t code)
{
    static const char *const labels[] = {
        "CONNECT USB   ", "READ PACKAGE  ", "CHECK HEAD    ",
        "STAGE LOADER  ", "CONFIRM       ", "RESTART       "
    };
    band(0, 240, INK);
    text_line(54, 90, " UPDATE MODE", 12, GOLD, INK);
    text_line(43, 122, step < sizeof labels / sizeof labels[0] ? labels[step] :
              code == 1 ? "REHEARSAL OK  " : "UPDATE FAILED ", 14, CREAM, INK);
    lcd_sync();
}

static void ota_commit(const uint8_t *parm)
{
    recovery_healthy(&recovery_guard);
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

static void compas_enter_rom(void)
{
    recovery_healthy(&recovery_guard);
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_uboot();
}

static __attribute__((noinline, noreturn)) void compas_usb_safe(void)
{
    ota_show(0, 0);
    uint32_t started = ota_now_ms();
    unsigned released = 0;
    for (;;) {
        ota_idle();
        fm1_input_scan();
        ota_service();
        if (usb.uboot_req)
            compas_enter_rom();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (usb_flash_ok)
                ota_session();
            ota_show(0, 0);
        }
        if (ota_now_ms() - started >= 10000u)
            recovery_healthy(&recovery_guard);
        if (!(fm1_in.buttons & (1u << PANEL_PLAY_BUTTON)))
            released = 1;
        else if (released) {
            recovery_healthy(&recovery_guard);
            usb_detach();
            fm1_reboot();
        }
    }
}
