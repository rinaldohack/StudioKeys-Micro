/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* P6f: the boot animation, one second, at power-on and when the companion's editor opens: the
 * LangitCinta! mark alone on the LCD and the 16 pads as a loading bar - red, filling row 1 then row 2
 * over the first 0.8 s, all green for the last 0.2 s, then the synth. A hardware self-test: every
 * pixel of the chain and the panel is lit once before the UI takes them over.
 *
 * One source for both: main.c's fm1_main runs boot_anim() on the device, and host/studiokeys_host.c
 * steps boot_pads() from its own clock (web_ms) for the companion, so the VST shows the same splash
 * on open. Included before main.c (studiokeys.c and studiokeys_host.c), as everything else here. */
#include "lc_boot_logo.h"        /* the mark at 208 x 185, 4-bit coverage, Huffman-coded (tools/gen_boot_logo.py) */

#define BOOT_MS      1000u        /* the whole splash */
#define BOOT_FILL_MS 800u         /* the bar; the last BOOT_MS - BOOT_FILL_MS is green */

/* ui_pads.c: the bar's own colours, and the chain's paint (device) */
static void pad_boot_set(uint32_t pad, uint32_t rgb);
static void pad_boot_clear(void);
static void pad_boot_paint(void);

/* the splash screen: the mark, centred, on the panel background (no wordmark: the boot screen is the
 * logo). Drawn native - no scaling - in the two coded strips, each the height the blit canvas holds. */
static void boot_screen(void)
{
    const uint16_t *rv = ramp(0xFFFF, T_BG);
    const int32_t x = (240 - (int32_t)LC_BOOT_LOGO_W) / 2;
    const uint32_t y = (240u - LC_BOOT_LOGO_H) / 2u;
    lcd_fill(0, 0, 240, 240, T_BG);
    cv_begin(LC_BOOT_LOGO_W, LC_BOOT_LOGO_ROWS0, T_BG);
    cv_alpha_hc(0, 0, LC_BOOT_LOGO_W, LC_BOOT_LOGO_ROWS0, LC_BOOT_LOGO_S0, LC_BOOT_LOGO_HC, rv);
    cv_blit((uint32_t)x, y);
    cv_begin(LC_BOOT_LOGO_W, LC_BOOT_LOGO_H - LC_BOOT_LOGO_ROWS0, T_BG);
    cv_alpha_hc(0, 0, LC_BOOT_LOGO_W, LC_BOOT_LOGO_H - LC_BOOT_LOGO_ROWS0, LC_BOOT_LOGO_S1,
                LC_BOOT_LOGO_HC, rv);
    cv_blit((uint32_t)x, y + LC_BOOT_LOGO_ROWS0);
}

/* the pads t ms in: one more pad every 50 ms (row 1 = pads 0..7, row 2 = 8..15, the newest one
 * brighter as the bar's head), then every pad green for the last 200 ms */
static void boot_pads(uint32_t t)
{
    uint32_t i, head;
    if (t >= BOOT_FILL_MS) {
        for (i = 0; i < 16u; i++)
            pad_boot_set(i, 0x00C800u);                /* green: the whole chain proven */
        return;
    }
    head = t * 16u / BOOT_FILL_MS;                      /* 0 .. 15 */
    for (i = 0; i < 16u; i++)
        pad_boot_set(i, i < head ? 0xC80000u : i == head ? 0xFF6060u : 0);
}

/* the device: the 1 s, then the chain goes black for the UI's first frame */
static void boot_anim(void)
{
#ifdef SKM_ON_DEVICE
    uint32_t t0, t;
    boot_screen();
    t0 = fm1_ms;
    do {
        t = fm1_ms - t0;
        boot_pads(t > BOOT_MS ? BOOT_MS : t);
        pad_boot_paint();
        if (t < BOOT_MS)
            fm1_delay_ms(20);                          /* 50 bar steps over 0.8 s */
    } while (t < BOOT_MS);
    pad_boot_clear();                                  /* the map takes the chain back */
    smk_rgb_fill(0, 0, 0);
    smk_rgb_show();
#endif
}
