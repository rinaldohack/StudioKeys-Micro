/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* SMK-37 panel scan. The 16x8 matrix is a 74HC595 chain on SPI2 (PA3 clock,
 * PA4 data, PA1 latch, PA0 output-enable held low). Keys and pads are not
 * StudioKEYS's 27-key bitmap: edges go into smk_q and smk_panel_poll() turns
 * them into MIDI (source 3) on the same queue as USB.
 *
 * One column per fm1_input_tick (10 kHz). Pads are read one mux address per
 * frame, front row on ADC 1 and back row on ADC 8. */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"
#include "smk_board.h"

#define FM1_NCOL SMK_NCOL
#define FM1_NENC SMK_NENC
#define FM1_GAP_BINS 8u
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };

static volatile struct {
    uint32_t notes;              /* unused: the keybed is MIDI, not this bitmap */
    uint32_t buttons;            /* bit0 OCT-, bit1 OCT+ (the power-on calibration gesture) */
    uint32_t pressed, released;
    uint32_t notes_pressed;
    uint8_t raw[SMK_NCOL];
    uint32_t note_t0[27];
    uint8_t enc_prev[SMK_NENC], enc_last[SMK_NENC], enc_rest[SMK_NENC];
    uint16_t enc_still[SMK_NENC];
    int8_t enc_sub[SMK_NENC];
    int16_t enc_steps[SMK_NENC];
    uint32_t frames;
} fm1_in;

static uint8_t fm1__dim_k;
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[FM1_NCOL];
static uint8_t fm1_led_breath[FM1_NCOL];

static volatile struct {
    uint32_t last, gap_max, gap_hist[FM1_GAP_BINS];
    uint32_t on[FM1_NCOL], on_max[FM1_NCOL];
    uint32_t cost_sum, cost_max, ticks;
    uint32_t enc_moves[FM1_NENC], enc_lost[FM1_NENC];
    uint32_t press_n, press_sum, press_max;
    uint32_t kb_n, kb_sum, kb_max, dac_sum, dac_max;
    uint32_t dim_n, dim_sum;
} fm1_in_stat;

#define SMK_Q 32u
static volatile uint8_t smk_q_st[SMK_Q], smk_q_d1[SMK_Q], smk_q_d2[SMK_Q];
static volatile uint32_t smk_qw, smk_qr;

static uint8_t smk_key_st[SMK_NKEYS];
static uint16_t smk_key_t0[SMK_NKEYS];
static uint8_t smk_key_da[SMK_NKEYS], smk_key_dbb[SMK_NKEYS];   /* debounced first / second contact */
static uint8_t smk_key_ra[SMK_NKEYS], smk_key_rb[SMK_NKEYS];   /* the previous raw sample of each contact */
static uint8_t smk_key_ca[SMK_NKEYS], smk_key_cb[SMK_NKEYS];   /* consecutive frames the raw held */
#define SMK_DEB_FRAMES 2u                                       /* a contact must hold 2 frames (3.2 ms) to change */

/* The 595's column select: active-low one-hot, cols 0..7 in bits 31..24 and cols 8..15 in bits
 * 23..16 (the stock table; the fork's smk_input.h SMK_COLW(c) is the same expression). Every
 * matrix code in smk_board.h is `(column + 1) << 4 | row`, i.e. the stock's 1-based column in the
 * high nibble, which is exactly this bit once the low nibble has been cleared. Shifting this into
 * the word is what makes the scan read ONE column at a time. */
#define SMK_COLW(c) (~((c) < 8u ? 0x01000000u << (c) : 0x00010000u << ((c) - 8u)))

static uint8_t smk_pad_down[SMK_NPADS];
static uint8_t smk_pad_p[SMK_NPADS];
/* P16: each pad's own rest level, in ADC counts (the stock firmware's `padgate`). A pad's shim is
 * loaded by the chassis, so the pads do not idle at the same count and they drift with temperature:
 * the press / release points ride on this instead of the fixed 72 / 32, which a pad idling at 40
 * could never cross on a light hit and one idling at 90 could never release. */
static uint8_t smk_pad_rest[SMK_NPADS];
static uint8_t smk_pad_rise[SMK_NPADS];      /* P16: boot samples taken, then passes over the rest */
static uint16_t smk_pad_rest_seen;              /* bit per pad: the rest level has been taken (P16) */
#define SMK_PAD_ON 24u                          /* counts over the rest level: a press (the reference port's
                                                 * value; 72 lost the light hits at the top of a pad) */
