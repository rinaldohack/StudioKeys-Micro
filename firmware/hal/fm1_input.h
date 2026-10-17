/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
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
static uint8_t smk_pad_down[SMK_NPADS];
static uint8_t smk_pad_p[SMK_NPADS];
static uint8_t smk_pad_at;
static uint32_t smk_pad_edge, smk_pad_rel;   /* P4: bit per pad, set on a press / release since last read */
static uint8_t smk_col, smk_mux;
static uint32_t smk_sr_buf __attribute__((aligned(4)));
static volatile int8_t smk_oct;
static volatile uint8_t smk_part;          /* selected part: the keys, wheels and pedal follow it */
static uint8_t smk_ctrl;                   /* 1: panel is a generic MIDI controller, engine stays quiet */
static uint8_t smk_note_ch[SMK_NKEYS];
static uint8_t smk_note_n[SMK_NKEYS];   /* the pitch sent at note-on; octave can move before the key lifts */
static uint8_t smk_defer_pads;          /* 1: audio is rendering; the slow pad/ADC scan waits */
static volatile int16_t smk_an[8];
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

static uint32_t smk_colbit(uint32_t c) { return c < 8u ? 24u + c : 16u + (c - 8u); }

static uint8_t smk_rows(void)
{
    uint32_t pa = FM1_PR(FM1_PA, FM1_IN), ph = FM1_PR(FM1_PH, FM1_IN);
    uint32_t v = ((pa >> 2) & 1u) | ((pa >> 4) & 0xEu) | ((pa >> 5) & 0x10u) | ((ph >> 1) & 0x20u) |
                 ((ph >> 2) & 0xC0u);
    return (uint8_t)((~v) & 0xFFu);
}

static int smk_closed(uint32_t id) { return (fm1_in.raw[id >> 4] >> (id & 0xFu)) & 1; }

static int smk_adc(uint32_t ch)
{
    uint32_t t, v;
    SMK_ADC_CON = 0;
    if (SMK_WLA_CON0 & (1u << 14))
        SMK_WLA_CON0 &= ~(1u << 14);
    SMK_ADC_CON = 0xF04Eu | ((ch & 0xFu) << 8);
    SMK_ADC_CON |= 0x10u;
    SMK_ADC_CON |= 0x40u;
    for (t = 0; t < 20000u; t++)
        if (SMK_ADC_CON & 0x80u)
            break;
    v = SMK_ADC_RES & 0x3FFu;
    SMK_ADC_CON = 0x40u;
    SMK_ADC_CON = 0;
    return t == 20000u ? -1 : (int)v;
}

static uint32_t smk_vel(uint32_t dt_us)
{
    if (dt_us < 1500u)
        return 127u;
    if (dt_us > 28000u)
        return 8u;
    return 127u - (dt_us - 1500u) * 119u / 26500u;
}

static void smk_pitch(int raw)
{
    static int16_t sent = 0x7FFF;
    static uint8_t ch = 0xFF;
    int32_t bend, u, d;
    uint32_t part = smk_ctrl ? 0u : (smk_part & 15u);
    if (raw < 0)
        return;
    d = raw - (int32_t)SMK_PITCH_MID;
    /* The wheel's travel is about ±120 counts. << 6 fills the 14-bit bend
     * without a divide (a folded divisor looks like a ROM address). */
    bend = d > -12 && d < 12 ? 0 : d << 6;
    if (bend < -8192)
        bend = -8192;
    if (bend > 8191)
        bend = 8191;
    if (ch == part && sent != 0x7FFF && (bend > sent ? bend - sent : sent - bend) < 32)
        return;
    ch = (uint8_t)part;
    sent = (int16_t)bend;
    u = bend + 8192;
    smk_ev(0xE0u | part, (uint32_t)u & 0x7Fu, ((uint32_t)u >> 7) & 0x7Fu);
}

static void smk_mod(int raw)
{
    static int16_t sent = -1;
    static uint8_t ch = 0xFF;
    int32_t v;
    uint32_t part = smk_ctrl ? 0u : (smk_part & 15u);
    if (raw < 0)
        return;
    v = ((int32_t)SMK_MOD_ZERO - raw) >> 1;   /* wheel down 0, wheel up just under 127 */
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

static void smk_pads(void)
{
    uint32_t i = smk_mux, addr = SMK_MUX_OF[i], p, mx = 0, ch;
    int raws[2], fader;
    smk_sr(0xFFFFFFC7u | (addr << 3));
    fm1_delay_us(150);
    raws[0] = smk_adc(SMK_ADC_PAD_A);
    raws[1] = smk_adc(SMK_ADC_PAD_B);
    fader = smk_adc(SMK_ADC_FADER);
    smk_sr(0xFFFFFFFFu);
    if (addr < 8u && fader >= 0)
        smk_an[addr] = (int16_t)fader;
    if (addr == SMK_PITCH)
        smk_pitch(fader);
    else if (addr == SMK_MOD)
        smk_mod(fader);
    else if (addr == SMK_SUSTAIN)
        smk_sustain(fader);
    for (ch = 0; ch < 2u; ch++) {
        uint32_t pad = i + ch * 8u;
        uint32_t note = SMK_PAD_NOTE0 + pad;
        int raw = raws[ch];
        uint32_t press;
        int down;
        press = raw < 0 ? 0u : (uint32_t)raw >> 3;
        if (press > 127u)
            press = 127u;
        down = smk_pad_down[pad] ? raw > 32 : raw > 72;
        smk_pad_p[pad] = down ? (uint8_t)press : 0;
        if (down && !smk_pad_down[pad]) {
            smk_pad_down[pad] = 1;
            smk_pad_edge |= 1u << pad;                  /* P4: the UI's page-aware pad map */
            smk_ev(0x90u | (smk_ctrl ? 9u : SMK_PAD_CH), note, press < 8u ? 8u : press);
        } else if (!down && smk_pad_down[pad]) {
            smk_pad_down[pad] = 0;
            smk_pad_rel |= 1u << pad;                   /* P4 */
            smk_ev(0x80u | (smk_ctrl ? 9u : SMK_PAD_CH), note, 0);
        }
    }
    for (p = 0; p < SMK_NPADS; p++)
        if (smk_pad_down[p] && smk_pad_p[p] > mx)
            mx = smk_pad_p[p];
    if ((uint8_t)mx > smk_pad_at + 8u || smk_pad_at > (uint8_t)mx + 8u || (!mx && smk_pad_at)) {
        smk_pad_at = (uint8_t)mx;
        if (smk_qw - smk_qr < SMK_Q / 2u)
            smk_ev(0xD0u | (smk_ctrl ? 9u : SMK_PAD_CH), mx, 0);
    }
    if (++smk_mux == 8u)
        smk_mux = 0;
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
    uint32_t c = smk_col;
    smk_sr(0xFFFFFFFFu ^ (1u << smk_colbit(c)));
    fm1_delay_us(12);
    fm1_in.raw[c] = smk_rows();
    if (++smk_col == SMK_NCOL) {
        smk_col = 0;
        smk_sr(0xFFFFFFFFu);
        smk_buttons();
        smk_keys();
        smk_encoders();
        if (!smk_defer_pads)                           /* the pad mux waits 150 us and reads the ADC */
            smk_pads();
        fm1_in.frames++;
    }
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
