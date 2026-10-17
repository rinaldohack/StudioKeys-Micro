/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM),
 * a seven-saw supersaw, hard sync, ring, noise, drive and a trapezoidal filter.
 * SUP's detune and mix follow the JP-8000 Super Saw measurements in Adam Szabo,
 * "How to Emulate the Super Saw" (KTH 2010): the spread stays tight until late
 * in the knob, the six sides are not a mirror image, and mix lowers the center
 * while the sides rise. On SUP the noise knob is the sub and the drive knob is
 * the filter mode. Drift is a few cents on the stack, not a knob. SYNC and RING
 * are the two-oscillator path. HYPE is a second stack: detune opens early and
 * wide, the sub is the same noise knob, and the filter is pushed harder. */
static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM", "SUP", "SYNC", "RING", "HYPE"};

/* Cents of the seven saws at full detune. The center stays on the note.
 * The flat side opens further than the sharp side. */
static const int16_t SUP_FULL[7] = {-202, -113, -34, 0, 34, 104, 177};

/* DTN 0..127 -> Q10 fraction of SUP_FULL. Flat through the middle, then open. */
static const uint16_t SUP_SPREAD[128] = {
        0,    1,    3,    4,    6,    7,    8,   10,   11,   13,   15,   16,   18,   19,   21,   23,
       24,   26,   27,   29,   30,   32,   33,   35,   36,   38,   40,   41,   43,   45,   46,   48,
       49,   51,   53,   54,   56,   57,   59,   61,   62,   64,   65,   67,   68,   70,   72,   73,
       75,   76,   78,   80,   81,   83,   84,   86,   88,   89,   91,   92,   94,   96,   97,   99,
      102,  105,  109,  112,  115,  118,  121,  124,  128,  131,  134,  138,  141,  144,  147,  151,
      157,  163,  168,  174,  180,  186,  192,  198,  204,  211,  217,  224,  230,  236,  243,  249,
      256,  262,  268,  275,  281,  288,  294,  300,  307,  313,  320,  326,  332,  339,  345,  352,
      358,  364,  371,  377,  383,  390,  396,  402,  480,  558,  635,  713,  791,  869,  946, 1024,
};

/* MIX 0..127 -> center saw and one side saw, Q15. Sides match the center near 95. */
static const uint16_t SUP_CENTER[128] = {
    32697,32554,32411,32268,32125,31982,31839,31697,31554,31411,31268,31125,30982,30840,30697,30554,
    30411,30268,30125,29982,29840,29697,29554,29411,29268,29125,28982,28840,28697,28554,28411,28268,
    28125,27983,27840,27697,27554,27411,27268,27125,26983,26840,26697,26554,26411,26268,26126,25983,
    25840,25697,25554,25411,25268,25126,24983,24840,24697,24554,24411,24268,24126,23983,23840,23697,
    23554,23411,23269,23126,22983,22840,22697,22554,22411,22269,22126,21983,21840,21697,21554,21412,
    21269,21126,20983,20840,20697,20554,20412,20269,20126,19983,19840,19697,19554,19412,19269,19126,
    18983,18840,18697,18555,18412,18269,18126,17983,17840,17697,17555,17412,17269,17126,16983,16840,
    16698,16555,16412,16269,16126,15983,15840,15698,15555,15412,15269,15126,14983,14840,14698,14555,
};
static const uint16_t SUP_SIDE[128] = {
     1454, 1784, 2111, 2434, 2755, 3073, 3388, 3700, 4008, 4314, 4617, 4917, 5214, 5508, 5799, 6086,
     6371, 6653, 6932, 7208, 7481, 7751, 8017, 8281, 8542, 8800, 9055, 9307, 9556, 9802,10044,10284,
    10521,10755,10986,11214,11439,11661,11880,12096,12309,12518,12725,12929,13130,13328,13523,13715,
    13904,14090,14273,14453,14630,14804,14975,15143,15308,15470,15629,15785,15938,16088,16235,16379,
    16520,16658,16793,16925,17054,17180,17303,17423,17540,17654,17765,17873,17978,18080,18179,18275,
    18368,18458,18545,18629,18710,18788,18863,18935,19004,19070,19133,19193,19250,19305,19356,19404,
    19449,19491,19530,19566,19599,19629,19656,19680,19702,19720,19735,19747,19756,19762,19765,19765,
    19763,19757,19748,19736,19721,19703,19682,19658,19632,19602,19569,19533,19494,19452,19408,19360,
};

