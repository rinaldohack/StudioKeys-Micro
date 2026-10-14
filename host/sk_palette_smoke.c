/* SPDX-License-Identifier: GPL-3.0-only
 * Smoke: every palette index selects and yields non-garbage RGB565 slots. */
#include "sk_host.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint32_t n, i;
    uint16_t first[5];
    int ok_intra = 0;
    int ok_across = 0;

    sk_host_init();
    sk_host_boot();

    n = sk_host_npalettes();
    if (n < 2u) {
        fprintf(stderr, "npalettes too small: %u\n", n);
        return 1;
    }

    memset(first, 0, sizeof first);

    for (i = 0; i < n; i++) {
        uint16_t rgb[5];

        sk_host_set_palette(i);
        if (sk_host_palette() != i) {
            fprintf(stderr, "palette want %u got %u\n", i, sk_host_palette());
            return 1;
        }

        memset(rgb, 0xFF, sizeof rgb);
        sk_host_palette_rgb565(rgb);

        /* Filled: API must overwrite the 0xFFFF sentinel for all five slots. */
        if (rgb[0] == 0xFFFFu && rgb[1] == 0xFFFFu && rgb[2] == 0xFFFFu &&
            rgb[3] == 0xFFFFu && rgb[4] == 0xFFFFu) {
            fprintf(stderr, "palette %u rgb565 left unfilled\n", i);
            return 1;
        }

        /* BG / THEME / ACCENT differ within this palette. */
        if (rgb[0] != rgb[3] || rgb[0] != rgb[4] || rgb[3] != rgb[4])
            ok_intra = 1;

        if (i == 0u)
            memcpy(first, rgb, sizeof first);
        else if (memcmp(rgb, first, sizeof first) != 0)
            ok_across = 1;
    }

    if (!ok_intra && !ok_across) {
        fprintf(stderr, "palette colors not distinctive (BG/THEME/ACCENT)\n");
        return 1;
    }

    puts("palette ok");
    return 0;
}
