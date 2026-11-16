/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/ota.c: a fake M-UPGRADE serves a real .fwsc over
 * the SysEx protocol (pack7, cmd 0x30 reads, 0xE0000000 -> "success") and a
 * simulated NOR plays the flash.
 *   cc -o build/test-ota tests/board-ota.c && build/test-ota FILE.fwsc
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)   /* (the sources' .pool sections: no Mach-O section names) */

#include "../board/usb/identity.h"
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000], original[0x100000];
static uint32_t writes;
static uint8_t *logical;
static size_t logical_len;
static uint8_t rx[1024];                 /* one pending host frame (7-bit bytes) */
static uint32_t rx_len, rx_full;
static uint32_t now_ms, requests, ident_replies, mute_after = 0xFFFFFFFFu, bad_erase;
static int committed;
static uint8_t committed_parm[112];
static int32_t last_code;
static uint8_t *corrupt_at;              /* flip a served byte (loader body) */

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}

static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static void host_reply(const uint8_t *msg, uint32_t n)       /* queue msg as a device-bound frame */
{
    rx_len = pack7(msg, n, rx);
    rx_full = 1;
}

/* ---- hooks ---- */
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t d, addr, len, i, s = 0;
    if (p[0] != 0xF0 || p[n - 1] != 0xF7) { printf("bad wire framing\n"); exit(1); }
    d = unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) { ident_replies++; return 0; }
    if (d != 15 || u[2] != 0x30) { printf("unexpected device msg cmd %02x len %u\n", u[2], d); exit(1); }
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    if (++requests > mute_after)
        return 0;                                         /* host gone */
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0;
    m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr == 0xE0000000u) {
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
        if (corrupt_at && corrupt_at >= logical + addr && corrupt_at < logical + addr + len)
            m[14 + (corrupt_at - (logical + addr))] ^= 0x01;
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    host_reply(m, 15 + len);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!rx_full) return 0;
    *p = rx; *n = rx_len;
    return 1;
}
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms += 1; }
static int ota_erase(uint32_t off)
{
    if (off < 0xE0000u || off >= 0xE5000u || (off & 0xFFFu)) { bad_erase++; return -8; }
    ++writes;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int ota_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off < 0xE0000u || off + n > 0xE5000u) { bad_erase++; return -8; }
    ++writes;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static int ota_fread(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { if (step == 9) last_code = code; }
static void ota_commit(const uint8_t *parm) { committed = 1; memcpy(committed_parm, parm, 112); }

#include "../board/usb/src/ota.c"

static uint32_t ota_body_off(void)                  /* ota.bin data + 0x20 in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, logical, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 100) return ota_rd32(e + 8) + 0x20u;
    }
    return 0;
}

static int check(const char *what, int ok)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static int area_erased(void)
{
    uint32_t i;
    for (i = 0xE0000; i < 0xE5000; i++) if (nor[i] != 0xFF) return 0;
    return 1;
}

static void reset_dev(void)
{
    memset(nor, 0xA5, sizeof nor);
    memset(nor + OTA_AREA, 0xFF, OTA_AREA_LEN);
    memcpy(nor, logical + 0x400, 0x4000);                 /* package's real flash head */
    memcpy(original, nor, sizeof nor);
    writes = now_ms = 0;
    rx_full = 0; requests = 0; ident_replies = 0; committed = 0; last_code = 0;
    mute_after = 0xFFFFFFFFu; corrupt_at = NULL; bad_erase = 0;
}