static int32_t analog_super(voice_t *v, uint32_t *ph0, uint32_t *ph1, const vmod_t *m, int32_t det, int32_t mix,
                            const int8_t *wob)
{
    uint32_t k, ph[7];
    int32_t stack = 0, one = 0, avg, amt, ctr, side;
    if (det < 0)
        det = 0;
    if (det > 127)
        det = 127;
    if (mix < 0)
        mix = 0;
    if (mix > 127)
        mix = 127;
    amt = (int32_t)SUP_SPREAD[det];
    ctr = (int32_t)SUP_CENTER[mix];
    side = (int32_t)SUP_SIDE[mix];
    ph[0] = *ph0;
    ph[1] = *ph1;
    ph[2] = v->ph[2];
    ph[3] = (uint32_t)v->s[3];
    ph[4] = (uint32_t)v->s[4];
    ph[5] = (uint32_t)v->s[5];
    ph[6] = (uint32_t)v->s[6];
    for (k = 0; k < 7u; k++) {
        int32_t ct = (SUP_FULL[k] * amt >> 10) + (wob ? wob[k] : 0);
        uint32_t inc = ct ? cents_inc(m->pitch16, ct, 0) : m->inc;
        int32_t w = osc_saw(ph[k], inc);
        ph[k] += inc;
        stack += w;
        if (k == 3u)
            one = w;
    }
    *ph0 = ph[0];
    *ph1 = ph[1];
    v->ph[2] = ph[2];
    v->s[3] = (int32_t)ph[3];
    v->s[4] = (int32_t)ph[4];
    v->s[5] = (int32_t)ph[5];
    v->s[6] = (int32_t)ph[6];
    /* Six side saws, each at SUP_SIDE, plus the center. >> 1 keeps the sum
     * near one saw; the sides still sit at the measured level against it. */
    avg = (stack - one) / 6;
    return (mulq15(one, ctr) + 6 * mulq15(avg, side)) >> 1;
}

/* HYPE cents at full detune. Not a mirror, so the stack does not cancel. */
static const int16_t HYPE_FULL[7] = {-150, -90, -40, 0, 45, 100, 170};

/* Same seven phases as SUP. Detune is linear in the knob: half-scale near 64,
 * full at the top. Mix 0 is the center saw, mix 127 lets the sides take over. */
static int32_t analog_hype(voice_t *v, uint32_t *ph0, uint32_t *ph1, const vmod_t *m, int32_t det, int32_t mix,
                           const int8_t *wob)
{
    uint32_t k, ph[7];
    int32_t stack = 0, one = 0, avg, amt, ctr, side;
    if (det < 0)
        det = 0;
    if (det > 127)
        det = 127;
    if (mix < 0)
        mix = 0;
    if (mix > 127)
        mix = 127;
    amt = det << 3;
    if (amt > 1024)
        amt = 1024;
    ctr = 32767 - mix * 200;
    if (ctr < 0)
        ctr = 0;
    side = mix * 180;
    ph[0] = *ph0;
    ph[1] = *ph1;
    ph[2] = v->ph[2];
    ph[3] = (uint32_t)v->s[3];
    ph[4] = (uint32_t)v->s[4];
    ph[5] = (uint32_t)v->s[5];
    ph[6] = (uint32_t)v->s[6];
    for (k = 0; k < 7u; k++) {
        int32_t ct = (HYPE_FULL[k] * amt >> 10) + (wob ? wob[k] : 0);
        uint32_t inc = ct ? cents_inc(m->pitch16, ct, 0) : m->inc;
        int32_t w = osc_saw(ph[k], inc);
        ph[k] += inc;
        stack += w;
        if (k == 3u)
            one = w;
    }
    *ph0 = ph[0];
    *ph1 = ph[1];
    v->ph[2] = ph[2];
    v->s[3] = (int32_t)ph[3];
    v->s[4] = (int32_t)ph[4];
    v->s[5] = (int32_t)ph[5];
    v->s[6] = (int32_t)ph[6];
    avg = (stack - one) / 6;
    return (mulq15(one, ctr) + 6 * mulq15(avg, side)) >> 1;
}

