/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 Elite panel probe. Draws the live matrix, the 37 velocity keys,
 * the 8 encoders and the 16 muxed analog channels. Never touches flash.
 *
 * Matrix, from the stock scan (app.bin ISR at file 0x1be14, key table
 * 0x02058D58, encoders 0x020580E4):
 *   16 columns, 74HC595 chain on SPI2 port C: CLK PA3, DO PA4, latch PA1,
 *   output-enable PA0 held low. Column c selects bit (c<8 ? 24+c : 16+c-8).
 *   8 rows, active low, packed bit0 PA2, bit1 PA5, bit2 PA6, bit3 PA7,
 *   bit4 PA9, bit5 PH6, bit6 PH8, bit7 PH9.
 *   Switch index is (column << 4) | row. A key is two of those.
 * Analog mux select is bits 3..5 of the same shift word. Readings are
 * ADC ch1 (PA8) and ch2 (PA10), eight mux states each.
 *
 * A palm (28 or more cells closed for about 2.5 s) writes the UBOOT mailbox
 * and resets. Restore of a bad flash is still write 0 of restore_0_9D000.bin.
 */
#include <stdint.h>
#include "../hal/fm1_time.h"
#include "../hal/fm1_sys.h"

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];

#define PR(port, off) (*(volatile uint32_t *)(0x50000u + (uint32_t)(port) * 0x40u + (off)))
enum { PA = 0, PB = 1, PC = 2, PH = 7 };
enum { R_OUT = 0x00, R_IN = 0x04, R_DIR = 0x08, R_DIE = 0x0C, R_PU = 0x10, R_PD = 0x14, R_HD = 0x1C,
       R_DIEH = 0x20 };

#define SPI_CON  (*(volatile uint32_t *)0x11D00u)
#define SPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define SPI_BUF  (*(volatile uint32_t *)0x11D08u)
#define SPI_ADR  (*(volatile uint32_t *)0x11D0Cu)
#define SPI_CNT  (*(volatile uint32_t *)0x11D10u)
#define SPI2_CON  (*(volatile uint32_t *)0x11E00u)
#define SPI2_BAUD (*(volatile uint32_t *)0x11E04u)
#define SPI2_ADR  (*(volatile uint32_t *)0x11E0Cu)
#define SPI2_CNT  (*(volatile uint32_t *)0x11E10u)
#define IOMAP1   (*(volatile uint32_t *)0x51020u)
#define ADC_CON  (*(volatile uint32_t *)0x13100u)
#define ADC_RES  (*(volatile uint32_t *)0x13104u)
#define WLA_CON0 (*(volatile uint32_t *)0x11900u)

#define CS_BIT  (1u << 8)   /* PC8 */
#define DC_BIT  (1u << 5)   /* PB5 D/C */
#define SCK_BIT (1u << 6)   /* PB6 LCD clock */
#define MOSI_BIT (1u << 7)  /* PB7 LCD data */
#define CLK_BIT (1u << 3)   /* PA3 matrix clock, SPI2 */
#define DO_BIT  (1u << 4)   /* PA4 matrix data, SPI2 */
#define LAT_BIT (1u << 1)   /* PA1 matrix latch */
#define OE_BIT  (1u << 0)   /* PA0, stock gpio_write(0, 0) at file 0x4da8 */

