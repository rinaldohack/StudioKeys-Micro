/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
 * P17: the SCL / CHORD / ARP pages' tailored pad maps (P5b), their direct picks and LED colours. */
#define UI_TEST_NO_MAIN 1
#define SK_HOST_PADS 1              /* P5e: the ramp test needs the companion's press edges */
#include "ui_test.c"

/* the map a page shows: its first pads' actions, the page's own picks on 5..8, FX4, and the picks after */
static int scl_page(void)
{
    int bad = 0;
    ui_power_on(); go_page(GR_SCALE); track_t *t = &trk[0];
    bad += check("P17 SCL: pads 1..4 cycle ROOT / SCL / QNT / TRN",
                 pad_get(0)->act == PAD_PARAM0 && pad_get(0)->param == P_ROOT &&
                 pad_get(1)->param == P_SCALE && pad_get(2)->param == P_QUANT && pad_get(3)->param == P_TRANS);
    bad += check("P17 SCL: pads 5..8 pick the key lock OFF SNAP WHITE SEQ",
                 pad_get(4)->act == PAD_QNT && pad_get(4)->param == 0 && pad_get(5)->param == 1 &&
                 pad_get(6)->param == 2 && pad_get(7)->param == 3);
    bad += check("P17 SCL: the shared FX4 and the octave pair are kept",
                 pad_get(8)->act == PAD_FX_DIST && pad_get(11)->act == PAD_FX_REV &&
                 pad_get(12)->act == PAD_OCTDN && pad_get(13)->act == PAD_OCTUP);
    t->p[P_QUANT] = 0;
    pad_do(pad_get(6));
    bad += check("P17 SCL: WHITE picks key lock 2 and says so", t->p[P_QUANT] == 2 && msg_is("WHITE"));
    bad += check("P17 SCL: the picked lock is lit, the others dim", pad_colour(pad_get(6)) == PC_GRN &&
                 pad_colour(pad_get(5)) == PC_DIM && pad_colour(pad_get(7)) == PC_DIM);
    pad_do(pad_get(4));
    bad += check("P17 SCL: OFF puts the key lock back", t->p[P_QUANT] == 0 && msg_is("OFF"));
    t->p[P_ROOT] = 0;
    pad_do(pad_get(0));
    bad += check("P17 SCL: pad 1 cycles the root", t->p[P_ROOT] == 1 && msg_is("ROOT NOTE"));
    return bad;
}
static int chord_page(void)
{
    int bad = 0;
    ui_power_on(); go_page(GR_CHORD); track_t *t = &trk[0];
    bad += check("P17 CHORD: pads 1..2 cycle the type and the voicing",
                 pad_get(0)->act == PAD_PARAM0 && pad_get(0)->param == P_CHRD &&
                 pad_get(1)->act == PAD_PARAM0 && pad_get(1)->param == P_VOIC);
    bad += check("P17 CHORD: pads 3..4 pick OFF and the in-key triads",
                 pad_get(2)->act == PAD_CHRD && pad_get(2)->param == 0 &&
                 pad_get(3)->act == PAD_CHRD && pad_get(3)->param == 1);
    bad += check("P17 CHORD: pads 5..8 pick CLOSE OPEN INV1 INV2",
                 pad_get(4)->act == PAD_VOIC && pad_get(4)->param == 0 && pad_get(5)->param == 1 &&
                 pad_get(6)->param == 2 && pad_get(7)->param == 3);
    bad += check("P17 CHORD: pad 15 is +OCT, pad 16 PANIC",
                 pad_get(14)->act == PAD_VOIC && pad_get(14)->param == 4 && pad_get(15)->act == PAD_PANIC);
    t->p[P_CHRD] = 5;
    pad_do(pad_get(3));
    bad += check("P17 CHORD: DIA3 picks the chord type and names it", t->p[P_CHRD] == 1 && msg_is("DIA3"));
    bad += check("P17 CHORD: the picked type is lit, the neighbouring pick dim", pad_colour(pad_get(3)) == PC_GRN &&
                 pad_colour(pad_get(2)) == PC_DIM);
    t->p[P_VOIC] = 0;
    pad_do(pad_get(7));
    bad += check("P17 CHORD: INV2 picks the voicing", t->p[P_VOIC] == 3 && msg_is("INV2") &&
                 pad_colour(pad_get(7)) == PC_GRN && pad_colour(pad_get(6)) == PC_DIM);
    pad_do(pad_get(14));
    bad += check("P17 CHORD: +OCT is one tap away", t->p[P_VOIC] == 4 && msg_is("+OCT"));
    pad_do(pad_get(2));
    bad += check("P17 CHORD: OFF disables the chord keys", t->p[P_CHRD] == 0 && msg_is("OFF"));
    return bad;
}
static int arp_page(void)
{
    int bad = 0;
    ui_power_on(); go_page(GR_ARP); track_t *t = &trk[0];
    bad += check("P17 ARP: pads 1..4 cycle MODE RATE OCT GATE",
                 pad_get(0)->act == PAD_PARAM0 && pad_get(0)->param == P_AMODE &&
                 pad_get(1)->param == P_ARATE && pad_get(2)->param == P_AOCT && pad_get(3)->param == P_AGATE);
    bad += check("P17 ARP: pads 5..8 pick UP DN UPDN RND",
                 pad_get(4)->act == PAD_ARPMODE && pad_get(4)->param == 1 && pad_get(5)->param == 2 &&
                 pad_get(6)->param == 3 && pad_get(7)->param == 4);
    bad += check("P17 ARP: pads 13..15 are OFF, RPT and HOLD, 16 PANIC",
                 pad_get(12)->act == PAD_ARPMODE && pad_get(12)->param == 0 &&
                 pad_get(13)->act == PAD_ARPMODE && pad_get(13)->param == 6 &&
                 pad_get(14)->act == PAD_TOGGLE && pad_get(14)->param == P_AHOLD && pad_get(15)->act == PAD_PANIC);
    t->p[P_AMODE] = 0;
    pad_do(pad_get(4));
    bad += check("P17 ARP: UP starts the arp", t->p[P_AMODE] == 1 && msg_is("UP") &&
                 pad_colour(pad_get(4)) == PC_GRN && pad_colour(pad_get(5)) == PC_DIM);
    pad_do(pad_get(6));
    bad += check("P17 ARP: UPDN picks the up-down order", t->p[P_AMODE] == 3 && msg_is("UPDN"));
    pad_do(pad_get(7));
    bad += check("P17 ARP: RND picks the random order", t->p[P_AMODE] == 4 && msg_is("RND"));
    pad_do(pad_get(13));
    bad += check("P17 ARP: RPT is the note repeat", t->p[P_AMODE] == 6 && msg_is("REPEAT"));
    t->p[P_AHOLD] = 0;
    pad_do(pad_get(14));
    bad += check("P17 ARP: HOLD latches the chord (a switch reads green when on)",
                 t->p[P_AHOLD] == TP[P_AHOLD].max && pad_colour(pad_get(14)) == PC_GRN);
    pad_do(pad_get(14));
    bad += check("P17 ARP: HOLD again lets go", t->p[P_AHOLD] == TP[P_AHOLD].min);
    pad_do(pad_get(2));
    bad += check("P17 ARP: pad 3 walks the arp octave", t->p[P_AOCT] == TP[P_AOCT].def + 1);
    pad_do(pad_get(12));
    bad += check("P17 ARP: OFF stops it", t->p[P_AMODE] == 0 && msg_is("OFF"));
    return bad;
}
static int pad_capture(void)
{
    int bad = 0;
    ui_power_on(); go_page(GR_SCALE); track_t *t = &trk[0];
    song.sel = 0; t->p[P_DIST] = 0;
    song.rec = 1; seq_start(); seq_tick(t, CTL);
    pad_do(pad_get(8));                              /* the DIST toggle on the SCL page */
    bad += check("P17: a pad's parameter write is captured like the knob's", t->p[P_DIST] == 64 &&
                 motion_count(t) == 1u && motion.event[0].param == P_DIST && motion.event[0].value == 64);
    seq_stop(); song.rec = 0; motion_clear(t);
    return bad;
}
/* P5g: the Leslie pad's fill, sampled while the rotor really turns. One mix_block is one block of the
 * horn (voice.c calls e->block per part per block), so a sample a block follows the phase exactly, and
 * pad_rgb_value is the one value the chain and the screen both paint (ui_pads.c). */