/* Filter envelope, its own contour (the amp envelope still shapes loudness).
 * s[7]: level in the low bits, stage in bits 28..29. Returns Q15. */
static int32_t analog_feg(voice_t *v, const int16_t *p)
{
    int32_t keep = v->s[7] & (int32_t)(3u << 30);   /* sub divider, not the envelope */
    int32_t e = v->s[7] & 0x0FFFFFFF;
    uint32_t st = ((uint32_t)v->s[7] >> 28) & 3u;
    int32_t sus = (int32_t)p[P_SUS] << 17;
    if (!v->gate && st < 3u)
        st = 3;
    if (v->gate && !st)
        st = 1;
    switch (st) {
    case 1:
        e += (int32_t)ENV_LIN[p[P_ATK] & 127];
        if (e >= (1 << 24)) {
            e = 1 << 24;
            st = 2;
        }
        break;
    case 2:
        e += mulq16(sus - e, ENV_EXP[p[P_DEC] & 127]);
        break;
    case 3:
        e -= mulq16(e, ENV_EXP[p[P_REL] & 127]);
        if (e < (1 << 12)) {
            e = 0;
            st = 0;
        }
        break;
    default:
        e = 0;
        break;
    }
    if (e < 0)
        e = 0;
    if (e > (1 << 24))
        e = 1 << 24;
    v->s[7] = e | (int32_t)(st << 28) | keep;
    return e >> 9;
}

/* SVF with a chosen output. mode 0 LP (same state update as tsvf_lp), 1 LP24
 * (a second stage in *pole), 2 bandpass, 3 highpass. */
static int32_t analog_filt(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, int32_t *pole, uint32_t mode,
                           int32_t cut)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    int32_t y, a, sh;
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    y = mode == 2u ? v1 : mode == 3u ? in - v2 : v2;
    if (mode == 1u && pole) {
        sh = 8 - (cut >> 11);
        if (sh < 1)
            sh = 1;
        if (sh > 8)
            sh = 8;
        *pole += (y - *pole) >> sh;
        y = *pole;
    }
    a = y < 0 ? -y : y;
    if (a > 16000) {
        a = 16000 + (softclip((a - 16000) * 2) >> 1);
        y = y < 0 ? -a : a;
    }
    return y;
}

static uint32_t sup_rng(uint32_t r)
{
    return r * 1664525u + 1013904223u;
}

