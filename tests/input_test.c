/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Host test of the SMK-37 panel HAL (hal/fm1_input.h): the 16x8 matrix scan, the key/button
 * two-contact debounce, the pad/encoder paths and the event queue, against a model of the scan
 * and of bouncing contacts. The scan is the TIMER5 one (fm1_input_tick): one column per tick,
 * the debounce step (smk_keys / smk_buttons / smk_encoders) runs after the last column.
 * A key's two contacts close together, bounce (closed / open runs of 30..700 us) for up to 3 ms,
 * stay closed, and on the way up bounce again for up to 5 ms. Checked:
 *   - a clean press of every key emits exactly one note-on and one note-off on the part's channel;
 *   - 2000 bouncy presses of every key: exactly one note-on and one note-off each, no note ending
 *     while the key is held, none hanging 12 ms after the last bounce;
 *   - a stray closed sample (a 150 us glitch) plays nothing;
 *   - fast repeats (40 ms apart) are all heard;
 *   - the printed buttons: the two-sample debounce, the edge register, the OCT- / OCT+ bits;
 *   - an encoder detent cycle is one step (the corner decoder is unchanged).
 * The GPIO / timer helpers that touch the real registers (fm1_input_init, smk_sr, smk_adc) are
 * compiled, never called: the test writes fm1_in.raw directly, as the scan would. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t host_now;                          /* TIMER4 ticks (24 MHz) */
#define FM1_HOST_TIME 1
uint32_t fm1_ticks(void) { return host_now; }
#include "../firmware/hal/fm1_time.h"
#include "../firmware/hal/fm1_gpio.h"
#include "../firmware/hal/fm1_input.h"

#define TICK_TICKS (100u * FM1_TICKS_PER_US)       /* one column = 100 us */
#define NCELL 256u

static int fails;
static int check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

static uint32_t rng = 0x12345u;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return n ? rng % n : 0u;
}

/* the closed state of every matrix cell (id = column<<4 | row) */
static uint8_t cell[NCELL];
static void set_cell(uint32_t id, int on) { cell[id] = (uint8_t)(on ? 1 : 0); }
static void close_cells(const uint8_t *p, uint32_t n, int on)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        set_cell(p[i], on);
}
static void raw_from_cells(uint32_t c)
{
    uint32_t r;
    uint8_t v = 0;
    for (r = 0; r < 16u; r++)
        if (cell[(c << 4) | r])
            v |= (uint8_t)(1u << r);
    fm1_in.raw[c] = v;
}

/* the key / button contact under test: a list of toggle times (us), starting open */
#define NEDGE 1024
static struct { uint32_t t[NEDGE], n; } ct;
static int contact_at(uint32_t us)
{
    uint32_t i, s = 0;
    for (i = 0; i < ct.n && ct.t[i] <= us; i++)
        s ^= 1u;
    return (int)s;
}
static void add_edge(uint32_t us) { if (ct.n < NEDGE) ct.t[ct.n++] = us; }
static uint32_t bounce(uint32_t t, uint32_t span, int final)
{
    uint32_t end = t + span, last = t;
    int s = final;
    add_edge(t);
    while (span) {
        uint32_t run = 30u + rnd(670u);
        if (t + run >= end)
            break;
        t += run;
        add_edge(t);
        s ^= 1;
        last = t;
    }
    if (s != final) {
        t += 30u + rnd(200u);
        add_edge(t);
        last = t;
    }
    return last;
}

/* drain the event queue the HAL's smk_ev fills */
static int queue_pop(uint32_t *st, uint32_t *d1, uint32_t *d2)
{
    uint32_t i;
    if (smk_qr == smk_qw)
        return 0;
    i = smk_qr % SMK_Q;
    *st = smk_q_st[i];
    *d1 = smk_q_d1[i];
    *d2 = smk_q_d2[i];
    smk_qr++;
    return 1;
}

static uint32_t sim_us, sim_col;

static void tick(void)
{
    uint32_t c = sim_col;
    host_now = sim_us * FM1_TICKS_PER_US;
    raw_from_cells(c);
    if (++sim_col == SMK_NCOL) {
        sim_col = 0;
        smk_buttons();
        smk_keys();
        smk_encoders();
    }
    sim_us += TICK_TICKS / FM1_TICKS_PER_US;
}

