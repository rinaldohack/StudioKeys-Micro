/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* SMK-37 LCD: 240x240 ST7789 on SPI1 port A.
 * PB6 clock, PB7 data (SPI drives it; the pin stays an input),
 * PB5 D/C, PC8 CS. No reset pin. The backlight is wired to the power switch.
 * The panel protocol is src/lcd.c. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_LCD_PB_OUT (*(volatile uint32_t *)0x50040u)
#define FM1_LCD_PB_DIR (*(volatile uint32_t *)0x50048u)
#define FM1_LCD_PB_DIE (*(volatile uint32_t *)0x5004Cu)
#define FM1_LCD_PB_DIEH (*(volatile uint32_t *)0x50060u)
#define FM1_LCD_PB_PU (*(volatile uint32_t *)0x50050u)
#define FM1_LCD_PB_PD (*(volatile uint32_t *)0x50054u)
#define FM1_LCD_PC_OUT (*(volatile uint32_t *)0x50080u)
#define FM1_LCD_PC_DIR (*(volatile uint32_t *)0x50088u)
#define FM1_LCD_PC_DIE (*(volatile uint32_t *)0x5008Cu)
#define FM1_LCD_PC_DIEH (*(volatile uint32_t *)0x500A0u)
#define FM1_LCD_IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
#define FM1_LCD_SPI_CON (*(volatile uint32_t *)0x11D00u)
#define FM1_LCD_SPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define FM1_LCD_SPI_BUF (*(volatile uint32_t *)0x11D08u)
#define FM1_LCD_SPI_ADR (*(volatile uint32_t *)0x11D0Cu)
#define FM1_LCD_SPI_CNT (*(volatile uint32_t *)0x11D10u)
#define FM1_LCD_DC (1u << 5)
#define FM1_LCD_CLK (1u << 6)
#define FM1_LCD_DO (1u << 7)
#define FM1_LCD_CS (1u << 8)
/* IOMAP_CON1 bit 4: 0 = SPI1 group A (PB6/PB7, the LCD), 1 = group B (PC9/PC10, the drumpad LEDs).
 * One SPI1, two pin groups: the LCD and the WS2812 chain share the engine and take turns (lcd.c). */
#define FM1_LCD_GROUP_B 0x10u
#define FM1_PADLED_BAUD 8u                     /* lsb / 9 = 6.67 MHz: one SPI byte per LED bit */
#define FM1_PADLED_CLK (1u << 9)               /* PC9: group B's clock - and ADC channel 8 (back pads) */

static uint32_t fm1_lcd_timeouts;

FM1_INLINE void fm1_lcd_hw_init(void)
{
    uint32_t pins = FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO;
    FM1_LCD_PB_DIE |= pins;
    FM1_LCD_PB_DIEH |= pins;
    FM1_LCD_PB_PU &= ~pins;
    FM1_LCD_PB_PD &= ~pins;
    FM1_LCD_PB_DIR = (FM1_LCD_PB_DIR & ~(FM1_LCD_DC | FM1_LCD_CLK)) | FM1_LCD_DO;
    FM1_LCD_PC_DIE |= FM1_LCD_CS;
    FM1_LCD_PC_DIEH |= FM1_LCD_CS;
    FM1_LCD_PC_OUT |= FM1_LCD_CS;
    FM1_LCD_PC_DIR &= ~FM1_LCD_CS;
    FM1_LCD_IOMAP_CON1 &= ~0x10u;          /* SPI1 port A: PB6 clock, PB7 data */
    FM1_LCD_SPI_CON = 0x4021u;
    FM1_LCD_SPI_BAUD = 4u;
    FM1_LCD_SPI_CON |= 0x2000u;            /* strong clock, as stock does after the init table */
}

FM1_INLINE void fm1_lcd_baud(uint32_t b) { FM1_LCD_SPI_BAUD = b; }

static void fm1_lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 4000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u)
        fm1_lcd_timeouts++;
    FM1_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_lcd_deselect(void) { FM1_LCD_PC_OUT |= FM1_LCD_CS; }

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_LCD_PB_OUT &= ~FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_BUF = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_LCD_PB_OUT |= FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}

/* ---- the drumpad RGB chain: SPI1 group B (PC10), the LCD deselected ----
 * The 16 WS2812-class LEDs are time-critical (a 1.25 us bit) and must never be bit-banged under the
 * audio and timer IRQs: an IRQ that stretches a pulse past its window resets the chain mid-frame, and
 * the pixels past it keep the previous frame's colour - the "seizure". Instead the whole frame goes out
 * as SPI bytes with the SPI clock doing the timing: one byte per LED bit, 0xF0 = 1 and 0xC0 = 0, MSB
 * first, at BAUD 8 (6.67 MHz -> 1.2 us a bit, the datasheet's 1.25 us with margin). The DMA engine then
 * shifts it with no CPU involvement and no jitter, whatever the interrupts do.
 * Stock v016 does exactly this, on the same pins, between LCD transfers. */
FM1_INLINE void fm1_padled_send(const void *p, uint32_t n)
{
#ifdef FM1_HOST_TIME
    (void)p;                               /* the host has no SPI1: smk_rgb_show's buffer is the result */
    (void)n;
#else
    FM1_LCD_PC_OUT |= FM1_LCD_CS;          /* the panel must ignore the bus */
    FM1_LCD_SPI_BAUD = FM1_PADLED_BAUD;
    FM1_LCD_IOMAP_CON1 |= FM1_LCD_GROUP_B; /* bit 4: SPI1 -> PC9 clock / PC10 data */
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
#endif
}

FM1_INLINE void fm1_padled_done(void)
{
#ifdef FM1_HOST_TIME
#else
    FM1_LCD_IOMAP_CON1 &= ~FM1_LCD_GROUP_B;  /* back to the LCD's pins .. */
    FM1_LCD_SPI_BAUD = 4u;                   /* .. and its clock */
    /* PC9 is ALSO the back pad row's ADC input (channel 8): the pin is driven as SPI1 group B's
     * clock for this frame, so it has to become an analog input again before the scan samples it.
     * fm1_adc_init() set exactly this, and the panel scan's next mux-B conversion is the one after
     * the frame, so nothing else re-does it: without these three lines the whole back row reads the
     * clock line and the pads go dead. */
    FM1_PR(FM1_PC, FM1_DIE) &= ~FM1_PADLED_CLK;
    FM1_PR(FM1_PC, 0x20) &= ~FM1_PADLED_CLK;
    FM1_PR(FM1_PC, FM1_DIR) |= FM1_PADLED_CLK;
#endif
}
