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
    bad += check("P5 WHEEL: MORE keeps PERC / CLICK / ROTR and the mix (no drawbar 6..9 on knobs)",
                 smk_bank_param(0) == P_E9 && smk_bank_param(1) == P_E10 && smk_bank_param(2) == P_E11 &&
                 smk_bank_param(3) == P_LEVEL && smk_bank_param(7) == 0xFFFFu);
    bad += check("P5 WHEEL: pads 1..4 are the bottom (UP) half, pads 9..12 the top (DOWN) half",
                 pad_get(0)->act == PAD_DBAR && ((pad_get(0)->param >> 4) & 1u) == 1u &&
                 pad_get(8)->act == PAD_DBAR && ((pad_get(8)->param >> 4) & 1u) == 0u &&
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
    sk_host_pad_press(0, 1);                        /* the companion's press injects the scan's edge */
    smk_pads_run();
    bad += check("P5e: a press steps the bar once", t->p[P_E5] == 1);
    fm1_ms += 400u;                                 /* held past the 350 ms delay: it ramps */
    smk_pads_run();
    bad += check("P5e: holding it ramps (press and drag, without a mouse)", t->p[P_E5] == 2);
    fm1_ms += 120u;
    smk_pads_run();
    bad += check("P5e: the ramp repeats every 110 ms", t->p[P_E5] == 3);
    sk_host_pad_press(0, 0);
    smk_pads_run();
    fm1_ms += 500u;
    smk_pads_run();
    bad += check("P5e: letting go stops the ramp", t->p[P_E5] == 3);
    pad_level_set(0, -4);
    bad += check("P5e: the level clamps at 0", t->p[P_E5] == 0);
    return bad;
}
static int any_voice(const track_t *t, int note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].gate && (note < 0 || t->v[i].note == (uint32_t)note))
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
    for (i = 0; i < NVOICE; i++)                     /* let the drums go */
        t->v[i].gate = 0;
    t->eng_req = t->engine = ENGI_WHEEL;
    t->p[P_E0] = 0;
    pad_do_idx = 0;
    pad_do(pad_get(0));                              /* a drawbar pad: an action, never a note */
    bad += check("P5 WHEEL: a drawbar pad does not sound a note", !any_voice(t, -1));
    return bad;
}
int main(void)
{
    int bad = scl_page() + chord_page() + arp_page() + pad_capture() + wheel_page() + drum_pads();
    printf("%s\n", bad ? "PAGES TEST FAILED" : "SCL / CHORD / ARP / WHEEL / DRUM page tests passed");
    return bad != 0;
}