/* 3x5 glyphs, five row-bytes, low 3 bits. Index 0..15 are hex, then letters. */
enum { G_HEX = 0, G_K = 16, G_E, G_Y, G_N, G_C, G_H, G_L, G_D, G_A, G_P, G_B, G_U, G_S, G_M, G_R, G_G };
static const uint8_t GLYPH[][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, {0x2, 0x6, 0x2, 0x2, 0x7}, {0x7, 0x1, 0x7, 0x4, 0x7}, {0x7, 0x1, 0x7, 0x1, 0x7},
    {0x5, 0x5, 0x7, 0x1, 0x1}, {0x7, 0x4, 0x7, 0x1, 0x7}, {0x7, 0x4, 0x7, 0x5, 0x7}, {0x7, 0x1, 0x1, 0x1, 0x1},
    {0x7, 0x5, 0x7, 0x5, 0x7}, {0x7, 0x5, 0x7, 0x1, 0x7}, {0x7, 0x5, 0x7, 0x5, 0x5}, {0x6, 0x5, 0x6, 0x5, 0x6},
    {0x7, 0x4, 0x4, 0x4, 0x7}, {0x6, 0x5, 0x5, 0x5, 0x6}, {0x7, 0x4, 0x7, 0x4, 0x7}, {0x7, 0x4, 0x7, 0x4, 0x4},
    {0x5, 0x6, 0x4, 0x6, 0x5}, {0x7, 0x4, 0x7, 0x4, 0x7}, {0x5, 0x2, 0x2, 0x2, 0x5}, {0x5, 0x7, 0x1, 0x1, 0x1},
    {0x7, 0x4, 0x4, 0x4, 0x7}, {0x4, 0x4, 0x4, 0x4, 0x7}, {0x4, 0x4, 0x7, 0x4, 0x4}, {0x6, 0x5, 0x5, 0x5, 0x6},
    {0x7, 0x5, 0x7, 0x5, 0x5}, {0x6, 0x5, 0x6, 0x5, 0x6}, {0x6, 0x5, 0x5, 0x5, 0x6}, {0x7, 0x2, 0x2, 0x2, 0x2},
    {0x5, 0x7, 0x7, 0x5, 0x7}, {0x6, 0x5, 0x5, 0x5, 0x6}, {0x7, 0x5, 0x7, 0x5, 0x5},
};

/* Firmware key order. Each pair is (first contact, second contact). */
static const uint8_t KEYS[37][2] = {
    {0x10, 0x52}, {0x20, 0x72}, {0x30, 0x92}, {0x40, 0xB2}, {0x50, 0xD2}, {0x60, 0xF2}, {0x70, 0xE2},
    {0x80, 0xC2}, {0x90, 0xA2}, {0xA0, 0x82}, {0xC0, 0x62}, {0xD0, 0x42}, {0xE0, 0x22}, {0xF0, 0x12},
    {0xB0, 0x32}, {0x11, 0x33}, {0x21, 0x43}, {0x31, 0x53}, {0x41, 0x63}, {0x51, 0x73}, {0x61, 0x83},
    {0x71, 0x93}, {0x81, 0xA3}, {0x91, 0xB3}, {0xA1, 0xC3}, {0xB1, 0xD3}, {0xC1, 0xE3}, {0xD1, 0x23},
    {0xE1, 0x13}, {0xF1, 0x14}, {0x64, 0x24}, {0x74, 0x34}, {0x84, 0x44}, {0x94, 0x54}, {0xA4, 0xD4},
    {0xB4, 0xF4}, {0xC4, 0xE4},
};
static const uint8_t ENCS[8][2] = {
    {0x97, 0xA7}, {0x37, 0x47}, {0x17, 0x27}, {0xB7, 0xC7},
    {0x57, 0x67}, {0x77, 0x87}, {0xD7, 0xE7}, {0xF7, 0x07},
};
enum { CW = 0x4182u, CCW = 0x2814u };

static uint8_t kind[128];          /* 0 free, 1 key first, 2 key second, 3 encoder */
static uint8_t raw[16];
static uint8_t held[40];
static uint8_t nheld;
static uint16_t enc_n[8];
static uint8_t enc_prev[8];
static uint16_t key_t0[37];
static uint16_t key_dt[37];
static uint8_t key_arm[37];
static uint16_t adc_v[24];
static uint32_t frame_n, palm_t0;
static uint32_t probe_stamp __attribute__((section(".data"))) = 0x50524F42u;

static uint16_t line[240];