static void analog_note_on(track_t *t, voice_t *v)
{
    v->ph[1] = v->ph[0] + 0x40000000u;
    if ((uint32_t)t->p[P_E0] == 5u || (uint32_t)t->p[P_E0] == 8u) {
        /* Each SUP note starts the seven saws at a new phase. The other waves
         * keep the fixed second-oscillator offset above. */
        uint32_t r = 0x6D2B79F5u ^ ((uint32_t)v->age << 16) ^ (uint32_t)v->age;
        r = sup_rng(r); v->ph[0] = r;
        r = sup_rng(r); v->ph[1] = r;
        r = sup_rng(r); v->ph[2] = r;
        r = sup_rng(r); v->s[3] = (int32_t)r;
        r = sup_rng(r); v->s[4] = (int32_t)r;
        r = sup_rng(r); v->s[5] = (int32_t)r;
        r = sup_rng(r); v->s[6] = (int32_t)r;
    } else {
        v->ph[2] = v->ph[0] + 0x15555555u;
        v->s[3] = (int32_t)(v->ph[0] + 0x2AAAAAAAu);
        v->s[4] = (int32_t)(v->ph[0] + 0x6AAAAAAAu);
        v->s[5] = (int32_t)(v->ph[0] + 0x80000000u);
        v->s[6] = (int32_t)(v->ph[0] + 0xC0000000u);
    }
    v->s[0] = v->s[1] = 0;                            /* filter */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
    v->s[7] = 1 << 28;                                /* filter envelope: attack, level 0 */
}

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    int32_t drive = 32768 + p[P_E6] * 512;                       /* 1x .. 3x */
    uint32_t inc1 = m->inc;
    uint32_t inc2 = det ? cents_inc(m->pitch16, det, 0) : inc1;      /* DTN in cents */
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    int32_t nz = noise * 200, drv = p[P_E6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];                  /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    if (wave >= 5u) {
        /* SUP / SYNC / RING: the filter envelope replaces the amp envelope's push
         * on this voice (voice.c already added that push to m->cutoff). */
        int32_t amp_push = (m->envq15 * p[P_ED_FLT]) >> 7;
        int32_t feg = analog_feg(v, p);
        uint32_t mode = (uint32_t)drv >> 5;
        int8_t wob[7];
        int32_t pole = 0;
        if (mode > 3u)
            mode = 3u;
        cut += ((feg * p[P_ED_FLT]) >> 7) - amp_push;
        if ((wave == 5u || wave == 8u) && mode != 1u) {
            uint32_t dph = (uint32_t)nst;
            for (i = 0; i < 7u; i++)
                wob[i] = (int8_t)(sine_i(dph + i * 0x24924924u) >> 13);   /* about +-4 cents */
            nst = (int32_t)(dph + 0x200000u);
        } else {
            for (i = 0; i < 7u; i++)
                wob[i] = 0;
            if (wave == 5u || wave == 8u) {
                pole = nst;
                if (pole > 40000 || pole < -40000)
                    pole = 0;
            }
        }
        tsvf_coef(&flt, cut, p[P_E5]);
        for (i = 0; i < n; i++) {
            int32_t a, b, s;
            uint32_t o0;
            if (wave == 5u || wave == 8u) {
                o0 = (uint32_t)v->s[3];               /* the center saw, before this sample advances it */
                s = wave == 8u ? analog_hype(v, &ph0, &ph1, m, det, mix, wob)
                               : analog_super(v, &ph0, &ph1, m, det, mix, wob);
                if (wave == 8u && s > 14000)
                    s = 14000 + (softclip((s - 14000) << 1) >> 2);
                else if (wave == 8u && s < -20000)
                    s = -20000 - (softclip((-20000 - s) << 1) >> 3);
                if ((uint32_t)v->s[3] < o0) {         /* two of its cycles: square sub, one octave down */
                    uint32_t bit = 1u << 30;
                    if ((uint32_t)v->s[7] & bit)
                        v->s[7] ^= (int32_t)(bit << 1);
                    v->s[7] ^= (int32_t)bit;
                }
                if (nz) {
                    int32_t sub = ((uint32_t)v->s[7] & (1u << 31)) ? -20000 : 20000;
                    s += mulq15(sub, nz);
                }
            } else if (wave == 6u) {
                a = osc_saw(ph0, inc1);
                o0 = ph0;
                ph0 += inc1;
                if (ph0 < o0)
                    ph1 = 0;                          /* hard sync */
                b = osc_saw(ph1, inc2);
                ph1 += inc2;
                s = mulq15(a, m1) + mulq15(b, m2);
            } else {
                a = osc_sine(ph0);
                b = osc_sine(ph1);
                ph0 += inc1;
                ph1 += inc2;
                b = (a * b) >> 15;                    /* ring */
                s = mulq15(a, m1) + mulq15(b, m2);
            }
            s = analog_filt(&flt, s >> 1, &ic1, &ic2, &pole, mode, cut) << 1;
            out[i] += voice_amp(s, m, i) << 1;
        }
        if ((wave == 5u || wave == 8u) && mode == 1u)
            nst = pole;
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->s[0] = ic1;
        v->s[1] = ic2;
        v->s[2] = nst;
        return;
    }
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t a, b, s;
        switch (wave) {
        case 1:
            a = osc_pulse(ph0, inc1, 0x80000000u);
            b = osc_pulse(ph1, inc2, 0x80000000u);
            break;
        case 2:
            a = osc_tri(ph0);
            b = osc_tri(ph1);
            break;
        case 3:
            a = osc_sine(ph0);
            b = osc_sine(ph1);
            break;
        case 4:
            a = osc_pulse(ph0, inc1, pw);
            b = osc_pulse(ph1, inc2, pw);
            break;
        default:
            a = osc_saw(ph0, inc1);
            b = osc_saw(ph1, inc2);
            break;
        }
        ph0 += inc1;
        ph1 += inc2;
        s = mulq15(a, m1) + mulq15(b, m2);
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);   /* pre-shifts: drive is up to 3x, no overflow */
        {   /* filter, linear up to half scale, then a soft knee (only resonance peaks saturate); soft_knee()
             * written out: the host compiler makes this loop 1 % slower with the call */
            int32_t y = tsvf_lp(&flt, s >> 1, &ic1, &ic2), aa = y < 0 ? -y : y;
            if (aa > 16000) {
                aa = 16000 + (softclip((aa - 16000) * 2) >> 1);
                y = y < 0 ? -aa : aa;
            }
            s = y << 1;
        }
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
}