#define SMK_PAD_OFF 12u                         /* .. and the release, likewise (was 32) */
#define SMK_PAD_REST_MAX 160u                   /* the rest never climbs past this: a dead pad stays pressable */
#define SMK_PAD_RISE 32u                        /* passes over the rest before it is believed (~0.5 s) */
#define SMK_PAD_SLEW 2u                         /* counts the rest may fall per pass (one bad sample is not a new rest) */
#define SMK_PAD_BOOT 4u                         /* samples the boot level is taken over (the highest of them) */
static uint8_t smk_pad_at;
static uint32_t smk_pad_edge, smk_pad_rel;   /* P4: bit per pad, set on a press / release since last read */
static uint8_t smk_col, smk_mux;
static uint32_t smk_sr_buf __attribute__((aligned(4)));
#ifdef FM1_HOST_TIME
static uint32_t smk_host_word;          /* the last word smk_sr_start() built (host tests only) */
#endif
/* the 24-slot analog pipeline (fm1_input_tick): 8 mux addresses x {A, B, C}, one conversion a tick */
static uint8_t smk_slot;                 /* the slot whose address goes out with the next word */
static uint8_t smk_adc_slot = 0xFFu;     /* the slot whose conversion is running (0xFF: none) */
static void smk_analog(uint32_t addr, uint32_t slot, int raw);
static void smk_pad_one(uint32_t pad, int raw);
static volatile int8_t smk_oct;
static volatile uint8_t smk_part;          /* selected part: the keys, wheels and pedal follow it */
static uint8_t smk_ctrl;                   /* 1: panel is a generic MIDI controller, engine stays quiet */
static uint8_t smk_fbank;                  /* P6g: FADER BANK's two fixed fader CC banks (0: 41..44, 1: 45..48) */
static uint8_t smk_note_ch[SMK_NKEYS];
static uint8_t smk_note_n[SMK_NKEYS];   /* the pitch sent at note-on; octave can move before the key lifts */
static volatile int16_t smk_an[8];
/* the pitch / mod / sustain wheels' last raw (SMK_PITCH, SMK_MOD, SMK_SUSTAIN carry no smk_an slot):
 * MENU > SYSTEM > TEST MODE shows them live (ui_input.c sk_test_mode) */
static volatile int16_t smk_wheel[3];
/* MENU > SYSTEM > CALIBRATE PITCH BEND / CALIBRATE MOD WHEEL (wheel_cal.c): what the pots themselves
 * report at their ends, in place of the factory SMK_PITCH_* / SMK_MOD_* numbers, plus the scales worked
 * out from them once (Q10 per raw count) so the scan stays a multiply. A wheel is calibrated on its own:
 * bit 0 of the flag is the pitch wheel, bit 1 the mod wheel. The bit is cleared while a set is being
 * replaced, so a scan in between reads the factory numbers for one sample (a bend / level nobody hears) */
static volatile int16_t smk_wcal[5];          /* pitch centre, up, down; mod zero, full */
static volatile int32_t smk_wsc[2];           /* pitch: bend Q10 per raw count, up / down */
static volatile int32_t smk_msc;              /* mod: level 0..127 Q10 per raw count */
static volatile uint8_t smk_wcal_ok;          /* 1: pitch, 2: mod (both: 3) */
#define SMK_WCAL_PITCH 1u
#define SMK_WCAL_MOD 2u
#ifndef SMK_WCAL_MIN
#define SMK_WCAL_MIN 16                        /* the least travel a calibrated wheel can have (a stuck pot: no) */
#endif
#define SMK_WCAL_SCAN 1                        /* the scan (and these hooks) exist: wheel_cal.h applies to it */
static uint32_t smk_btn_lvl, smk_btn_raw, smk_btn_edge;
static uint32_t smk_x_lvl, smk_x_raw, smk_x_edge;

enum { SMK_CW = 0x4182u, SMK_CCW = 0x2814u };
#define SMK_REST_FRAMES 900u   /* ~1 s still off the detent state (25 ms frames): that is the detent */
#define SMK_SPI2_CON  (*(volatile uint32_t *)0x11E00u)
#define SMK_SPI2_BAUD (*(volatile uint32_t *)0x11E04u)
#define SMK_SPI2_ADR  (*(volatile uint32_t *)0x11E0Cu)
#define SMK_SPI2_CNT  (*(volatile uint32_t *)0x11E10u)
#define SMK_IOMAP1    (*(volatile uint32_t *)0x51020u)
#define SMK_ADC_CON   (*(volatile uint32_t *)0x13100u)
#define SMK_ADC_RES   (*(volatile uint32_t *)0x13104u)
#define SMK_WLA_CON0  (*(volatile uint32_t *)0x11900u)
#define SMK_OE  (1u << 0)
#define SMK_LAT (1u << 1)
#define SMK_CLK (1u << 3)
#define SMK_DO  (1u << 4)