static int protected_unchanged(void)
{
    uint32_t end = OTA_AREA + OTA_AREA_LEN;
    return !bad_erase && !memcmp(nor, original, OTA_AREA) &&
           !memcmp(nor + end, original + end, sizeof nor - end);
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *raw;
    size_t n, i;
    int bad = 0, rc;
    if (argc < 2) { printf("usage: ota_test FILE.fwsc\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    raw = malloc(0x200000);
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    if (n < 20 * 48 + 0x4400) { printf("%s: too short for a package\n", argv[1]); return 2; }
    logical = malloc(n);
    for (i = 0; i < 20; i++) memcpy(logical + i * 47, raw + i * 48, 47);   /* drop the marker bytes */
    memcpy(logical + 20 * 47, raw + 20 * 48, n - 20 * 48);
    logical_len = n - 20;

    reset_dev();
    {
        uint8_t plain[7] = {0, 0x59, 0x11, 0, 0, 0, 0xFF};
        uint8_t full[8] = {0, 0x59, 0x11, 1, 0, 0, 1, 0xFE};
        host_reply(plain, sizeof plain);
        ota_service();
        bad += check("stock-compatible family identity",
                     ident_replies == 1 && !memcmp(ota_msg + 6, FELUCCA_HANDSHAKE_ID, sizeof FELUCCA_HANDSHAKE_ID));
        host_reply(full, sizeof full);
        ota_service();
        bad += check("full Compas identity",
                     ident_replies == 2 && !memcmp(ota_msg + 6, FELUCCA_ID, sizeof FELUCCA_ID));
    }

    reset_dev();
    rc = ota_session();
    uint32_t total = requests;
    bad += check("session commits after confirmation", rc == 0 && committed && total > 5);
    uint32_t staged = ota_rd32(nor + OTA_AREA + 8);
    bad += check("staged loader body matches the package",
                 committed && staged >= 32u && staged <= OTA_AREA_LEN &&
                 !memcmp(nor + OTA_AREA + 32, logical + ota_body_off(), staged - 32u));
    bad += check("checked flash record matches the reset record",
                 !memcmp(nor + OTA_RES, committed_parm, sizeof committed_parm) &&
                 ota_rd16(committed_parm) == ota_crc16(committed_parm + 2, 78, 0) &&
                 ota_rd32(committed_parm + 72) == OTA_AREA);
    bad += check("nothing written outside staging", protected_unchanged());

    unsigned failed = 0;
    for (uint32_t cut = 0; cut < total; ++cut) {
        reset_dev();
        mute_after = cut;
        rc = ota_session();
        if (rc >= 0 || committed || !area_erased() || !protected_unchanged())
            ++failed;
        mute_after = 0xFFFFFFFFu;
        if (ota_session() || !committed || !protected_unchanged())
            ++failed;
    }
    printf("  %u reply cut points and retries\n", total);
    bad += check("each interrupted transfer cleans staging and retries", failed == 0);

    reset_dev();
    nor[OTA_AREA + 7] = 0xA5;  /* refuse before touching any pre-existing bytes */
    mute_after = 0;
    rc = ota_session();
    bad += check("preflight refusal leaves NOR untouched",
                 rc < 0 && !committed && writes == 0 && nor[OTA_AREA + 7] == 0xA5);

    reset_dev();
    corrupt_at = logical + ota_body_off() + 400;
    rc = ota_session();
    bad += check("loader checksum failure cleans staging without commit",
                 rc < 0 && !committed && area_erased() && protected_unchanged());
    /* Boot cleanup (BB1 r85): leftover staging is erased at start-up. */
    reset_dev();
    memset(nor + OTA_AREA, 0x5A, 0x40);
    memset(nor + OTA_RES, 0x11, 8);
    rc = ota_boot_cleanup();
    bad += check("boot cleanup erases leftover staging", rc == 1 && area_erased() && protected_unchanged());
    writes = 0;
    rc = ota_boot_cleanup();
    bad += check("second boot: nothing staged, no writes", rc == 0 && writes == 0);
    reset_dev();
    nor[OTA_RES + 3] = 0x00;                                  /* a record alone still counts */
    rc = ota_boot_cleanup();
    bad += check("an update record alone is cleared too", rc == 1 && area_erased());
    free(logical);
    free(raw);
    return bad != 0;
}
