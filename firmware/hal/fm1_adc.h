/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* SMK-37 SARADC. PA8 channel 1 (front pads), PA10 channel 2 (faders,
 * pitch, mod, sustain), PC9 channel 8 (back pads). The mux address is
 * bits 3..5 of the shift register, driven by the panel scan.
 *
 *   fm1_adc_init()        the three pins analog, no pulls
 *   fm1_adc_read(ch)      0..1023, -1 on timeout
 */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"

#define FM1_ADC_CON (*(volatile uint32_t *)0x13100u)
#define FM1_ADC_RES (*(volatile uint32_t *)0x13104u)
#define FM1_WLA_CON0 (*(volatile uint32_t *)0x11900u)
enum { FM1_ADC_PAD_A = 1, FM1_ADC_FADER = 2, FM1_ADC_PAD_B = 8, FM1_ADC_BATT = 15 };

static void fm1_adc_init(void)
{
    uint32_t pa = (1u << 8) | (1u << 10);
    uint32_t pc9 = 1u << 9;
    FM1_PR(FM1_PA, FM1_DIE) &= ~pa;
    FM1_PR(FM1_PA, FM1_PU) &= ~pa;
    FM1_PR(FM1_PA, FM1_PD) &= ~pa;
    FM1_PR(FM1_PA, FM1_DIR) |= pa;
    FM1_PR(FM1_PC, FM1_DIE) &= ~pc9;
    FM1_PR(FM1_PC, 0x20) &= ~pc9;          /* DIEH off: PC9 stays analog, not the LED clock */
    FM1_PR(FM1_PC, FM1_PU) &= ~pc9;
    FM1_PR(FM1_PC, FM1_PD) &= ~pc9;
    FM1_PR(FM1_PC, FM1_DIR) |= pc9;
}

static int32_t fm1_adc_convert(uint32_t ch)
{
    uint32_t t, v;
    FM1_ADC_CON = 0;
    FM1_ADC_CON = 0xF04Eu | ((ch & 0xFu) << 8);
    FM1_ADC_CON |= 0x10u;
    FM1_ADC_CON |= 0x40u;
    for (t = 0; t < 20000u; t++)
        if (FM1_ADC_CON & 0x80u)
            break;
    v = FM1_ADC_RES & 0x3FFu;
    FM1_ADC_CON = 0x40u;
    FM1_ADC_CON = 0;
    return t == 20000u ? -1 : (int32_t)v;
}

static int32_t fm1_adc_read(uint32_t ch)
{
    if (FM1_WLA_CON0 & (1u << 14))
        FM1_WLA_CON0 &= ~(1u << 14);
    return fm1_adc_convert(ch);
}

/* Average channel 15 after the PMU mux has had time to settle. ch is the
 * P3_ANA_CON4 subchannel: 0 bandgap, 5 VBAT/4. */
static int32_t smk_pmu_avg(uint32_t ch)
{
    uint8_t ana = fm1_p33_read(0x04u);
    int32_t s = 0, n = 0, i, v;
    fm1_p33_write(0x04u, (uint8_t)((ana & (uint8_t)~0x0Fu) | 0x21u | (uint8_t)((ch & 7u) << 1)));
    fm1_delay_us(100);
    for (i = 0; i < 6; i++) {
        v = fm1_adc_convert(15u);
        if (i >= 2 && v > 0) {
            s += v;
            n++;
        }
    }
    fm1_p33_write(0x04u, ana);
    return n ? s / n : -1;
}

/* Cell voltage from VBAT/4 against the 1.2 V bandgap, then onto the header's
 * 531 / 561 / 591 steps: under 3.5 V empty, 3.7 V one bar, 3.9 V two, above full. */
static int32_t fm1_batt_gauge(void)
{
    uint32_t wla = FM1_WLA_CON0;
    int32_t vbg, vbat, cell;
    fm1_irq_off();
    FM1_WLA_CON0 = (wla & ~(7u << 15) & ~(0xFu << 1)) | (4u << 15) | (2u << 1) | 1u | (1u << 14);
    vbg = smk_pmu_avg(0u);
    FM1_WLA_CON0 &= ~(1u << 14);
    vbat = smk_pmu_avg(5u);
    FM1_WLA_CON0 = wla;
    fm1_irq_on();
    if (vbg < 100 || vbat < 50)
        return -1;
    cell = vbat * 1200 / vbg * 4;                 /* bandgap is 1.2 V; VBAT pin is the cell / 4 */
    if (cell < 3450)
        return 500;
    if (cell < 3700)
        return 545;
    if (cell < 3900)
        return 575;
    return 610;
}