static uint16_t rgb(uint32_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

/* Stock v015 LCD, from the init at file 0x2547a and the SPI1 service at 0x1b57a.
 * Port A (IOMAP_CON1 bit 4 clear): PB6 clock, PB7 data, PB5 D/C, PC8 CS.
 * PB7 is an input; the SPI block drives it. Baud is 4. Each command is its
 * own CS pulse, then the parameter bytes are a second CS pulse. */
static uint8_t spi_fail;
static uint8_t scratch[16];

static int spi_done(void)
{
    uint32_t n;
    for (n = 0; n < 200000u; n++)
        if (SPI_CON & 0x8000u) {
            SPI_CON |= 0x4000u;
            return 1;
        }
    SPI_CON |= 0x4000u;
    spi_fail = 1;
    return 0;
}

static void lcd_write(uint8_t cmd, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    PR(PB, R_OUT) &= ~DC_BIT;
    PR(PC, R_OUT) &= ~CS_BIT;
    SPI_CON |= 0x4000u;
    SPI_BUF = cmd;
    spi_done();
    PR(PC, R_OUT) |= CS_BIT;
    if (!n)
        return;
    for (i = 0; i < n && i < sizeof scratch; i++)
        scratch[i] = d[i];
    PR(PB, R_OUT) |= DC_BIT;
    PR(PC, R_OUT) &= ~CS_BIT;
    SPI_CON |= 0x4000u;
    SPI_ADR = (uint32_t)(uintptr_t)scratch;
    SPI_CNT = i;
    spi_done();
    PR(PC, R_OUT) |= CS_BIT;
}

static void lcd_cmd(uint8_t c) { lcd_write(c, 0, 0); }

static void lcd_pixels_begin(void)
{
    PR(PB, R_OUT) &= ~DC_BIT;
    PR(PC, R_OUT) &= ~CS_BIT;
    SPI_CON |= 0x4000u;
    SPI_BUF = 0x2C;
    spi_done();
    PR(PC, R_OUT) |= CS_BIT;
    PR(PB, R_OUT) |= DC_BIT;
    PR(PC, R_OUT) &= ~CS_BIT;
}

static void lcd_pixels(const uint8_t *p, uint32_t n)
{
    SPI_CON |= 0x4000u;
    SPI_ADR = (uint32_t)(uintptr_t)p;
    SPI_CNT = n;
    spi_done();
}

static void lcd_pixels_end(void) { PR(PC, R_OUT) |= CS_BIT; }

static void lcd_init(void)
{
    static const uint8_t b2[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t gm[] = {0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34};
    static const uint8_t mad[] = {0x00}, col[] = {0x05}, b7[] = {0x35}, vcom[] = {0x32};
    static const uint8_t c2[] = {0x01}, c3[] = {0x15}, c4[] = {0x20}, c6[] = {0x0F}, d0[] = {0xA4, 0xA1};
    uint32_t pins = DC_BIT | SCK_BIT | MOSI_BIT;
    uint32_t x;
    PR(PB, R_DIE) |= pins;
    PR(PB, R_DIEH) |= pins;
    PR(PB, R_PU) &= ~pins;
    PR(PB, R_PD) &= ~pins;
    PR(PB, R_DIR) = (PR(PB, R_DIR) & ~(DC_BIT | SCK_BIT)) | MOSI_BIT;
    PR(PC, R_DIE) |= CS_BIT;
    PR(PC, R_DIEH) |= CS_BIT;
    PR(PC, R_OUT) |= CS_BIT;
    PR(PC, R_DIR) &= ~CS_BIT;
    IOMAP1 &= ~0x10u;
    SPI_CON = 0x4021u;
    SPI_BAUD = 4u;
    fm1_delay_ms(100);
    lcd_cmd(0x11);
    fm1_delay_ms(120);
    lcd_write(0x36, mad, 1);
    lcd_write(0x3A, col, 1);
    lcd_write(0xB2, b2, 5);
    lcd_write(0xB7, b7, 1);
    lcd_write(0xBB, vcom, 1);
    lcd_write(0xC2, c2, 1);
    lcd_write(0xC3, c3, 1);
    lcd_write(0xC4, c4, 1);
    lcd_write(0xC6, c6, 1);
    lcd_write(0xD0, d0, 2);
    lcd_write(0xE0, gm, 14);
    lcd_write(0xE1, gm, 14);
    lcd_cmd(0x21);
    lcd_cmd(0x28);                         /* display OFF while the glass is filled */
    SPI_CON |= 0x2000u;
    fm1_delay_ms(50);
    {
        uint8_t box[4] = {0, 0, 0, 239};
        lcd_write(0x2A, box, 4);
        lcd_write(0x2B, box, 4);
    }
    for (x = 0; x < 240u; x++)
        line[x] = 0xFFFFu;
    lcd_pixels_begin();
    for (x = 0; x < 240u; x++) {
        if ((x & 31u) == 0u)
            fm1_wdt_feed();
        lcd_pixels((const uint8_t *)line, 480);
    }
    lcd_pixels_end();
    lcd_cmd(0x29);                         /* display ON, stock file offset 0x1edb6 */
}

/* Pad LEDs are a WS2812 chain on PC10. Bitbanged so a blank panel still
 * shows that this image is running. GRB, 16 pads. */
#define LED_BIT (1u << 10)

static void leds_init(void)
{
    PR(PC, R_DIE) |= LED_BIT;
    PR(PC, R_PU) &= ~LED_BIT;
    PR(PC, R_PD) &= ~LED_BIT;
    PR(PC, R_OUT) &= ~LED_BIT;
    PR(PC, R_DIR) &= ~LED_BIT;
}

static void ws_bit(uint32_t one)
{
    uint32_t t, hi = one ? 18u : 7u, lo = one ? 8u : 16u;   /* TIMER4 is 24 MHz */
    PR(PC, R_OUT) |= LED_BIT;
    t = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t) < hi)
        ;
    PR(PC, R_OUT) &= ~LED_BIT;
    t = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t) < lo)
        ;
}