static int smk_ev_expend(uint32_t st, uint32_t d1)
{
    uint32_t k = st & 0xF0u;
    return k == 0xE0u || k == 0xD0u || (k == 0xB0u && d1 == 1u);   /* bend, pad pressure, mod wheel */
}
static void smk_ev(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t at, kind = st & 0xF0u;
    int vital = kind == 0x80u || kind == 0x90u || (kind == 0xB0u && d1 == 64u);
    if (smk_qw - smk_qr >= SMK_Q) {
        uint32_t n, i;
        if (!vital)
            return;                                    /* a full queue drops another bend, not a note */
        n = smk_qw - smk_qr;
        for (i = 1; i <= n; i++) {
            uint32_t ix = (smk_qw - i) % SMK_Q;
            if (ix == (smk_qr % SMK_Q))
                continue;
            if (smk_ev_expend(smk_q_st[ix], smk_q_d1[ix])) {
                smk_q_st[ix] = (uint8_t)st;
                smk_q_d1[ix] = (uint8_t)d1;
                smk_q_d2[ix] = (uint8_t)d2;
                return;
            }
        }
        return;
    }
    at = smk_qw % SMK_Q;
    smk_q_st[at] = (uint8_t)st;
    smk_q_d1[at] = (uint8_t)d1;
    smk_q_d2[at] = (uint8_t)d2;
    smk_qw++;
}

static void smk_sr(uint32_t w)
{
    uint32_t n;
    smk_sr_buf = w;
    SMK_SPI2_CON |= 0x4000u;
    SMK_SPI2_ADR = (uint32_t)(uintptr_t)&smk_sr_buf;
    SMK_SPI2_CNT = 4u;
    SMK_SPI2_CON |= 1u;
    for (n = 0; n < 200000u && !(SMK_SPI2_CON & 0x8000u); n++)
        ;
    SMK_SPI2_CON |= 0x4000u;
    FM1_PR(FM1_PA, FM1_OUT) |= SMK_LAT;
    FM1_PR(FM1_PA, FM1_OUT) &= ~SMK_LAT;
}

/* Start the shift of the next column's word and return; the latch pulse of the word started on the
 * previous tick happens on the next one. 4 bytes at BAUD 5 (~10 us) finish long before the tick is
 * over, so nothing here waits (the reference port's ISR order; waiting here stretched the tick). */
static void smk_sr_start(uint32_t w)
{
    smk_sr_buf = w;
#ifdef FM1_HOST_TIME
    /* the host has no SPI2: record what would have been shifted and latched, so a test can check the
     * word the scan builds (the column select in the top half, the mux address in bits 3..5) - the one
     * thing the raw[] injection below cannot see */
    smk_host_word = w;
#else
    SMK_SPI2_CON |= 0x4000u;
    SMK_SPI2_ADR = (uint32_t)(uintptr_t)&smk_sr_buf;
    SMK_SPI2_CNT = 4u;
#endif
}

static void smk_latch(void)
{
    FM1_PR(FM1_PA, FM1_OUT) |= SMK_LAT;
    FM1_PR(FM1_PA, FM1_OUT) &= ~SMK_LAT;
}

static uint32_t smk_colbit(uint32_t c) { return c < 8u ? 24u + c : 16u + (c - 8u); }

static uint8_t smk_rows(void)
{
    uint32_t pa = FM1_PR(FM1_PA, FM1_IN), ph = FM1_PR(FM1_PH, FM1_IN);
    uint32_t v = ((pa >> 2) & 1u) | ((pa >> 4) & 0xEu) | ((pa >> 5) & 0x10u) | ((ph >> 1) & 0x20u) |
                 ((ph >> 2) & 0xC0u);
    return (uint8_t)((~v) & 0xFFu);
}

static int smk_closed(uint32_t id) { return (fm1_in.raw[id >> 4] >> (id & 0xFu)) & 1; }

/* Start one conversion and return at once; smk_adc_done() reads the result on a later tick.
 *
 * The SMK's own timing, not the FM-1's: `0x400D | ch << 8` is a start delay of 4 x 8 ADC clocks at
 * a /16 divider, ~40 us a conversion - comfortably inside one 100 us tick. The FM-1's `0xF04E` (delay
 * 15 x 8 clocks at /96) takes ~200 us, which is how the pads and faders came to read nothing: the
 * conversion started on one tick had not finished by the next one, and the second and third reads of
 * the frame never completed at all. That was the dead second row and the lost knob steps. */
static void smk_adc_start(uint32_t ch)
{
    if (SMK_WLA_CON0 & (1u << 14))
        SMK_WLA_CON0 &= ~(1u << 14);
    SMK_ADC_CON = 0;
    SMK_ADC_CON = 0x400Du | ((ch & 0xFu) << 8);
    SMK_ADC_CON |= 0x10u;                  /* enable */
    SMK_ADC_CON |= 0x40u;                  /* start */
}

/* 1 and the 10-bit result when the conversion has finished, else 0. */
static int smk_adc_done(int *out)
{
    if (!(SMK_ADC_CON & 0x80u))
        return 0;
    *out = (int)(SMK_ADC_RES & 0x3FFu);
    SMK_ADC_CON = 0x40u;                   /* clear the pending flag */
    SMK_ADC_CON = 0;
    return 1;
}

/* One blocking conversion, for the paths that are not the 10 kHz tick (boot, the console's probes). */
static int smk_adc(uint32_t ch)
{
    uint32_t t;
    int v;
    smk_adc_start(ch);
    for (t = 0; t < 20000u; t++)
        if (smk_adc_done(&v))
            return v;
    SMK_ADC_CON = 0x40u;
    SMK_ADC_CON = 0;
    return -1;
}