static const preset_t ANALOG_PRESETS[] = {
    {"SAW LEAD", {0, 12, 64, 0, 90, 30, 10, 64}, {4, 70, 100, 50}, 20, 1, FX(0, 10, 45, 30), PAT(4),
     GLIDE(40)},
    {"SOFT PAD", {0, 20, 64, 4, 60, 10, 0, 32}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SQR BASS", {1, 0, 0, 0, 50, 70, 40, 64}, {0, 60, 40, 30}, 30, 1, FX(5, 0, 10, 10), PAT(2)},
    {"PWM STR", {4, 8, 40, 0, 75, 20, 0, 48}, {60, 80, 110, 85}, 8, 0, FX(0, 50, 20, 60), PAT(5)},
    {"ACID", {0, 0, 0, 0, 50, 100, 25, 64}, {0, 55, 20, 30}, 48, 1, FX(20, 0, 45, 15), PAT(1),
     GLIDE(28), GLMODE(1)},
    {"SINE KEY", {3, 6, 50, 0, 127, 0, 0, 0}, {2, 80, 30, 70}, 0, 0, FX(0, 30, 25, 40), PAT(6)},
    {"RAVE", {4, 30, 64, 0, 85, 20, 30, 64}, {20, 80, 110, 60}, 10, 1, FX(30, 40, 30, 30), PAT(13)},
    {"SUB BASS", {3, 0, 0, 0, 40, 0, 20, 0}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    {"PLUCK", {0, 8, 50, 0, 30, 40, 0, 64}, {0, 88, 0, 60}, 55, 0, FX(0, 20, 50, 30), PAT(3)},
    {"BRASS", {0, 10, 64, 0, 45, 20, 10, 64}, {35, 70, 90, 45}, 40, 0, FX(0, 20, 20, 40), PAT(6)},
    {"WIND", {0, 0, 0, 90, 30, 90, 0, 0}, {60, 90, 60, 80}, 50, 0, FX(0, 30, 30, 70), PAT(5)},
    {"STRINGS", {0, 25, 64, 0, 70, 10, 0, 32}, {70, 90, 115, 90}, 5, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SUPER", {5, 112, 110, 0, 80, 18, 0, 48}, {40, 80, 100, 70}, 12, 0, FX(0, 40, 25, 55), PAT(5)},
    {"HYPE", {8, 96, 110, 40, 64, 48, 36, 72}, {6, 64, 30, 36}, 36, 0, FX(8, 28, 18, 36), PAT(4)},
    /* R&B / EDM stage sounds (LangitCinta! StageLabs): three basses, three chord/pad voices, three leads.
     * Programmed for the styles around RnB and EDM - a ballad pad, a funk pluck, an arena saw, a talk lead -
     * with no artist or record named. Appended: a stored preset index never moves. */
    {"R&B BASS", {2, 8, 60, 0, 52, 22, 26, 16}, {0, 80, 55, 24}, 28, 1, FX(3, 0, 0, 4), PAT(2)},
    {"808 SUB", {3, 0, 0, 0, 34, 0, 18, 0}, {0, 115, 100, 60}, 0, 1, FX(6, 0, 0, 5), PAT(8)},
    {"FUNK BASS", {1, 10, 58, 0, 58, 58, 30, 28}, {0, 58, 42, 18}, 48, 1, FX(10, 0, 0, 6), PAT(3), GLIDE(10)},
    {"CHORD STAB", {4, 22, 64, 0, 80, 32, 12, 40}, {2, 58, 34, 42}, 22, 0, FX(4, 48, 18, 44), PAT(6)},
    {"BALLAD PAD", {5, 48, 64, 26, 62, 14, 0, 48}, {78, 92, 112, 100}, 10, 0, FX(0, 70, 30, 80), PAT(5)},
    {"WARM CHORD", {0, 32, 72, 0, 60, 20, 8, 44}, {34, 82, 96, 88}, 14, 0, FX(0, 56, 26, 66), PAT(5)},
    {"TALK LEAD", {6, 0, 88, 0, 74, 72, 44, 44}, {0, 62, 72, 36}, 26, 1, FX(14, 22, 32, 30), PAT(4),
     GLIDE(36), GLMODE(1)},
    {"ARENA LEAD", {8, 60, 64, 30, 88, 24, 0, 52}, {4, 76, 106, 62}, 12, 0, FX(0, 36, 32, 56), PAT(4)},
    {"NEON LEAD", {4, 26, 62, 0, 96, 46, 22, 58}, {0, 52, 58, 30}, 30, 1, FX(12, 26, 36, 34), PAT(4),
     GLIDE(14)},
};

static const param_desc_t *analog_desc(const struct track *t, uint32_t k);

static const engine_t ENG_ANALOG = {
    .name = "ANALOG",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 8, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = ANALOG_PRESETS,
    .npresets = NELEM(ANALOG_PRESETS),
    .note_on = analog_note_on,
    .render = analog_render,
    .knob = {P_E4, P_E5, P_ATK, P_REL},
    .keep = 0x03,                /* the filter */
    .desc = analog_desc,
};

/* SUP: noise is the sub, drive is the filter mode (0..31 LP, 32..63 LP24, 64..95 BP, 96..127 HP).
 * SYNC and RING use that mode too. Saw through PWM keep noise and drive. */
static const param_desc_t ANALOG_SUB = {"SUB", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t ANALOG_MODE = {"MODE", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t *analog_desc(const struct track *t, uint32_t k)
{
    uint32_t wave = (uint32_t)t->p[P_E0];
    if (wave < 5u)
        return 0;
    if ((wave == 5u || wave == 8u) && k == 3u)
        return &ANALOG_SUB;
    if (k == 6u)
        return &ANALOG_MODE;
    return 0;
}