static void leds(uint32_t g, uint32_t r, uint32_t b)
{
    uint32_t n, i, pix = (g << 16) | (r << 8) | b;
    for (n = 0; n < 16u; n++)
        for (i = 0; i < 24u; i++)
            ws_bit((pix >> (23u - i)) & 1u);
    fm1_delay_us(300);
}

static void lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    uint8_t b[4];
    b[0] = (uint8_t)(x0 >> 8);
    b[1] = (uint8_t)x0;
    b[2] = (uint8_t)(x1 >> 8);
    b[3] = (uint8_t)x1;
    lcd_write(0x2A, b, 4);
    b[0] = (uint8_t)(y0 >> 8);
    b[1] = (uint8_t)y0;
    b[2] = (uint8_t)(y1 >> 8);
    b[3] = (uint8_t)y1;
    lcd_write(0x2B, b, 4);
}

/* Stock clocks the 74HC595 from SPI2 port C (PA3 clock, PA4 data, baud 5,
 * CON 0x6020). Writing the byte count does nothing until CON bit 0 is set
 * (file 0x4da2). PA0 stays low so the chain's outputs are enabled. */
static uint32_t sr_buf __attribute__((aligned(4)));

static void sr_word(uint32_t w)
{
    uint32_t n;
    sr_buf = w;
    SPI2_CON |= 0x4000u;
    SPI2_ADR = (uint32_t)(uintptr_t)&sr_buf;
    SPI2_CNT = 4u;
    SPI2_CON |= 1u;
    for (n = 0; n < 200000u && !(SPI2_CON & 0x8000u); n++)
        ;
    SPI2_CON |= 0x4000u;
    PR(PA, R_OUT) |= LAT_BIT;
    for (n = 0; n < 32u; n++)
        ;
    PR(PA, R_OUT) &= ~LAT_BIT;
}

static uint32_t colbit(uint32_t c) { return c < 8u ? 24u + c : 16u + (c - 8u); }

static uint8_t read_rows(void)
{
    uint32_t pa = PR(PA, R_IN), ph = PR(PH, R_IN);
    uint32_t v = ((pa >> 2) & 1u) | ((pa >> 4) & 0xEu) | ((pa >> 5) & 0x10u) | ((ph >> 1) & 0x20u) |
                 ((ph >> 2) & 0xC0u);
    return (uint8_t)((~v) & 0xFFu);            /* 1 = contact closed */
}