static uint32_t smk_vel(uint32_t dt_us)
{
    if (dt_us < 1500u)
        return 127u;
    if (dt_us > 28000u)
        return 8u;
    return 127u - (dt_us - 1500u) * 119u / 26500u;
}

/* A calibrated wheel: the pitch wheel's released count is the bend's centre and its full up / down travel the
 * ends; the mod wheel (no spring) has only its bottom (zero) and its top. Anything else, including the all-zero
 * set, is the factory numbers. The scales are Q10 so a scan converts with a multiply and a shift:
 * (x * scale) >> 10, x the counts from the centre (< 2: the centre's own jitter). Each wheel is set on its own.
 * The scale is rounded UP, so the learned end itself reaches the wheel's last count (+8191 / -8192, 127) instead
 * of falling a step short on a travel that does not divide the range: the scan clamps either end anyway. */
static void smk_pitch_cal(int32_t pc, int32_t pu, int32_t pd)
{
    int32_t up = pu - pc, dn = pc - pd;
    smk_wcal_ok &= (uint8_t)~SMK_WCAL_PITCH;
    if (!(pc > 0 && pc < 1023 && pd >= 0 && pu <= 1023 && up >= SMK_WCAL_MIN && dn >= SMK_WCAL_MIN)) {
        smk_wcal[0] = smk_wcal[1] = smk_wcal[2] = 0;    /* factory */
        smk_wsc[0] = smk_wsc[1] = 0;
        return;
    }
    smk_wsc[0] = ((8191 << 10) + up - 1) / up;
    smk_wsc[1] = ((8192 << 10) + dn - 1) / dn;
    smk_wcal[0] = (int16_t)pc;
    smk_wcal[1] = (int16_t)pu;
    smk_wcal[2] = (int16_t)pd;
    smk_wcal_ok |= SMK_WCAL_PITCH;
}
static void smk_mod_cal(int32_t mz, int32_t mf)
{
    int32_t span = mz - mf;
    smk_wcal_ok &= (uint8_t)~SMK_WCAL_MOD;
    if (!(mz > 0 && mz <= 1023 && mf >= 0 && span >= SMK_WCAL_MIN)) {
        smk_wcal[3] = smk_wcal[4] = 0;                  /* factory */
        smk_msc = 0;
        return;
    }
    smk_msc = ((127 << 10) + span - 1) / span;
    smk_wcal[3] = (int16_t)mz;
    smk_wcal[4] = (int16_t)mf;
    smk_wcal_ok |= SMK_WCAL_MOD;
}
/* a wheel's calibration and whether it is one, for the screens that show them (ui_input.c) */
static int16_t smk_wheel_cal_of(uint32_t k) { return k < 5u ? smk_wcal[k] : 0; }
static int smk_wheel_calibrated(uint32_t wheel) { return (smk_wcal_ok & (1u << wheel)) != 0u; }

static void smk_pitch(int raw)
{
    static int16_t sent = 0x7FFF;
    static uint8_t ch = 0xFF;
    int32_t bend, x;
    uint32_t part = smk_ctrl ? 0u : (smk_part & 15u);
    if (raw < 0)
        return;
    if (smk_wcal_ok & SMK_WCAL_PITCH) {
        x = raw - (int32_t)smk_wcal[0];
        if (x > 1023) x = 1023;
        if (x < -1023) x = -1023;
        bend = x > -2 && x < 2 ? 0 : (x * (x < 0 ? smk_wsc[1] : smk_wsc[0])) >> 10;
    } else {
        /* The wheel's travel is about ±120 counts. << 6 fills the 14-bit bend
         * without a divide (a folded divisor looks like a ROM address). */
        x = raw - (int32_t)SMK_PITCH_MID;
        bend = x > -12 && x < 12 ? 0 : x << 6;
    }
    if (bend < -8192)
        bend = -8192;
    if (bend > 8191)
        bend = 8191;
    if (ch == part && sent != 0x7FFF && (bend > sent ? bend - sent : sent - bend) < 32)
        return;
    ch = (uint8_t)part;
    sent = (int16_t)bend;
    x = bend + 8192;
    smk_ev(0xE0u | part, (uint32_t)x & 0x7Fu, ((uint32_t)x >> 7) & 0x7Fu);
}

