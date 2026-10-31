/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The drumpad RGB chain (hal/smk_ws2812.h): the pad -> pixel map and the encoded frame.
 *
 * The chain is serpentine (smk_board.h SMK_LED_OF, read off the stock firmware's own table), so the
 * buffer smk_rgb_show() walks must be in CHAIN order and the map must be applied exactly once. Applying
 * it twice is not identity - SMK_LED_OF is not an involution - and it sent 15 of the 16 pads to another
 * pad's pixel. Nothing in the scan or the UI could see that, so it is checked here on the encoder. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* The encoder is what is under test, not the SPI1 DMA: fm1_padled_send / fm1_padled_done touch the
 * panel's registers, which do not exist on the host (deref = SIGSEGV). FM1_HOST_TIME makes them no-ops
 * so smk_rgb_buf is the test's output, the same shim tests/input_test.c uses. */
static uint32_t host_now;
#define FM1_HOST_TIME 1
uint32_t fm1_ticks(void) { return host_now; }
#include "../firmware/hal/smk_ws2812.h"

static int fails;
static void check(const char *what, int ok)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        fails++;
    }
}

int main(void)
{
    uint32_t p, i, ok;

    /* every pad reaches its own pixel, and every pixel is reached exactly once */
    {
        uint32_t hit[SMK_RGB_N];
        memset(hit, 0, sizeof hit);
        for (p = 0; p < SMK_RGB_N; p++)
            if (SMK_LED_OF[p] < SMK_RGB_N)
                hit[SMK_LED_OF[p]]++;
        ok = 1;
        for (p = 0; p < SMK_RGB_N; p++)
            ok &= hit[p] == 1;
        check("SMK_LED_OF is a permutation of the 16 pixels", ok);
    }

    /* the map is not an involution: applying it twice really does move pads (the bug this guards) */
    {
        uint32_t same = 0;
        for (p = 0; p < SMK_RGB_N; p++)
            same += SMK_LED_OF[SMK_LED_OF[p]] == SMK_LED_OF[p];
        check("SMK_LED_OF is not idempotent (a double map scrambles the chain)", same < SMK_RGB_N);
    }

    /* one pad lit, alone: exactly its own pixel's 24 codes light up, all in that pixel's slot, GRB */
    for (p = 0; p < SMK_RGB_N; p++) {
        uint32_t slot = SMK_LED_OF[p], n1 = 0, other = 0;
        smk_rgb_fill(0, 0, 0);
        smk_rgb_set(p, 0xFF, 0x00, 0x00);          /* pure red: G byte 0x00, R byte 0xFF, B byte 0x00 */
        smk_rgb_show();
        for (i = 0; i < SMK_RGB_N * 24u; i++) {
            const uint8_t *o = smk_rgb_buf + SMK_RGB_LATCH + i;
            if (*o == 0xF0u) {
                if (i / 24u == slot)
                    n1++;
                else
                    other++;
            }
        }
        /* GRB MSB first: byte 0 = G (all 0), byte 1 = R (all 1), byte 2 = B (all 0) -> 8 lit codes */
        ok = other == 0 && n1 == 8u;
        ok &= smk_rgb_buf[SMK_RGB_LATCH + slot * 24u + 0] == 0xC0u;   /* G = 0 */
        ok &= smk_rgb_buf[SMK_RGB_LATCH + slot * 24u + 8] == 0xF0u;   /* R = 0xFF */
        ok &= smk_rgb_buf[SMK_RGB_LATCH + slot * 24u + 16] == 0xC0u;  /* B = 0 */
        if (!ok) {
            printf("FAIL: pad %u encodes into pixel %u (n1=%u other=%u)\n", p, slot, n1, other);
            fails++;
        }
    }
    check("each pad lights only its own pixel, in GRB order", fails == 0);

    /* green is the G byte, not R: the R/G transposition the double map's seam looked like */
    smk_rgb_fill(0, 0, 0);
    smk_rgb_set(0, 0x00, 0xFF, 0x00);
    smk_rgb_show();
    {
        const uint8_t *o = smk_rgb_buf + SMK_RGB_LATCH + SMK_LED_OF[0] * 24u;
        ok = o[0] == 0xF0u && o[8] == 0xC0u && o[16] == 0xC0u;   /* G lit, R and B dark */
        check("green lands in the G byte", ok);
    }

    /* the reset: 120 zero bytes is 1.15 ms at 6.67 MHz, past the chain's 280-300 us latch */
    ok = 1;
    for (i = 0; i < SMK_RGB_LATCH; i++)
        ok &= smk_rgb_buf[i] == 0;
    check("the frame starts with a reset longer than the chain's latch", ok && SMK_RGB_LATCH >= 40u);

    /* a fill covers the whole chain, whatever the order */
    smk_rgb_fill(1, 2, 3);
    smk_rgb_show();
    ok = 1;
    for (i = 0; i < SMK_RGB_N * 24u; i += 24u) {
        int j;
        for (j = 0; j < 24; j++)
            ok &= smk_rgb_buf[SMK_RGB_LATCH + i + j] == smk_rgb_buf[SMK_RGB_LATCH + j];
    }
    check("a fill writes every pixel alike", ok);

    /* nothing changed -> nothing sent (smk_rgb_show returns before touching the buffer) */
    smk_rgb_buf[0] = 0xAA;
    smk_rgb_show();                                  /* not dirty: must not re-encode */
    check("an unchanged frame is not re-sent", smk_rgb_buf[0] == 0xAA);

    if (fails) {
        printf("WS2812 TEST FAILED (%d)\n", fails);
        return 1;
    }
    printf("ws2812: all ok\n");
    return 0;
}