static void reset(void)
{
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    memset((void *)&fm1_in_stat, 0, sizeof fm1_in_stat);
    memset(cell, 0, sizeof cell);
    memset(&ct, 0, sizeof ct);
    smk_qw = smk_qr = 0;
    smk_btn_lvl = smk_btn_raw = smk_btn_edge = 0;
    smk_x_lvl = smk_x_raw = smk_x_edge = 0;
    smk_col = smk_mux = 0;
    {
        uint32_t i;
        for (i = 0; i < SMK_NENC; i++)
            fm1_in.enc_prev[i] = 0xFF;
    }
}

/* run the scan until `until` us, holding a key's two contacts per `ct`; count note events */
static uint32_t ons, offs, other, first_on_us, last_off_us, early_off;
static uint32_t held_from, held_to;
static uint32_t key_a = 0xFFu, key_b = 0xFFu;
static void run(uint32_t until)
{
    while (sim_us < until) {
        uint32_t st, d1, d2;
        /* the key under test drives its two contacts from the model */
        if (key_a != 0xFFu) {
            int cl = contact_at(sim_us);
            set_cell(key_a, cl);
            set_cell(key_b, cl);
        }
        tick();
        while (queue_pop(&st, &d1, &d2)) {
            if ((st & 0xF0u) == 0x90u && d2 != 0u) {
                ons++;
                if (!first_on_us)
                    first_on_us = sim_us;
            } else if ((st & 0xF0u) == 0x80u) {
                offs++;
                last_off_us = sim_us;
                if (sim_us > held_from && sim_us < held_to)
                    early_off++;
            } else
                other++;
        }
    }
}