static void smk_mod(int raw)
{
    static int16_t sent = -1;
    static uint8_t ch = 0xFF;
    int32_t v, x;
    uint32_t part = smk_ctrl ? 0u : (smk_part & 15u);
    if (raw < 0)
        return;
    if (smk_wcal_ok & SMK_WCAL_MOD) {
        /* wheel down is zero; past the top it stays 127 (ignored, not extrapolated) */
        x = (int32_t)smk_wcal[3] - raw;
        if (x < 0)
            x = 0;
        if (x > 1023)
            x = 1023;
        v = (x * smk_msc) >> 10;
    } else {
        v = ((int32_t)SMK_MOD_ZERO - raw) >> 1;   /* wheel down 0, wheel up just under 127 */
    }
    if (v < 0)
        v = 0;
    if (v > 127)
        v = 127;
    if (ch == part && v == sent)
        return;
    ch = (uint8_t)part;
    sent = (int16_t)v;
    smk_ev(0xB0u | part, 1u, (uint32_t)v);
}

static void smk_sustain(int raw)
{
    static uint8_t down, ch;
    int now;
    uint32_t part = smk_ctrl ? 0u : (smk_part & 15u);
    if (raw < 0)
        return;
    now = down ? raw < 600 : raw < 256;
    if ((uint32_t)ch != part) {
        if (down)
            smk_ev(0xB0u | ch, 64u, 0u);
        ch = (uint8_t)part;
        if (now)
            smk_ev(0xB0u | part, 64u, 127u);
        down = (uint8_t)now;
        return;
    }
    if (now == down)
        return;
    down = (uint8_t)now;
    smk_ev(0xB0u | part, 64u, now ? 127u : 0u);
}

static void smk_keys(void)
{
    uint32_t k;
    for (k = 0; k < SMK_NKEYS; k++) {
        int ra = smk_closed(SMK_KEYS[k][0]), rb = smk_closed(SMK_KEYS[k][1]);
        int a, b;
        int32_t note = (int32_t)SMK_KEY_NOTE0 + (int32_t)k + (int32_t)smk_oct * 12;
        if (note < 0)
            note = 0;
        if (note > 127)
            note = 127;
        /* per-contact debounce: a contact must read the same for SMK_DEB_FRAMES frames to move
         * (rejects the 1-frame glitch a bounce can leave at a sample instant) */
        smk_key_ca[k] = (uint8_t)(ra == smk_key_ra[k] ? smk_key_ca[k] + 1u : 0u);
        smk_key_cb[k] = (uint8_t)(rb == smk_key_rb[k] ? smk_key_cb[k] + 1u : 0u);
        smk_key_ra[k] = (uint8_t)ra;
        smk_key_rb[k] = (uint8_t)rb;
        if (smk_key_ca[k] + 1u >= SMK_DEB_FRAMES)
            smk_key_da[k] = (uint8_t)ra;
        if (smk_key_cb[k] + 1u >= SMK_DEB_FRAMES)
            smk_key_dbb[k] = (uint8_t)rb;
        a = smk_key_da[k];
        b = smk_key_dbb[k];
        if (!smk_key_st[k]) {
            if (a) {
                smk_key_st[k] = 1;
                smk_key_t0[k] = (uint16_t)fm1_micros();
            }
        } else if (smk_key_st[k] == 1u && a && b) {
            uint32_t dt = (uint16_t)((uint16_t)fm1_micros() - smk_key_t0[k]);
            smk_key_st[k] = 2;
            smk_note_ch[k] = smk_ctrl ? 0u : (smk_part & 15u);
            smk_note_n[k] = (uint8_t)note;             /* release this pitch even if OCT changes first */
            smk_ev(0x90u | smk_note_ch[k], note, smk_vel(dt));
        } else if (!a && !b) {
            if (smk_key_st[k] == 2u)
                smk_ev(0x80u | smk_note_ch[k], smk_note_n[k], 0);
            smk_key_st[k] = 0;
        }
    }
}

static void smk_buttons(void)
{
    /* Bit number is the FM-1 matrix id PANEL_DEFAULT teaches the UI. */
    static const uint8_t cell[14] = {
        SMK_OCT_DOWN, SMK_OCT_UP, SMK_FX, SMK_SCALE, 0xFF, 0xFF, SMK_PARA, SMK_GLOBE,
        SMK_KNOB_BANK, SMK_PATCH, SMK_ARP, SMK_SEQ, SMK_SEQ_PLAY, SMK_SEQ_REC
    };
    static const uint8_t extra[7] = {
        SMK_LEFT, SMK_RIGHT, SMK_FADER_BANK, SMK_CHORD, SMK_NOTE_REPEAT, SMK_BT, 0xFF
    };
    uint32_t now = 0, x = 0, i;
    for (i = 0; i < 14u; i++)
        if (cell[i] != 0xFFu && smk_closed(cell[i]))
            now |= 1u << i;
    for (i = 0; i < 6u; i++)
        if (smk_closed(extra[i]))
            x |= 1u << i;
    if (now == smk_btn_raw) {
        smk_btn_edge |= now & ~smk_btn_lvl;
        smk_btn_lvl = now;
        fm1_in.buttons = now;
    }
    smk_btn_raw = now;
    if (x == smk_x_raw) {
        smk_x_edge |= x & ~smk_x_lvl;
        smk_x_lvl = x;
    }
    smk_x_raw = x;
}

