/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 LCD: ST7789-class 240x240 on SPI1 port A.
 * SCK PB6, MOSI PB7 (SPI drives it; the pin stays an input), D/C PB5, CS PC8.
 * No reset pin. The backlight is wired to the power switch.
 * Same entry points as fm1_lcd_hw.h, so src/lcd.c is unchanged apart from
 * the init table.
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_gpio.h"

#define SMK_LCD_SPI_CON  (*(volatile uint32_t *)0x11D00u)
#define SMK_LCD_SPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define SMK_LCD_SPI_BUF  (*(volatile uint32_t *)0x11D08u)
#define SMK_LCD_SPI_ADR  (*(volatile uint32_t *)0x11D0Cu)
#define SMK_LCD_SPI_CNT  (*(volatile uint32_t *)0x11D10u)
#define SMK_LCD_IOMAP    (*(volatile uint32_t *)0x51020u)
#define SMK_LCD_DC  (1u << 5)
#define SMK_LCD_SCK (1u << 6)
#define SMK_LCD_MOSI (1u << 7)
#define SMK_LCD_CS  (1u << 8)

static uint32_t fm1_lcd_timeouts;

FM1_INLINE void fm1_lcd_hw_init(void)
{
    uint32_t pins = SMK_LCD_DC | SMK_LCD_SCK | SMK_LCD_MOSI;
    FM1_PR(FM1_PB, FM1_DIE) |= pins;
    FM1_PR(FM1_PB, 0x20) |= pins;                          /* DIEH */
    FM1_PR(FM1_PB, FM1_PU) &= ~pins;
    FM1_PR(FM1_PB, FM1_PD) &= ~pins;
    FM1_PR(FM1_PB, FM1_DIR) = (FM1_PR(FM1_PB, FM1_DIR) & ~(SMK_LCD_DC | SMK_LCD_SCK)) | SMK_LCD_MOSI;
    FM1_PR(FM1_PC, FM1_DIE) |= SMK_LCD_CS;
    FM1_PR(FM1_PC, 0x20) |= SMK_LCD_CS;
    FM1_PR(FM1_PC, FM1_OUT) |= SMK_LCD_CS;
    FM1_PR(FM1_PC, FM1_DIR) &= ~SMK_LCD_CS;
    SMK_LCD_IOMAP &= ~0x10u;                               /* SPI1 port A */
    SMK_LCD_SPI_CON = 0x4021u;
    SMK_LCD_SPI_BAUD = 4u;
    SMK_LCD_SPI_CON |= 0x2000u;                            /* strong clock, as stock does after the table */
}

FM1_INLINE void fm1_lcd_baud(uint32_t b) { SMK_LCD_SPI_BAUD = b; }

static void fm1_lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 4000000u && !(SMK_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u)
        fm1_lcd_timeouts++;
    SMK_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_lcd_deselect(void) { FM1_PR(FM1_PC, FM1_OUT) |= SMK_LCD_CS; }

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_PR(FM1_PB, FM1_OUT) &= ~SMK_LCD_DC;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_LCD_CS;
    SMK_LCD_SPI_CON |= 0x4000u;
    SMK_LCD_SPI_BUF = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_PR(FM1_PB, FM1_OUT) |= SMK_LCD_DC;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_LCD_CS;
    SMK_LCD_SPI_CON |= 0x4000u;
    SMK_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    SMK_LCD_SPI_CNT = n;
}