static uint32_t leslie_lum(void)
{
    const uint32_t c = pad_rgb_value(4);
    return ((c >> 16) & 0xFFu) + ((c >> 8) & 0xFFu) + (c & 0xFFu);
}
static void leslie_run(uint32_t blocks)
{
    static int32_t o[2u * CTL];
    while (blocks--) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
    }
}
typedef struct { uint32_t lo, hi; } fill_t;
/* the darkest and the brightest fill over a window (no peak search: the fill is a triangle, its
 * extremes are what the LED actually reaches) */
static fill_t leslie_range(uint32_t blocks)
{
    fill_t r;
    uint32_t k, v;
    r.lo = ~0u;
    r.hi = 0u;
    for (k = 0; k < blocks; k++) {
        leslie_run(1u);
        v = leslie_lum();
        if (v < r.lo)
            r.lo = v;
        if (v > r.hi)
            r.hi = v;
    }
    return r;
}
/* the blink's period in blocks: the mean gap between the fill's upward crossings of its middle. The
 * fill is quantised (k is 44..128, in steps of 1), so the crossing needs hysteresis or every step
 * would read as a blink of its own - `mid` and `hyst` come from leslie_range of the same window. */
static uint32_t leslie_period(uint32_t blocks, uint32_t mid, uint32_t hyst)
{
    uint32_t k, v, n = 0, last = 0, sum = 0;
    int low = leslie_lum() < mid;
    for (k = 1u; k <= blocks; k++) {
        leslie_run(1u);
        v = leslie_lum();
        if (low) {
            if (v >= mid + hyst) {                     /* through the middle, going up */
                if (n++)
                    sum += k - last;
                last = k;
                low = 0;
            }
        } else if (v + hyst <= mid) {
            low = 1;
        }
    }
    return n > 1u ? sum / (n - 1u) : 0u;
}
/* blocks from a top of the fill to the next upward crossing of its middle: half a turn */
static uint32_t leslie_until_mid(uint32_t limit, uint32_t mid, uint32_t hyst)
{
    uint32_t k, v;
    int low = 0;
    for (k = 1u; k <= limit; k++) {
        leslie_run(1u);
        v = leslie_lum();
        if (!low) {
            if (v + hyst <= mid)
                low = 1;
        } else if (v >= mid + hyst) {
            return k;
        }
    }
    return 0u;
}
static int near_u(uint32_t a, uint32_t b, uint32_t pct)
{
    const uint32_t d = a > b ? a - b : b - a;
    return d <= b * pct / 100u;
}
static int wheel_page(void)
{
    int bad = 0;
    ui_power_on();
    track_t *t = &trk[0];
    t->eng_req = t->engine = ENGI_WHEEL;
    smk_kbank = 0;
    bad += check("P5 WHEEL: SOUND = 16' 5 1/3' 8' 4' 2 2/3' + REG DRV CHOR (5 knobs, not 8)",
                 smk_bank_param(0) == P_E0 && smk_bank_param(1) == P_E1 && smk_bank_param(2) == P_E2 &&
                 smk_bank_param(3) == P_E3 && smk_bank_param(4) == P_E4 &&
                 smk_bank_param(5) == P_VREG_REG && smk_bank_param(6) == P_DIST && smk_bank_param(7) == P_CHOR);
    smk_kbank = 1;
    bad += check("P5 WHEEL: MORE keeps PERC / CLICK / ROTR, the mix and the 1' bar (no dead knob)",
                 smk_bank_param(0) == P_E9 && smk_bank_param(1) == P_E10 && smk_bank_param(2) == P_E11 &&
                 smk_bank_param(3) == P_LEVEL && smk_bank_param(7) == P_E8);
    bad += check("P5f WHEEL: pads 1..4 are the TOP (DOWN) half, pads 9..12 the BOTTOM (UP) half",
                 pad_get(0)->act == PAD_DBAR && ((pad_get(0)->param >> 4) & 1u) == 0u &&
                 pad_get(8)->act == PAD_DBAR && ((pad_get(8)->param >> 4) & 1u) == 1u &&
                 (pad_get(0)->param & 15u) == 5 && (pad_get(1)->param & 15u) == 6 &&
                 (pad_get(2)->param & 15u) == 7 && (pad_get(3)->param & 15u) == 8 &&
                 (pad_get(8)->param & 15u) == 5 && (pad_get(11)->param & 15u) == 8);
    bad += check("P5 WHEEL: cols 5..8 = ROT PER DEC REV / DRV HRM CHR CLK",
                 pad_get(4)->act == PAD_LESLIE && pad_get(5)->act == PAD_PERC &&
                 pad_get(6)->act == PAD_PDEC && pad_get(7)->act == PAD_FX_REV &&
                 pad_get(12)->act == PAD_FX_DIST && pad_get(13)->act == PAD_PERCHAR &&
                 pad_get(14)->act == PAD_FX_CHOR && pad_get(15)->act == PAD_CLICK);
    wheel_reg_apply(&t->p[P_E0], 0);
    bad += check("P5 WHEEL: REG writes all nine bars (FLUTE), match finds it back",
                 wheel_reg_match(&t->p[P_E0]) == 0 && !strcmp(wheel_reg_name(0), "FLUTE"));
    t->p[P_E0] = 8;
    bad += check("P5 WHEEL: bars off a registration read as CUSTOM",
                 wheel_reg_match(&t->p[P_E0]) < 0 && !strcmp(wheel_reg_name(16), "CUSTOM"));

    /* P5e: the colour language on the switch half — green = on, red = off, amber = Leslie slow.
     * The FX entries used to carry PC_RED as their *active* colour, so OVERDRIVE and the FX toggles
     * were red whether engaged or not and the DRV knob never showed on the pad. */
    t->p[P_DIST] = 0; t->p[P_CHOR] = 0; t->p[P_REV] = 0; t->p[P_E10] = 0; t->p[P_E9] = 0;
    bad += check("P5e WHEEL: every switch reads red while it is off",
                 pad_colour(pad_get(12)) == PC_RED && pad_colour(pad_get(14)) == PC_RED &&
                 pad_colour(pad_get(7)) == PC_RED && pad_colour(pad_get(5)) == PC_RED &&
                 pad_colour(pad_get(15)) == PC_RED);
    t->p[P_DIST] = 64;
    bad += check("P5e WHEEL: OVERDRIVE turns green when the DRV knob engages it",
                 pad_colour(pad_get(12)) == PC_GRN && (pad_rgb_value(12) & 0xFF00u) == 0xC800u);
    t->p[P_CHOR] = 40; t->p[P_REV] = 40; t->p[P_E10] = 40;
    bad += check("P5e WHEEL: CHORUS / REVERB / CLICK follow their params",
                 pad_colour(pad_get(14)) == PC_GRN && pad_colour(pad_get(7)) == PC_GRN &&
                 pad_colour(pad_get(15)) == PC_GRN);
    t->p[P_E9] = 1;
    bad += check("P5e WHEEL: PERCUSSION on is green, off is red",
                 pad_colour(pad_get(5)) == PC_GRN);
    t->p[P_E9] = 4;                                  /* 3rd, soft */
    bad += check("P5e WHEEL: PERC HARMONIC lights the 3rd, the fast decay stays red",
                 pad_colour(pad_get(13)) == PC_GRN && pad_colour(pad_get(6)) == PC_RED);
    t->p[P_E11] = 0;
    bad += check("P5e WHEEL: LESLIE off is red", pad_colour(pad_get(4)) == PC_RED);
    t->p[P_E11] = 1;
    bad += check("P5e WHEEL: LESLIE slow is amber", pad_colour(pad_get(4)) == PC_AMB);
    t->p[P_E11] = 2;
    bad += check("P5e WHEEL: LESLIE fast is green", pad_colour(pad_get(4)) == PC_GRN);

    /* P5g: the Leslie pad's fill breathes with the horn, one turn is one blink, and it follows the
     * rotor the audio is really using (eng_wheel.c wheel_horn_phase / wheel_horn_mhz): a ROTR flip
     * eases over ~0.4 s, so the blink ramps with the ear instead of snapping to the new mode's rate,
     * and the brightness is the horn's own phase, so the pad peaks where the sound does. */
    {
        const uint32_t p_slow = (uint32_t)((double)FS * 1.25 / (double)CTL + 0.5);    /* 0.8 Hz: 1.25 s a turn */
        const uint32_t p_fast = (uint32_t)((double)FS / (6.7 * (double)CTL) + 0.5);   /* 6.7 Hz: 149 ms a turn */
        const int16_t e9 = t->p[P_E9], e10 = t->p[P_E10];
        fill_t stop_cold, stop_warm, sfill, ffill;
        uint32_t k, slow_p, fast_p, slow_mid, fast_mid, slow_hyst, fast_hyst;
        uint32_t lum_before, lum_after, half_turn;
        t->p[P_E9] = t->p[P_E10] = 0;                     /* (percussion and click off: the rotor alone) */

        t->p[P_E11] = 0;                                  /* STOP: the horn coasts to a halt */
        leslie_run(4u * FS / CTL);
        stop_cold = leslie_range(1000u);
        bad += check("P5g WHEEL: STOP is solid red once the horn has coasted",
                     stop_cold.lo == stop_cold.hi && stop_cold.lo > 0u &&
                     pad_colour(pad_get(4)) == PC_RED);

        t->p[P_E11] = 1;                                  /* SLOW: 0.8 Hz, 1.25 s a turn */
        leslie_run(3u * FS / CTL);                        /* (the ease is over) */
        sfill = leslie_range(6u * FS / CTL);              /* (the fill's ends: measured, not assumed) */
        slow_mid = (sfill.lo + sfill.hi) / 2u;
        slow_hyst = (sfill.hi - sfill.lo) / 8u;
        slow_p = leslie_period(6u * FS / CTL, slow_mid, slow_hyst);

        t->p[P_E11] = 2;                                  /* FAST: 6.7 Hz, 149 ms a turn */
        leslie_run(6u * FS / CTL);
        ffill = leslie_range(6u * FS / CTL);
        fast_mid = (ffill.lo + ffill.hi) / 2u;
        fast_hyst = (ffill.hi - ffill.lo) / 8u;
        fast_p = leslie_period(6u * FS / CTL, fast_mid, fast_hyst);

        bad += check("P5g WHEEL: the LESLIE pad breathes once a turn - 1.25 s slow, 149 ms fast",
                     near_u(slow_p, p_slow, 2u) && near_u(fast_p, p_fast, 3u));
        bad += check("P5g WHEEL: ..over the whole fill, dark to full at either speed",
                     sfill.lo > 0u && sfill.hi >= sfill.lo * 2u &&
                     ffill.lo > 0u && ffill.hi >= ffill.lo * 2u);
        /* the ear hears the ease, so the blink has to ease too: put the horn at the mic (phase 0, the
         * loudest point of the turn) and flip - half a turn later it is at the far side, and at the
         * slow rotor's pace, not at 6.7 Hz */
        t->p[P_E11] = 1;
        leslie_run(4u * FS / CTL);                        /* (back to a settled SLOW) */
        for (k = 0; k < 4u * FS / CTL; k++) {             /* (wait for the horn at the mic) */
            const uint32_t ph = wheel_horn_phase(t);
            if (ph < 0x02000000u || ph > 0xFE000000u)
                break;
            leslie_run(1u);
        }
        lum_before = leslie_lum() * 128u / sfill.hi;      /* (relative: the mode's colour changes) */
        t->p[P_E11] = 2;                                  /* SLOW -> FAST in one block */
        leslie_run(1u);
        lum_after = leslie_lum() * 128u / ffill.hi;
        half_turn = leslie_until_mid(4u * FS / CTL, fast_mid, fast_hyst);
        bad += check("P5g WHEEL: a ROTR flip eases the blink - the first half turn after it is still slow",
                     half_turn > fast_p && half_turn > 0u);
        bad += check("P5g WHEEL: ..and the fill cannot jump when the mode does",
                     lum_before > 110u && lum_after > 110u && lum_after <= 128u &&
                     (lum_before > lum_after ? lum_before - lum_after : lum_after - lum_before) <= 6u);
        /* the fill's map onto the turn: full as the horn passes the mic (phase 0: eng_wheel.c's AM is
         * at unity there), darkest half a turn later, no bumps in between - a triangle, one a turn */
        {
            uint32_t b, rel0 = 0u, relh = 0u, prev = 0u, ok = 1u;
            for (b = 0; b < 2u * p_fast; b++) {        /* (two turns, a sample a block) */
                const uint32_t ph = wheel_horn_phase(t);
                const uint32_t rel = leslie_lum() * 128u / ffill.hi;
                if (ph < 0x01000000u || ph > 0xFF000000u)
                    rel0 = rel;                        /* (the horn at the mic) */
                if (ph > 0x7F000000u && ph < 0x81000000u)
                    relh = rel;                        /* (half a turn away) */
                if (ph < 0x80000000u && prev && rel > prev + 2u)
                    ok = 0u;                           /* (it must not rise before the half turn) */
                prev = rel;
                leslie_run(1u);
            }
            bad += check("P5g WHEEL: ..full as the horn passes the mic, darkest half a turn later",
                         rel0 >= 120u && relh <= 60u && ok);
        }
        /* STOP from FAST: the horn coasts, so the pad keeps blinking (slower and slower) and only
         * then comes to rest, solid red */
        t->p[P_E11] = 0;
        {
            const fill_t coast = leslie_range(FS / 2u / CTL);
            leslie_run(8u * FS / CTL);
            stop_warm = leslie_range(500u);
            bad += check("P5g WHEEL: STOP from FAST keeps blinking while the horn coasts, then is solid",
                         coast.hi > coast.lo + 20u && stop_warm.lo == stop_warm.hi &&
                         stop_warm.lo == stop_cold.lo);
        }
        t->p[P_E9] = e9;
        t->p[P_E10] = e10;
    }

    /* P5e: a drawbar pad is a virtual drawbar (level 0..8), and its LED bar brightens with it */
    t->p[P_E5] = 0;
    bad += check("P5e: a drawbar pad's level is its bar's value",
                 pad_level_id(0) == P_E5 && pad_level_get(0) == 0 && pad_level_id(4) < 0 &&
                 pad_colour(pad_get(0)) == PC_DIM);
    pad_level_set(0, 5);
    bad += check("P5e: a drag sets the bar outright (and it reads yellow)",
                 t->p[P_E5] == 5 && pad_level_get(0) == 5 && pad_colour(pad_get(0)) == PC_YEL);
    pad_level_nudge(0, 1);
    pad_level_nudge(0, 9);
    bad += check("P5e: a nudge steps it and clamps at 8", t->p[P_E5] == 8);
    {
        uint32_t hi = pad_rgb_value(0);
        t->p[P_E5] = 1;
        bad += check("P5e: the pad's yellow climbs with the bar (level 8 brighter than 1)",
                     hi > pad_rgb_value(0) && pad_rgb_value(0) != 0);
    }
    t->p[P_E5] = 0;
    sk_host_pad_press(8, 1);                        /* the BOTTOM row's UP half */
    smk_pads_run();
    bad += check("P5e: a press steps the bar once", t->p[P_E5] == 1);
    fm1_ms += 400u;                                 /* held past the 350 ms delay: it ramps */
    smk_pads_run();
    bad += check("P5e: holding it ramps (press and drag, without a mouse)", t->p[P_E5] == 2);
    fm1_ms += 120u;
    smk_pads_run();
    bad += check("P5e: the ramp repeats every 110 ms", t->p[P_E5] == 3);
    sk_host_pad_press(8, 0);
    sk_host_pad_press(0, 1);                        /* the TOP row's DOWN half */
    smk_pads_run();
    bad += check("P5e: the top row pulls the same bar down", t->p[P_E5] == 2);
    sk_host_pad_press(0, 0);
    smk_pads_run();
    fm1_ms += 500u;
    smk_pads_run();
    bad += check("P5e: letting go stops the ramp", t->p[P_E5] == 2);
    pad_level_set(0, -4);
    bad += check("P5e: the level clamps at 0", t->p[P_E5] == 0);
    return bad;
}
static int any_voice(const track_t *t, int note)
{
    uint32_t i;
    for (i = 0; i < VOICE_POOL; i++)
        if (vpool[i].part == (uint8_t)(t - trk) && vpool[i].gate &&
            (note < 0 || vpool[i].note == (uint32_t)note))
            return 1;
    return 0;
}
static int drum_pads(void)
{
    int bad = 0, i;
    ui_power_on();
    track_t *t = &trk[0];
    t->eng_req = t->engine = ENGI_DRUM;
    pad_do_idx = 0;
    pad_do(pad_get(0));                              /* row 1, pad 1 = the KICK lane */
    bad += check("P5b DRUM: pad 1 strikes the kit (no MIDI note needed)",
                 any_voice(t, DRUM_LANE_NOTE[0]));
    pad_do_idx = 8;
    pad_do(pad_get(8));                              /* row 2, pad 9 = PUNCH (GM 35) */
    bad += check("P5b DRUM: the second row plays the extra GM voices", any_voice(t, 35));
    for (i = 0; i < VOICE_POOL; i++)                 /* let the drums go */
        if (vpool[i].part == 0u)
            vpool[i].gate = 0;
    t->eng_req = t->engine = ENGI_WHEEL;
    t->p[P_E0] = 0;
    pad_do_idx = 0;
    pad_do(pad_get(0));                              /* a drawbar pad: an action, never a note */
    bad += check("P5 WHEEL: a drawbar pad does not sound a note", !any_voice(t, -1));
    return bad;
}
/* P5f: the PERFORM recall surface. Pads 1..8 are U01..U08 on the top row (hardware pads 0..7) and
 * 9..15 the bottom row, exactly as the wiring datasheet's reading order; pad 16 is the edit jump. A
 * recall must load the sound and keep the page: the companion keys its desk off the same page. */