/* PC9 is the back row's ADC input (channel 8) AND SPI1 group B's clock, so every LED frame drives it
 * as a clock for the length of that frame (fm1_lcd_hw.h hands it back as an analog input afterwards).
 * A back-row conversion that was already running, or a boot sample taken then, can therefore read the
 * clock line instead of the mux - and smk_pad_step's boot run freezes rest at the highest of its
 * samples, so one bad sample would leave the whole back row permanently down or permanently up.
 * lcd.c calls this after any frame that actually drove PC9, so the back row's boot run starts over
 * from clean samples instead of trusting one taken while the pin was not an input. */
static void smk_pads_pc9_used(void)
{
    uint32_t p;
    for (p = 8u; p < 16u; p++) {
        smk_pad_rest_seen &= (uint16_t)~(1u << p);
        smk_pad_rise[p] = 0;
        smk_pad_rest[p] = 0;
    }
}

/* P16: follow a pad's rest level, then answer "still down?". The level is taken at power-on as the
 * highest of the first SMK_PAD_BOOT samples (a single low glitch, or a mux that has not settled, is
 * not a rest), and after that it falls at most SMK_PAD_SLEW counts a pass and rises only after a
 * stretch of passes over it (SMK_PAD_RISE, ~0.5 s at the 8-frame mux rotation), which a strike or a
 * hover never lasts and a shim's drift does. Both limits matter: the level is frozen while the pad
 * reads down, so one sample that fell to 0 on a pad loading at 90 would leave the rest at 0 and the
 * pad pressed for ever. A sample that timed out (-1) says nothing either way and keeps the state.
 * At a rest of 0 the thresholds are the old 72 / 32 exactly. */
static int smk_pad_step(uint32_t pad, int raw)
{
    uint8_t bit = (uint8_t)(1u << (pad & 15u));
    uint32_t rest = smk_pad_rest[pad & 15u];
    int down = smk_pad_down[pad & 15u] != 0;
    if (raw < 0)
        return down;
    if (!(smk_pad_rest_seen & bit)) {          /* still booting: the highest sample of the run is the rest */
        uint8_t n = smk_pad_rise[pad & 15u];
        if ((uint32_t)raw > rest)
            rest = (uint32_t)raw > SMK_PAD_REST_MAX ? SMK_PAD_REST_MAX : (uint32_t)raw;
        smk_pad_rest[pad & 15u] = (uint8_t)rest;
        if (++n >= SMK_PAD_BOOT) {
            smk_pad_rest_seen |= bit;
            smk_pad_rise[pad & 15u] = 0;
        } else
            smk_pad_rise[pad & 15u] = n;
        return 0;                              /* the boot samples are where the pad rests, never a press */
    }
    if (!down) {
        uint8_t rise = smk_pad_rise[pad & 15u];
        if ((uint32_t)raw <= rest) {
            uint32_t gap = rest - (uint32_t)raw;
            rest = gap > SMK_PAD_SLEW ? rest - SMK_PAD_SLEW : (uint32_t)raw;
            rise = 0;
        } else if (rise >= SMK_PAD_RISE) {
            rest = (uint32_t)raw > SMK_PAD_REST_MAX ? SMK_PAD_REST_MAX : (uint32_t)raw;
            rise = 0;
        } else {
            rise = (uint8_t)(rise + 1u);
        }
        smk_pad_rest[pad & 15u] = (uint8_t)rest;
        smk_pad_rise[pad & 15u] = rise;
    }
    return (uint32_t)raw > rest + (down ? SMK_PAD_OFF : SMK_PAD_ON);
}

/* One analog sample arrives (see the 24-slot pipeline in fm1_input_tick): slot 0 of each mux address
 * is the front-row pad, slot 1 the back-row pad, slot 2 the fader / wheel / pedal on mux C. */
static void smk_analog(uint32_t addr, uint32_t slot, int raw)
{
    if (slot == 0u) {                               /* mux A (PA8): pads 0..7, the top row */
        smk_pad_one(addr, raw);
    } else if (slot == 1u) {                        /* mux B (PC9): pads 8..15, the bottom row */
        smk_pad_one(addr + 8u, raw);
    } else {                                        /* mux C (PA10): faders, wheels, pedal */
        uint32_t addr_lo = addr;
        if (addr_lo < 8u && raw >= 0)
            smk_an[addr_lo] = (int16_t)raw;
        if (addr_lo == SMK_PITCH) {
            if (raw >= 0) smk_wheel[0] = (int16_t)raw;
            smk_pitch(raw);
        } else if (addr_lo == SMK_MOD) {
            if (raw >= 0) smk_wheel[1] = (int16_t)raw;
            smk_mod(raw);
        } else if (addr_lo == SMK_SUSTAIN) {
            if (raw >= 0) smk_wheel[2] = (int16_t)raw;
            smk_sustain(raw);
        }
    }
}

