/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* SMK-37 drumpad RGB: a WS2812-class chain of 16 pixels on SPI1 group B (PC10), GRB order.
 *
 * The frame is sent by the SPI1 DMA engine, not bit-banged: one SPI byte per LED bit, 0xF0 = 1 and
 * 0xC0 = 0, MSB first, at BAUD 8 (6.67 MHz, ~1.2 us a bit). The SPI clock *is* the LED timing, so
 * nothing the interrupts do can stretch a pulse - an ISR arriving mid-bit while the line was driven
 * from C would hold a pulse long past the 1.25 us window, and the chain resets part-way through the
 * frame. That was the "seizure": the pixels before the reset kept the new frame's colour, the rest
 * kept the old one, and the audio and timer ISRs land at different offsets every frame.
 *
 * Two more things the chain needs, both learned from units that misbehaved:
 *  - a LONG reset before each frame. The stock firmware's 36 zero bytes (43 us) is not enough for
 *    newer WS2812 parts: a glitch on PC10 while SPI1 switches pin groups counts as a first bit, the
 *    chain shifts by one and a lit pad lights its chain neighbour. 250 bytes (~300 us) settles it.
 *  - GRB byte order and the serpentine chain map (SMK_LED_OF), not pad order.
 *
 * smk_rgb_show() is the only writer. It is the caller's job to keep it off the audio ISR and out of
 * the render's hot path: the panel refresh calls it once per UI frame, and only when a colour changed. */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"
#include "fm1_lcd_hw.h"
#include "smk_board.h"

#define SMK_RGB_PIN (1u << 10)                 /* PC10 */
#define SMK_RGB_N   SMK_NPADS                  /* 16 pixels, GRB */
/* Zeros ahead of the frame: the reset / latch, sized for the WHOLE chain, not for one row.
 *
 * The 16 pads are 8+8 with their own button-LED commons on separate GPIOs, and the UI paints and
 * flushes them in two passes (pad_boot_paint, then the map). A flush between the passes leaves the
 * chain latched with only the first row moved; without a real reset in front of the second frame the
 * part appends it as a continuation, so one row holds the new colours while the other never updates.
 *
 * The arithmetic, at BAUD 8 (lsb/9 = 6.67 MHz, so a bit is 150 ns and one SPI byte - 8 bits - is
 * 1.2 us): the reset must be comfortably past the part's 280-300 us. 36 bytes (the stock v016
 * figure) is 43 us and 120 bytes is only 144 us - both under spec, which is what let a glitch on
 * PC10 at the pin-group switch count as a first bit and shift the chain by one. 250 bytes is 300 us,
 * the reference port's figure and the margin this needs. A frame therefore costs 250 x 1.2 us =
 * 300 us plus 16 x 24 x 1.2 us = 461 us, about 0.76 ms, which is why the caller only reaches here
 * when a colour actually changed. */
#define SMK_RGB_LATCH 250u
#define SMK_RGB_LEN (SMK_RGB_LATCH + SMK_RGB_N * 24u)

/* The pixels to draw, as each pixel's own 0xGGRRBB (the byte order the chain wants).
 *
 * Indexed by CHAIN POSITION, not by pad: the buffer is the chain, so all the map does in here is pick
 * which slot a pad's colour is written to, and smk_rgb_show() walks the buffer as it is. Mapping the
 * buffer again on the way out scrambled the chain - SMK_LED_OF is a serpentine table, not an
 * involution, so SMK_LED_OF[SMK_LED_OF[p]] sent 15 of the 16 pads to another pad's pixel (the
 * left-to-right drift on the glass, and with it the R/G confusion the seam made look like a byte
 * order fault). */
static uint8_t smk_rgb[SMK_RGB_N][3];
/* The bytes handed to the DMA: SMK_RGB_LATCH zeros, then 16 x 24 one-byte-per-bit codes. */
static uint8_t smk_rgb_buf[SMK_RGB_LEN] __attribute__((aligned(4)));
static uint8_t smk_rgb_dirty = 1;

static void smk_rgb_init(void)
{
    uint32_t i;
    /* Hold PC10 low whenever SPI1 is not driving it, so the line always idles low and the latch
     * time is met between frames (the reference port keeps it a low output for the same reason). */
    FM1_PR(FM1_PC, FM1_DIE) |= SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_PU) &= ~SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_PD) &= ~SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_RGB_PIN;
    FM1_PR(FM1_PC, FM1_DIR) &= ~SMK_RGB_PIN;     /* low output */
    for (i = 0; i < SMK_RGB_LATCH; i++)
        smk_rgb_buf[i] = 0;
    smk_rgb_dirty = 1;
}

/* Set one pad's pixel. `pad` is the pad's printed number - 1 (the scan / note order); the chain is
 * serpentine, so SMK_LED_OF (smk_board.h) - the stock firmware's own pad -> pixel table - maps it to
 * the pixel the player is looking at. This is the ONE place the map is applied: the buffer it writes
 * is the chain itself, so nothing downstream may map again. */
static inline void smk_rgb_set(uint32_t pad, uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t px;
    if (pad >= SMK_RGB_N)
        return;
    px = SMK_LED_OF[pad & 15u];
    if (smk_rgb[px][0] == (uint8_t)g && smk_rgb[px][1] == (uint8_t)r && smk_rgb[px][2] == (uint8_t)b)
        return;                                  /* unchanged: nothing to re-encode or send */
    smk_rgb[px][0] = (uint8_t)g;
    smk_rgb[px][1] = (uint8_t)r;
    smk_rgb[px][2] = (uint8_t)b;
    smk_rgb_dirty = 1;
}

static void smk_rgb_fill(uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t n;
    for (n = 0; n < SMK_RGB_N; n++) {            /* every pixel: a fill is order-independent */
        smk_rgb[n][0] = (uint8_t)g;
        smk_rgb[n][1] = (uint8_t)r;
        smk_rgb[n][2] = (uint8_t)b;
    }
    smk_rgb_dirty = 1;
}

/* Push the whole frame out: encode after the latch, then one SPI1 DMA at about 0.76 ms. Returns 1 if
 * a frame was actually sent, 0 if nothing changed (and so PC9 was never driven as a clock). The caller
 * must have the LCD idle (lcd.c's fm1_pads_led does). */
static int smk_rgb_show(void)
{
    uint32_t p, i;
    uint8_t *o;
    if (!smk_rgb_dirty)
        return 0;
    smk_rgb_dirty = 0;
    for (p = 0; p < SMK_RGB_N; p++) {
        /* three GRB bytes, MSB first, each bit as one SPI byte */
        uint32_t v = ((uint32_t)smk_rgb[p][0] << 16) | ((uint32_t)smk_rgb[p][1] << 8) | smk_rgb[p][2];
        o = smk_rgb_buf + SMK_RGB_LATCH + p * 24u;
        for (i = 0; i < 24u; i++)
            o[i] = (uint8_t)((v >> (23u - i)) & 1u ? 0xF0u : 0xC0u);
    }
    fm1_padled_send(smk_rgb_buf, sizeof smk_rgb_buf);
    return 1;
}
