/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
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
