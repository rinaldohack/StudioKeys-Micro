/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * P17: the SCL / CHORD / ARP pages' tailored pad maps (P5b), their direct picks and LED colours. */
#define UI_TEST_NO_MAIN 1
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
    bad += check("P17 ARP: HOLD latches the chord", t->p[P_AHOLD] == TP[P_AHOLD].max &&
                 pad_colour(pad_get(14)) == PC_RED);
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
int main(void)
{
    int bad = scl_page() + chord_page() + arp_page() + pad_capture();
    printf("%s\n", bad ? "PAGES TEST FAILED" : "SCL / CHORD / ARP page tests passed");
    return bad != 0;
}