static void matrix_init(void)
{
    uint32_t rows_a = (1u << 2) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 9);
    uint32_t rows_h = (1u << 6) | (1u << 8) | (1u << 9);
    uint32_t sr = LAT_BIT | CLK_BIT | DO_BIT | OE_BIT;
    uint32_t i;
    PR(PA, R_DIE) |= rows_a | sr;
    PR(PA, R_PD) &= ~(rows_a | sr);
    PR(PA, R_PU) = (PR(PA, R_PU) | rows_a) & ~sr;
    PR(PA, R_DIR) = (PR(PA, R_DIR) | rows_a) & ~sr;
    PR(PA, R_OUT) &= ~sr;              /* PA0 low: 595 outputs on */
    PR(PH, R_DIE) |= rows_h;
    PR(PH, R_PD) &= ~rows_h;
    PR(PH, R_PU) |= rows_h;
    PR(PH, R_DIR) |= rows_h;
    IOMAP1 = (IOMAP1 & ~0x10000u) | 0x20000u;   /* SPI2 port C */
    SPI2_CON = 0x6020u;
    SPI2_BAUD = 5u;
    sr_word(0xFFFFFFFFu);
    for (i = 0; i < 128u; i++)
        kind[i] = 0;
    for (i = 0; i < 37u; i++) {
        kind[KEYS[i][0]] = 1;
        kind[KEYS[i][1]] = 2;
    }
    for (i = 0; i < 8u; i++) {
        kind[ENCS[i][0]] = 3;
        kind[ENCS[i][1]] = 3;
        enc_prev[i] = 0xFF;
    }
}

static uint32_t adc_read(uint32_t ch)
{
    uint32_t t, v;
    ADC_CON = 0;
    if (WLA_CON0 & (1u << 14))
        WLA_CON0 &= ~(1u << 14);
    ADC_CON = 0xF04Eu | ((ch & 0xFu) << 8);
    ADC_CON |= 0x10u;
    ADC_CON |= 0x40u;
    for (t = 0; t < 20000u; t++)
        if (ADC_CON & 0x80u)
            break;
    v = ADC_RES & 0x3FFu;
    ADC_CON = 0x40u;
    ADC_CON = 0;
    return t == 20000u ? 0xFFFFu : v;
}

static void adc_init(void)
{
    uint32_t m = (1u << 8) | (1u << 10);
    uint32_t pc9 = 1u << 9;                 /* stock pin 41, ADC channel 8 */
    PR(PA, R_DIE) &= ~m;
    PR(PA, R_PU) &= ~m;
    PR(PA, R_PD) &= ~m;
    PR(PA, R_DIR) |= m;
    PR(PC, R_DIE) &= ~pc9;
    PR(PC, R_DIEH) &= ~pc9;
    PR(PC, R_PU) &= ~pc9;
    PR(PC, R_PD) &= ~pc9;
    PR(PC, R_DIR) |= pc9;
}

static int closed(uint32_t id) { return (raw[id >> 4] >> (id & 0xFu)) & 1; }

static void scan(void)
{
    uint32_t c, e, k, i;
    nheld = 0;
    for (c = 0; c < 16u; c++) {
        sr_word(0xFFFFFFFFu ^ (1u << colbit(c)));
        fm1_delay_us(80);
        raw[c] = read_rows();
        for (i = 0; i < 8u && nheld < 40u; i++)
            if (raw[c] & (1u << i))
                held[nheld++] = (uint8_t)((c << 4) | i);
    }
    sr_word(0xFFFFFFFFu);
    for (e = 0; e < 8u; e++) {
        uint32_t cur = ((uint32_t)closed(ENCS[e][0]) << 1) | (uint32_t)closed(ENCS[e][1]);
        uint32_t idx;
        if (enc_prev[e] == 0xFF)
            enc_prev[e] = (uint8_t)cur;
        if (cur != enc_prev[e]) {
            idx = ((uint32_t)enc_prev[e] << 2) | cur;
            if ((CW >> idx) & 1u)
                enc_n[e]++;
            else if ((CCW >> idx) & 1u)
                enc_n[e]--;
            enc_prev[e] = (uint8_t)cur;
        }
    }
    for (k = 0; k < 37u; k++) {
        int a = closed(KEYS[k][0]), b = closed(KEYS[k][1]);
        if (a && !b) {
            if (!key_arm[k]) {
                key_arm[k] = 1;
                key_t0[k] = (uint16_t)fm1_micros();
            }
        } else if (a && b && key_arm[k]) {
            key_dt[k] = (uint16_t)((uint16_t)fm1_micros() - key_t0[k]);
            key_arm[k] = 2;
        } else if (!a) {
            key_arm[k] = 0;
        }
    }
    for (i = 0; i < 8u; i++) {
        sr_word(0xFFFFFFC7u | (i << 3));
        fm1_delay_us(150);
        adc_v[i] = (uint16_t)adc_read(1u);
        adc_v[8u + i] = (uint16_t)adc_read(2u);
        adc_v[16u + i] = (uint16_t)adc_read(8u);
    }
    sr_word(0xFFFFFFFFu);
}

