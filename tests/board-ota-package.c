/* Exercise the retained OTA implementation with the actual compressed loader.
 * Synthetic host/NOR only. The upstream fixture owns protocol simulation. */
#define main retained_ota_main
#include "ota_test.c"
#undef main

int main(int argc, char **argv)
{
    uint32_t off, body, at;
    int rc;
    FILE *f;
    if (argc != 3) return 2;
    if (retained_ota_main(argc, argv)) return 1;
    off = ota_body_off();
    body = ota_rd32(logical + off - 24);
    if (!body || body > 0x4E00 || off + body > logical_len) return 1;
    reset_dev();
    /* Check preservation beyond the updater's own staging reservation. */
    for (at = 0x4000; at < sizeof nor; at++)
        if (at < 0xE0000 || at >= 0xE5000) nor[at] = (uint8_t)(at * 17 + 3);
    rc = ota_session();
    if (check("whole compressed loader staged byte-for-byte",
              rc == 0 && committed && !memcmp(nor + 0xE0020, logical + off, body))) return 1;
    for (at = 0x4000; at < sizeof nor; at++)
        if ((at < 0xE0000 || at >= 0xE5000) && nor[at] != (uint8_t)(at * 17 + 3)) return 1;
    if (check("staging preserves application/show/other NOR", bad_erase == 0)) return 1;
    f = fopen(argv[2], "wb");
    if (!f) return 2;
    if (fwrite(nor, 1, sizeof nor, f) != sizeof nor) return 2;
    if (fclose(f)) return 2;
    for (at = 64; at < body; at += 512) {
        reset_dev();
        corrupt_at = logical + off + at;
        rc = ota_session();
        if (rc >= 0 || committed || !area_erased() || bad_erase) return 1;
    }
    puts("corruption across the compressed body refuses commit and clears staging: ok");
    free(logical);
    return 0;
}