static int perf_page(void)
{
    int bad = 0;
    ui_power_on();
    track_t *t = &trk[0];
    go_page(GR_PERF);
    bad += check("P5f PERFORM: pads 1..8 = U01..U08, 9..15 = U09..U15, 16 = ED",
                 pad_get(0)->act == PAD_RECALL && pad_get(0)->param == 0 &&
                 pad_get(7)->act == PAD_RECALL && pad_get(7)->param == 7 &&
                 pad_get(8)->act == PAD_RECALL && pad_get(8)->param == 8 &&
                 pad_get(14)->param == 14 && pad_get(15)->act == PAD_EDITJUMP);
    bad += check("P5f PERFORM: the recall surface owns the pads and reports itself as PERFORM",
                 pad_get(0)->act == PAD_RECALL && !ui.home && cur_page()->graph == GR_PERF);
    bad += check("P5f PERFORM: a free slot reads EMPTY under its U label and stays dark",
                 !strcmp(pad_get(0)->abbr, "U01") && !strcmp(pad_sub_label(0), "EMPTY") &&
                 pad_colour(pad_get(0)) == PC_OFF);
    bad += check("P5f PERFORM: only recall pads carry a second line",
                 !strcmp(pad_sub_label(15), "") && !strcmp(pad_sub_label(0), "EMPTY"));

    /* hold U01 past the 600 ms long-press: the first one only arms the two-step save */
    sk_host_pad_press(0, 1); smk_pads_run();
    fm1_ms += 650u;
    sk_host_pad_press(0, 0); smk_pads_run();
    bad += check("P5f PERFORM: a long press arms the save and says so",
                 pad_arm_slot == 0u && !strcmp(pad_sub_label(0), "HOLD TO SAVE"));
    sk_host_pad_press(0, 1); smk_pads_run();
    fm1_ms += 650u;
    sk_host_pad_press(0, 0); smk_pads_run();
    bad += check("P5f PERFORM: the second long press stores the sound in U01", up_used(0) && pad_arm_slot == 0xFFu);
    bad += check("P5f PERFORM: the slot's second line names the patch it holds (not the slot)",
                 pad_sub_label(0)[0] && strcmp(pad_sub_label(0), "EMPTY") != 0 &&
                 strcmp(pad_sub_label(0), "U01") != 0);

    /* recall: the sound comes back and the page does not move */
    {
        const int16_t keep = t->p[P_E2];
        t->p[P_E2] = (int16_t) (keep ? keep - 1 : keep + 1);
        go_page(GR_PERF);
        sk_host_pad_press(0, 1); smk_pads_run();
        fm1_ms += 60u;                                   /* a short press: recall, not save */
        sk_host_pad_press(0, 0); smk_pads_run();
        bad += check("P5f PERFORM: a tap recalls the slot's sound", t->p[P_E2] == keep);
        bad += check("P5f PERFORM: the recall keeps the PERFORM page (no jump to the editor)",
                     cur_page()->graph == GR_PERF && !ui.home);
        bad += check("P5f PERFORM: the recalled slot is the lit one",
                     t->user == 1u && pad_colour(pad_get(0)) == PC_GRN);
    }
    return bad;
}

int main(void)
{
    int bad = scl_page() + chord_page() + arp_page() + pad_capture() + wheel_page() + perf_page() + drum_pads();
    printf("%s\n", bad ? "PAGES TEST FAILED" : "SCL / CHORD / ARP / WHEEL / PERFORM / DRUM page tests passed");
    return bad != 0;
}