static void pix(uint32_t x, uint32_t y, uint32_t yy, uint16_t c)
{
    if (yy == y && x < 240u)
        line[x] = c;
}

static void stamp(uint32_t x, uint32_t y, uint32_t yy, uint32_t g, uint16_t fg)
{
    uint32_t r, col;
    if (yy < y || yy >= y + 10u || g >= sizeof GLYPH / sizeof GLYPH[0])
        return;
    r = GLYPH[g][(yy - y) >> 1];
    for (col = 0; col < 3u; col++)
        if (r & (4u >> col)) {
            pix(x + col * 2u, yy, yy, fg);
            pix(x + col * 2u + 1u, yy, yy, fg);
        }
}

static void hex4(uint32_t x, uint32_t y, uint32_t yy, uint32_t v, uint16_t fg)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        stamp(x + i * 8u, y, yy, (v >> (12u - 4u * i)) & 0xFu, fg);
}

static void hex2(uint32_t x, uint32_t y, uint32_t yy, uint32_t v, uint16_t fg)
{
    stamp(x, y, yy, (v >> 4) & 0xFu, fg);
    stamp(x + 8u, y, yy, v & 0xFu, fg);
}

static void paint(uint32_t yy)
{
    uint32_t x, i;
    uint16_t bg = rgb(0xFFFF), fg = rgb(0x0000), dim = rgb(0x4A69);
    uint16_t ky = rgb(0x001F), en = rgb(0x0400);
    for (x = 0; x < 240u; x++)
        line[x] = bg;
    if (palm_t0)
        for (x = 0; x < 240u; x++)
            line[x] = rgb(0x6000);
    /* title */
    stamp(2, 2, yy, G_S, fg);
    stamp(10, 2, yy, G_M, fg);
    stamp(18, 2, yy, G_K, fg);
    hex4(160, 2, yy, frame_n, dim);
    hex2(210, 2, yy, nheld, fg);
    /* column digits and the 16x8 grid */
    for (i = 0; i < 16u; i++)
        stamp(20 + i * 14u, 14, yy, i, dim);
    if (yy >= 26u && yy < 26u + 8u * 14u) {
        uint32_t row = 0, base = 26u;
        while (yy >= base + 14u) {
            base += 14u;
            row++;
        }
        stamp(2, base + 2u, yy, row, dim);
        if (yy - base >= 1u && yy - base < 13u) {
            for (i = 0; i < 16u; i++) {
                uint32_t id = (i << 4) | row;
                uint16_t c = rgb(0xDEFB);
                int down = (raw[i] >> row) & 1;
                if (kind[id] == 1 || kind[id] == 2)
                    c = down ? rgb(0x07FF) : ky;
                else if (kind[id] == 3)
                    c = down ? rgb(0x07E0) : en;
                else if (down)
                    c = rgb(0xFFE0);
                for (x = 0; x < 12u; x++)
                    pix(20 + i * 14u + x, yy, yy, c);
            }
        }
    }
    /* HELD: up to eight switch indices, then KEY, ENC, two ADC rows, GPIO */
    stamp(2, 142, yy, G_H, dim);
    for (i = 0; i < 8u && i < nheld; i++)
        hex2(16 + i * 24u, 142, yy, held[i], fg);
    if (nheld > 8u) {
        stamp(2, 154, yy, G_H, dim);
        for (i = 8; i < 16u && i < nheld; i++)
            hex2(16 + (i - 8u) * 24u, 154, yy, held[i], fg);
    }
    stamp(2, 168, yy, G_K, dim);
    for (i = 0; i < 37u; i++) {
        if (closed(KEYS[i][0]) || closed(KEYS[i][1])) {
            hex2(16, 168, yy, i, fg);
            stamp(36, 168, yy, G_A, closed(KEYS[i][0]) ? fg : dim);
            stamp(52, 168, yy, G_B, closed(KEYS[i][1]) ? fg : dim);
            hex4(68, 168, yy, key_dt[i], fg);
            break;
        }
    }
    stamp(2, 182, yy, G_E, dim);
    for (i = 0; i < 8u; i++)
        hex2(16 + i * 24u, 182, yy, enc_n[i] & 0xFFu, fg);
    stamp(2, 196, yy, G_A, dim);
    for (i = 0; i < 8u; i++)
        hex2(16 + i * 28u, 196, yy, adc_v[i] >> 2, adc_v[i] == 0xFFFFu ? rgb(0xF800) : fg);
    stamp(2, 210, yy, G_B, dim);
    for (i = 0; i < 8u; i++)
        hex2(16 + i * 28u, 210, yy, adc_v[8u + i] >> 2, adc_v[8u + i] == 0xFFFFu ? rgb(0xF800) : fg);
    stamp(2, 224, yy, G_C, dim);
    for (i = 0; i < 8u; i++)
        hex2(16 + i * 28u, 224, yy, adc_v[16u + i] >> 2, adc_v[16u + i] == 0xFFFFu ? rgb(0xF800) : fg);
}