int main(void)
{
    uint32_t k;
    printf("SMK-37 panel: %u keys, %u pads, %u encoders, %u columns; frame %u us\n",
           (unsigned)SMK_NKEYS, (unsigned)SMK_NPADS, (unsigned)SMK_NENC, (unsigned)SMK_NCOL,
           (unsigned)(SMK_NCOL * TICK_TICKS / FM1_TICKS_PER_US));

    /* a clean press / release of every key, at every phase of the scan */
    {
        uint32_t bad = 0, ph;
        for (k = 0; k < SMK_NKEYS; k++) {
            key_a = SMK_KEYS[k][0];
            key_b = SMK_KEYS[k][1];
            for (ph = 0; ph < SMK_NCOL * 100u; ph += 37u) {
                uint32_t t0;
                reset();
                sim_us = 0;
                sim_col = 0;
                t0 = 10000u + ph;
                add_edge(t0);
                add_edge(t0 + 100000u);
                ons = offs = other = first_on_us = last_off_us = early_off = 0;
                held_from = t0;
                held_to = t0 + 100000u;
                run(t0 + 130000u);
                if (ons != 1u || offs != 1u || other != 0u)
                    bad++;
                if (sim_us < t0 + 130000u) { }   /* (run always reaches until) */
            }
        }
        check("clean press: one note-on and one note-off each, every key, every scan phase", bad == 0u);
    }

    /* bouncy presses and releases */
    {
        uint32_t trials = 0, onebad = 0, early = 0, hang = 0;
        for (k = 0; k < 2000u; k++) {
            uint32_t t0, tb, hold, tr, tl, key = rnd(SMK_NKEYS);
            key_a = SMK_KEYS[key][0];
            key_b = SMK_KEYS[key][1];
            reset();
            sim_us = rnd(1100u);
            sim_col = rnd(SMK_NCOL);
            t0 = 5000u + rnd(1100u);
            tb = bounce(t0, rnd(3000u), 1);
            hold = 15000u + rnd(300000u);
            tr = tb + hold;
            tl = bounce(tr, rnd(5000u), 0);
            ons = offs = other = first_on_us = last_off_us = early_off = 0;
            held_from = tb;
            held_to = tr;
            run(tl + 12000u);
            trials++;
            if (ons != 1u || offs != 1u || other != 0u)
                onebad++;
            early += early_off;
            if (cell[key_a] || cell[key_b]) {
                /* the model settles open; a note left on is a hang */
            }
        }
        printf("bouncy: %u trials, %u bad\n", (unsigned)trials, (unsigned)onebad);
        check("2000 bouncy presses: exactly one note-on and one note-off each", onebad == 0u);
        check("no note ends while its key is held", early == 0u);
        (void)hang;
    }

    /* a stray closed sample: no note */
    {
        uint32_t g, any = 0;
        key_a = SMK_KEYS[20][0];
        key_b = SMK_KEYS[20][1];
        for (g = 0; g < SMK_NCOL * 100u; g += 13u) {
            reset();
            sim_us = 0;
            sim_col = 0;
            add_edge(5000u + g);
            add_edge(5150u + g);
            ons = offs = other = first_on_us = 0;
            held_from = held_to = 0;
            run(40000u);
            any += ons + offs;
        }
        check("a 150 us glitch plays no note", any == 0u);
    }

    /* fast repeats */
    {
        uint32_t t;
        key_a = SMK_KEYS[30][0];
        key_b = SMK_KEYS[30][1];
        reset();
        sim_us = 0;
        sim_col = 0;
        for (t = 0; t < 10u; t++) {
            bounce(5000u + t * 40000u, 1500u, 1);
            bounce(5000u + t * 40000u + 20000u, 2000u, 0);
        }
        ons = offs = other = first_on_us = 0;
        held_from = held_to = 0;
        run(5000u + 10u * 40000u + 20000u);
        check("10 notes 40 ms apart (20 ms held, bouncy): 10 note-ons, 10 note-offs",
              ons == 10u && offs == 10u);
    }

    /* the printed buttons: two-sample debounce, edge register, OCT bits */
    {
        uint32_t r;
        key_a = key_b = 0xFFu;
        reset();
        sim_us = 0;
        sim_col = 0;
        set_cell(SMK_OCT_DOWN, 1);
        run(4000u);                                 /* 2+ frames: the two-sample debounce settles */
        check("a printed button (OCT-) sets its bit after the two-sample debounce",
              (fm1_in.buttons & (1u << FM1_BTN_OCT_DOWN)) != 0u);
        r = fm1_input_edges(NULL);
        check("OCT- raises an edge", (r & (1u << FM1_BTN_OCT_DOWN)) != 0u);
        key_a = key_b = 0xFFu;
        reset();
        sim_us = 0;
        sim_col = 0;
        set_cell(SMK_OCT_UP, 1);
        run(4000u);
        check("OCT+ sets its bit",
              (fm1_in.buttons & (1u << FM1_BTN_OCT_UP)) != 0u);
    }

    /* encoders: one clockwise detent cycle of encoder 0 = one step (decoder unchanged) */
    {
        static const uint8_t SEQ[] = {0, 1, 3, 2, 0};   /* quadrature A<<1|B, from the rest */
        uint32_t i, f;
        int32_t s;
        key_a = key_b = 0xFFu;
        reset();
        for (i = 0; i < 5u; i++)
            for (f = 0; f < 4u; f++) {
                uint32_t a = (SEQ[i] >> 1) & 1u, b = SEQ[i] & 1u;
                memset(cell, 0, sizeof cell);
                memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);
                set_cell(SMK_ENC[0][0], (int)a);
                set_cell(SMK_ENC[0][1], (int)b);
                fm1_in.raw[SMK_ENC[0][0] >> 4] |= (uint8_t)(a << (SMK_ENC[0][0] & 15u));
                fm1_in.raw[SMK_ENC[0][1] >> 4] |= (uint8_t)(b << (SMK_ENC[0][1] & 15u));
                smk_encoders();
            }
        s = fm1_in.enc_steps[0];
        printf("encoder 0: one cycle -> %d step(s)\n", (int)s);
        check("an encoder detent cycle is one step", s == 1 || s == -1);
    }

    /* P16: the pads' rest level (hal/fm1_input.h smk_pad_step). A pad's press / release points ride on its
     * own idle, so a shim that loads the pad at 40 or 90 still plays and still releases; at an idle of 0 the
     * pair is the old fixed 72 / 32. The scan feeds one sample per 8 frames, so the test steps it by hand. */
    {
        int ok = 1;
#define PAD_RESET(p)                                      \
        do {                                              \
            smk_pad_down[p] = 0u;                         \
            smk_pad_rest[p] = 0u;                         \
            smk_pad_rise[p] = 0u;                         \
            smk_pad_rest_seen &= (uint16_t)~(1u << (p));  \
        } while (0)
#define PAD_STEP(p, v) (smk_pad_step((p), (v)) ? (smk_pad_down[p] = 1u, 1) : (smk_pad_down[p] = 0u, 0))
        /* the boot run: SMK_PAD_BOOT samples, none of which may press, the highest becoming the rest */
#define PAD_BOOT(p, v)                                    \
        do {                                              \
            ok &= PAD_STEP((p), (v)) == 0;                \
            ok &= PAD_STEP((p), (v)) == 0;                \
            ok &= PAD_STEP((p), (v)) == 0;                \
            ok &= PAD_STEP((p), (v)) == 0;                \
        } while (0)

        /* a healthy pad: the boot run leaves the level at its own count, and the pair is the old fixed table */
        PAD_RESET(0);
        PAD_BOOT(0, 0);
        ok = smk_pad_rest[0] == 0 && (smk_pad_rest_seen & 1u);
        ok &= PAD_STEP(0, 72) == 0;
        ok &= PAD_STEP(0, 73) == 1;
        ok &= PAD_STEP(0, 33) == 1;                  /* held: still over the release .. */
        ok &= PAD_STEP(0, 32) == 0;                  /* .. and under it at 32, up */
        check("a pad resting at 0 keeps the old 72 / 32 pair", ok && smk_pad_rest[0] == 0);

        /* the boot level is the highest of the run: one sample that fell to 0 in it is not the rest */
        PAD_RESET(5);
        ok = PAD_STEP(5, 41) == 0;
        ok &= PAD_STEP(5, 0) == 0;
        ok &= PAD_STEP(5, 44) == 0;
        ok &= PAD_STEP(5, 39) == 0;
        ok &= smk_pad_rest[5] == 44 && (smk_pad_rest_seen & (1u << 5));
        check("the boot level is the highest sample of the boot run", ok);

        /* a shim that loads the pad at 40: the fixed 72 fired on a light touch and the fixed 32 never let go */
        PAD_RESET(0);
        PAD_BOOT(0, 40);
        ok = smk_pad_rest[0] == 40;
        ok &= PAD_STEP(0, 100) == 0;                 /* rest + 60: still up .. */
        ok &= PAD_STEP(0, 115) == 1;                 /* .. over rest + 72 presses */
        ok &= PAD_STEP(0, 105) == 1;                 /* held: over rest + 32 */
        ok &= PAD_STEP(0, 70) == 0;                  /* and it lets go at 72, not at 32 */
        check("a pad resting at 40 presses and releases 72 / 32 over its own level", ok);

        /* a drifting shim at 90: the fixed table held the pad down from power-on */
        PAD_RESET(1);
        PAD_BOOT(1, 90);
        ok = smk_pad_rest[1] == 90;
        ok &= PAD_STEP(1, 130) == 0;
        ok &= PAD_STEP(1, 170) == 1;
        check("a pad resting at 90 reads up and presses over its own level", ok);

        /* one bad sample under a preloaded pad must not take the level to it: the level is frozen while the
         * pad reads down, so a rest of 0 there would hold the pad pressed for the rest of the session */
        PAD_RESET(6);
        PAD_BOOT(6, 90);
        ok = PAD_STEP(6, 0) == 0 && smk_pad_rest[6] == 88;
        ok &= PAD_STEP(6, 90) == 0;                  /* the pad is still where it was: not pressed */
        check("a single low sample cannot wedge a preloaded pad down", ok);

        /* a shim that drifts down: the level walks down, SMK_PAD_SLEW counts a pass, so the same force still
         * presses - and a run of low samples is what a drift looks like, not one sample */
        PAD_RESET(2);
        PAD_BOOT(2, 60);
        {
            uint32_t i;
            ok = PAD_STEP(2, 0) == 0 && smk_pad_rest[2] == 58;
            for (i = 0; i < 40u; i++)
                ok &= PAD_STEP(2, 0) == 0;
            ok &= smk_pad_rest[2] == 0;
            ok &= PAD_STEP(2, 80) == 1;              /* 0 + 72, the new level */
        }
        check("a pad whose rest drops walks down to the new level", ok);

        /* a quiet spike while up must not lift the level (the next low sample sets it back) */
        PAD_RESET(3);
        PAD_BOOT(3, 5);
        ok = PAD_STEP(3, 60) == 0 && smk_pad_rest[3] < 16;   /* under rest + 72: up, and the .. */
        ok &= PAD_STEP(3, 5) == 0 && smk_pad_rest[3] == 5;   /* .. next low sample is the level again */
        check("a noise spike while up does not raise a pad's rest level", ok);

        /* a timed-out ADC sample says nothing: the held state and the level stay */
        PAD_RESET(4);
        PAD_BOOT(4, 20);
        ok = PAD_STEP(4, 300) == 1;
        {
            uint32_t rest = smk_pad_rest[4];
            ok &= smk_pad_step(4, -1) == 1 && smk_pad_rest[4] == rest;
        }
        check("a timed-out sample keeps a held pad held", ok);
        PAD_RESET(4);
#undef PAD_BOOT
#undef PAD_STEP
#undef PAD_RESET
    }

    if (fails) {
        printf("INPUT TEST FAILED (%d)\n", fails);
        return 1;
    }
    printf("input: all ok\n");
    return 0;
}