/* One pad sample: press / release edge, note on the pad's channel, and the shared aftertouch. */
static void smk_pad_one(uint32_t pad, int raw)
{
    uint32_t note = SMK_PAD_NOTE0 + pad, press;
    int down;
    press = raw < 0 ? 0u : (uint32_t)raw >> 3;
    if (press > 127u)
        press = 127u;
    down = smk_pad_step(pad, raw);
    smk_pad_p[pad] = down ? (uint8_t)press : 0;
    if (down && !smk_pad_down[pad]) {
        smk_pad_down[pad] = 1;
        smk_pad_edge |= 1u << pad;                  /* P4: the UI's page-aware pad map */
        if (smk_ctrl)                               /* synth mode: the pad IS its action, no note (P4) */
            smk_ev(0x90u | 9u, note, press < 8u ? 8u : press);
    } else if (!down && smk_pad_down[pad]) {
        smk_pad_down[pad] = 0;
        smk_pad_rel |= 1u << pad;                   /* P4 */
        if (smk_ctrl)
            smk_ev(0x80u | 9u, note, 0);
    }
    if (smk_ctrl) {                                 /* channel-10 pressure: the loudest held pad */
        uint32_t p, mx = 0;
        for (p = 0; p < SMK_NPADS; p++)
            if (smk_pad_down[p] && smk_pad_p[p] > mx)
                mx = smk_pad_p[p];
        if ((uint8_t)mx > smk_pad_at + 8u || smk_pad_at > (uint8_t)mx + 8u || (!mx && smk_pad_at)) {
            smk_pad_at = (uint8_t)mx;
            if (smk_qw - smk_qr < SMK_Q / 2u)
                smk_ev(0xD0u | 9u, mx, 0);
        }
    }
}

static void smk_encoders(void)
{
    uint32_t e;
    for (e = 0; e < SMK_NENC; e++) {
        uint32_t cur = ((uint32_t)smk_closed(SMK_ENC[e][0]) << 1) | (uint32_t)smk_closed(SMK_ENC[e][1]);
        int8_t *sub = (int8_t *)&fm1_in.enc_sub[e];
        uint32_t idx, prev;
        if (cur != fm1_in.enc_last[e]) {
            fm1_in.enc_last[e] = (uint8_t)cur;
            fm1_in.enc_still[e] = 0;
            continue;
        }
        if (fm1_in.enc_prev[e] == 0xFFu) {         /* first frame: the knob rests here */
            fm1_in.enc_prev[e] = (uint8_t)cur;
            fm1_in.enc_rest[e] = (uint8_t)cur;
        }
        if (fm1_in.enc_still[e] < 0xFFFFu && ++fm1_in.enc_still[e] == SMK_REST_FRAMES &&
            cur != fm1_in.enc_rest[e]) {
            /* parked a long time off the detent state (held at power-on): that is the detent.
             * Never a second state: a knob held mid-click taught the complement of the detent as
             * one more rest and every click then counted twice (#23); short mid-click pauses of a
             * slow turn taught the mid states and the knob went dead */
            fm1_in.enc_rest[e] = (uint8_t)cur;
            *sub = 0;
        }
        prev = fm1_in.enc_prev[e];
        if (cur == prev)
            continue;
        idx = (prev << 2) | cur;
        if ((SMK_CW >> idx) & 1u) {
            (*sub)++;
            fm1_in_stat.enc_moves[e]++;
        } else if ((SMK_CCW >> idx) & 1u) {
            (*sub)--;
            fm1_in_stat.enc_moves[e]++;
        } else {                                   /* two states in one sample: a fast turn, */
            fm1_in_stat.enc_lost[e]++;             /* the way it was going */
            if (*sub > 0)
                *sub = (int8_t)(*sub + 2);
            else if (*sub < 0)
                *sub = (int8_t)(*sub - 2);
        }
        fm1_in.enc_prev[e] = (uint8_t)cur;
        if (*sub > 100 || *sub < -100)
            *sub = 0;                              /* (never off the detent that long) */
        if (cur == fm1_in.enc_rest[e]) {          /* back on the detent: whole cycles, a lost transition */
            int32_t n = *sub < 0 ? -*sub : *sub;   /* or two forgiven (one click = 4 transitions) */
            n = n >= 2 ? (n + 2) / 4 : 0;
            fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + (*sub < 0 ? -n : n));
            *sub = 0;
        }
    }
}