static void draw(void)
{
    uint32_t y;
    lcd_window(0, 0, 239, 239);
    lcd_pixels_begin();
    for (y = 0; y < 240u; y++) {
        if ((y & 31u) == 0u)
            fm1_wdt_feed();
        paint(y);
        lcd_pixels((const uint8_t *)line, 480);
    }
    lcd_pixels_end();
}

static void probe_main(void)
{
    if (probe_stamp != 0x50524F42u)
        fm1_enter_uboot();
    leds_init();
    leds(80, 0, 0);                        /* green pads before the panel is touched */
    lcd_init();
    matrix_init();
    adc_init();
    for (;;) {
        uint32_t t0 = fm1_micros(), slot = 280000u, p, g, b;
        fm1_wdt_feed();
        scan();
        if (spi_fail)
            leds(0, 70, 0);
        else if (nheld > 1u)
            leds(0, 40, 0);                    /* more than one cell: press a single control */
        else if (nheld == 1u) {
            /* green blinks = column+1 (1..16), blue blinks = row+1 (1..8) */
            g = (uint32_t)(held[0] >> 4) + 1u;
            b = (uint32_t)(held[0] & 0xFu) + 1u;
            p = t0 % (g * slot + 500000u + b * slot + 700000u);
            if (p < g * slot)
                leds((p % slot) < 150000u ? 50u : 0u, 0, 0);
            else if (p < g * slot + 500000u)
                leds(0, 0, 0);
            else if ((p -= g * slot + 500000u) < b * slot)
                leds(0, 0, (p % slot) < 150000u ? 50u : 0u);
            else
                leds(0, 0, 0);
        } else if (t0 & 0x100000u)
            leds(12, 0, 0);                    /* idle: dim green, about one second on / off */
        else
            leds(0, 0, 0);
        /* Hold one control for 8 s, or leave it alone for 90 s. */
        if (nheld == 1u) {
            if (!palm_t0)
                palm_t0 = t0 | 1u;
            else if (t0 - palm_t0 > 20000000u)
                fm1_enter_uboot();
        } else {
            palm_t0 = 0;
        }
        if (t0 > 90000000u)
            fm1_enter_uboot();
        if (!spi_fail)
            draw();
        frame_n++;
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    fm1_wdt_arm(0x0E);
    probe_main();
    for (;;)
        ;
}
