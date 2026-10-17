/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SMK-37 drumpad RGB: a WS2812 chain on PC10, 16 pixels, GRB order.
 * Ported from firmware/probe/probe.c (the routine proven on hardware), where
 * the chain is bitbanged from TIMER4 (24 MHz): one bit is a high pulse
 * (T1H ~750 ns / T0H ~290 ns) and a low pulse (~290 ns / ~750 ns), a 1.25 us
 * period. No interrupts, no ROM calls; only hal/ may touch the port registers.
 *
 * smk_rgb_show() is the only writer. It is the caller's job to keep the call
 * off the audio ISR and out of the render's hot path (the panel refresh calls
 * it once per UI frame, and only when the picture changed). */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"
#include "smk_board.h"

#define SMK_RGB_PIN (1u << 10)                 /* PC10 */
#define SMK_RGB_N   SMK_NPADS                  /* 16 pixels, GRB */

/* The chain latches whatever it last saw; the buffer is what to draw. GRB. */
static uint8_t smk_rgb[SMK_RGB_N][3];

static void smk_rgb_init(void)
{
    FM1_PR(FM1_PC, FM1_DIE) |= SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_PU) &= ~SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_PD) &= ~SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_DIR) &= ~SMK_RGB_PIN;    /* input until a bit starts: the line idles low */
}

static void smk_rgb_bit(uint32_t one)
{
    uint32_t t, hi = one ? 18u : 7u, lo = one ? 8u : 16u;   /* TIMER4 ticks (24 MHz) */
    FM1_PR(FM1_PC, FM1_OUT) |= SMK_RGB_PIN;
    t = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t) < hi)
        ;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_RGB_PIN;
    t = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t) < lo)
        ;
}

/* Push the whole buffer out: 16 pixels * 24 bits, then the >50 us latch. */
static void smk_rgb_show(void)
{
    uint32_t n, i, pix;
    for (n = 0; n < SMK_RGB_N; n++) {
        pix = ((uint32_t)smk_rgb[n][0] << 16) | ((uint32_t)smk_rgb[n][1] << 8) | smk_rgb[n][2];
        for (i = 0; i < 24u; i++)
            smk_rgb_bit((pix >> (23u - i)) & 1u);
    }
    fm1_delay_us(300);                          /* latch */
}

/* Set one pixel (GRB) and remember it; caller decides when to flush. */
static inline void smk_rgb_set(uint32_t pad, uint32_t g, uint32_t r, uint32_t b)
{
    if (pad >= SMK_RGB_N)
        return;
    smk_rgb[pad][0] = (uint8_t)g;
    smk_rgb[pad][1] = (uint8_t)r;
    smk_rgb[pad][2] = (uint8_t)b;
}

static void smk_rgb_fill(uint32_t g, uint32_t r, uint32_t b)
{
    uint32_t n;
    for (n = 0; n < SMK_RGB_N; n++)
        smk_rgb_set(n, g, r, b);
}