static void fm1_input_init(void)
{
    uint32_t rows_a = (1u << 2) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 9);
    uint32_t rows_h = (1u << 6) | (1u << 8) | (1u << 9);
    uint32_t sr = SMK_OE | SMK_LAT | SMK_CLK | SMK_DO;
    uint32_t e;
    for (e = 0; e < 8u; e++)
        smk_an[e] = -1;
    FM1_PR(FM1_PA, FM1_DIE) |= rows_a | sr;
    FM1_PR(FM1_PA, FM1_PD) &= ~(rows_a | sr);
    FM1_PR(FM1_PA, FM1_PU) = (FM1_PR(FM1_PA, FM1_PU) | rows_a) & ~sr;
    FM1_PR(FM1_PA, FM1_DIR) = (FM1_PR(FM1_PA, FM1_DIR) | rows_a) & ~sr;
    FM1_PR(FM1_PA, FM1_OUT) &= ~sr;
    FM1_PR(FM1_PH, FM1_DIE) |= rows_h;
    FM1_PR(FM1_PH, FM1_PD) &= ~rows_h;
    FM1_PR(FM1_PH, FM1_PU) |= rows_h;
    FM1_PR(FM1_PH, FM1_DIR) |= rows_h;
    SMK_IOMAP1 = (SMK_IOMAP1 & ~0x10000u) | 0x20000u;
    SMK_SPI2_CON = 0x6020u;
    SMK_SPI2_BAUD = 5u;
    smk_sr(0xFFFFFFFFu);
    for (e = 0; e < SMK_NENC; e++)
        fm1_in.enc_prev[e] = 0xFF;
}

static void fm1_input_tick(void)
{
    static const uint8_t ADCH[3] = {SMK_ADC_PAD_A, SMK_ADC_PAD_B, SMK_ADC_FADER};
    uint32_t c = smk_col, next, ns;
    int raw;

    /* 1. the conversion started on the previous tick: it takes ~40 us, so it is done */
    if (smk_adc_slot != 0xFFu) {
        if (smk_adc_done(&raw))
            smk_analog(smk_adc_slot / 3u, smk_adc_slot % 3u, raw);
        smk_adc_slot = 0xFFu;
    }

    /* 2. the rows of the column latched on the previous tick (the reference port's ISR order) */
    fm1_in.raw[c] = smk_rows();

    /* 3. latch the word shifted on the previous tick, start the next column's shift */
    smk_latch();
    next = (c + 1u) & (SMK_NCOL - 1u);
    smk_col = (uint8_t)next;

    /* 4. this tick's analog context: the mux address latched a moment ago is the one settled now */
    smk_adc_start(ADCH[smk_slot % 3u]);
    smk_adc_slot = smk_slot;
    ns = smk_slot + 1u >= 24u ? 0u : smk_slot + 1u;
    smk_slot = (uint8_t)ns;

    if (next == 0u) {
        smk_buttons();
        smk_keys();
        smk_encoders();
        fm1_in.frames++;
    }
    /* 5. the next word. It carries the NEXT column's select and the NEXT slot's mux address, so both
     *    have settled by the time that column is latched and that slot's conversion starts - and the
     *    LED half of the word is part of it, so nothing clears it here (a plain 0xFFFFFFFF dropped the
     *    button LEDs).
     *
     *    The column select MUST be in the word: the 595 drives it active-low one-hot in the top half
     *    (cols 0..7 = bits 31..24, cols 8..15 = bits 23..16, stock table; smk_board.h's own id bytes
     *    put the stock's column + 1 in the high nibble, which is exactly this shift). Shifting a fixed
     *    0xFFFFFFC7 selects all 16 columns at once, so every row line reads the OR of all 16 columns:
     *    a column whose row line is shared with a pad row then masks the pad's own, which is why one
     *    pad row read as dead while its neighbour worked and why presses on it were lost. The mux
     *    address (bits 3..5) rides in the low half, next to the button LEDs. */
    smk_sr_start((SMK_COLW(next) & 0xFFFFFFC7u) | ((ns / 3u) << 3));
    fm1_in_stat.ticks++;
}

static void fm1_led_dim_level(uint32_t lo) { (void)lo; }

static inline uint32_t fm1__lock(void)
{
#ifdef FM1_HOST_TIME
    return 0;                              /* the host has no interrupts */
#else
    __asm__ volatile("cli" ::: "memory");
    return 0;
#endif
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
#ifdef FM1_HOST_TIME
#else
    __asm__ volatile("csync\n\tsti" ::: "memory");
#endif
}

static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = e < SMK_NENC ? fm1_in.enc_steps[e] : 0;
    if (e < SMK_NENC)
        fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t e = smk_btn_edge;
    smk_btn_edge = 0;
    fm1__unlock(k);
    if (released)
        *released = 0;
    return e;
}

static uint32_t smk_extra_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t e = smk_x_edge;
    smk_x_edge = 0;
    fm1__unlock(k);
    return e;
}

static uint32_t smk_extra_down(void)
{
    return smk_x_lvl;
}

static uint32_t fm1_input_note_edges(void) { return 0; }

/* P4: pad press / release edges since the last read (bit per pad), for the UI's pad map. */
static uint32_t smk_pad_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t e = smk_pad_edge;
    smk_pad_edge = 0;
    if (released) {
        *released = smk_pad_rel;
        smk_pad_rel = 0;
    }
    fm1__unlock(k);
    return e;
}

static uint32_t smk_pad_down_mask(void)
{
    uint32_t p, m = 0;
    for (p = 0; p < SMK_NPADS; p++)
        m |= (uint32_t)smk_pad_down[p] << p;
    return m;
}
