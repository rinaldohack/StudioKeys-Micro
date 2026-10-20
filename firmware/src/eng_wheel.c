/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* WHEEL: a tonewheel-style additive organ. StudioKEYS's own design.
 *
 * Partials: nine sines per voice at the classic drawbar footages, 16' 5 1/3' 8' 4' 2 2/3' 2'
 * 1 3/5' 1 1/3' 1' (0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 times the note), each with a level 0..8
 * (about 3 dB a step). One shared sine table (plain lookup), one free-running integer phase
 * per partial: a note starts wherever its wheels are, as on the real generators. A partial
 * above ~6 kHz folds back an octave (as the top of a tonewheel set does), so nothing aliases.
 * Partials at level 0 are skipped (their phase still runs).
 *
 * Registration: the nine drawbar levels are the engine's own params P_E0..P_E8 (0..8 each,
 * left to right 16' .. 1'), so a knob, a pad or the companion's console can move one bar
 * without touching another. The 16 registrations (DRW_REG) are a shortcut, not storage: the
 * REG picker writes all nine bars at once, which is also what an old 8-value sound does when
 * it loads (wheel_migrate below turns its REG + SUB/BODY/TOP trims into the nine bars).
 * The louder the registration, the less each bar gets (a soft normalisation: full bars are
 * louder than one bar, but they do not stack up to clipping).
 *
 * PERC: a decaying partial on the 2nd (4') or 3rd (2 2/3') harmonic, normal or soft, fast
 * or slow. Single trigger: it only strikes on a note that starts while no other key of the
 * part is held (legato playing gets none). With PERC on the 1' bar is silent and, at normal
 * level, the bars drop by ~3 dB, as on the real circuit.
 * CLICK: a short burst of high-passed noise at the key contact (half as loud at the release).
 * DRIVE: the track's DIST (fx.c) — the engine has no drive of its own any more; the OVERDRIVE
 * pad and the desk's DRIVE knob both write P_DIST.
 * ROTR: a rotary speaker effect: two rotors (a low one and a high one, split by partial
 * frequency at ~800 Hz) that spin SLOW or FAST and ramp between the speeds (the high rotor in
 * ~1 s, the low one in ~4 s); each gives its partials an amplitude and a pitch (Doppler)
 * modulation whose depth follows its speed. All of it is folded into the per-block partial
 * gains and increments: no delay line, no per-sample cost. OFF fades it out and stops them.
 * In stereo (render_stereo: the mix's path) two virtual mics 60 deg off the front give the two
 * rotors' amplitude curves 120 deg apart, so the horn and the drum swing between L and R as they
 * turn; the mono fold-down (L + R) is the same rotary a little less deep. mix_part (fx.c) folds
 * the two channels out of the side signal, so PAN and the sends still act on the whole part.
 *
 * Envelope: an organ gate (the presets set A 0, S 127, a short R); the track ADSR still applies.
 * Cost: per sample and partial one table lookup and a multiply-add (a gain ramp only for the
 * partials whose gain moved: percussion, rotary, knobs); the bar levels, the rotor and the
 * percussion run once per block. State: the per-track rotor and bar gains (drw_trk) and the
 * per-voice phases / gains (drw_vc) live here, indexed by part and voice; voice_t is unused. */
#define DRW_NP 9                 /* partials */
#define DRW_POLY 8               /* engine_t.poly, and the size of the per-voice state below (92 B a voice) */
#define DRW_FOLD 584366000u      /* ~6 kHz as a phase increment: partials above fold down an octave */
#define DRW_XOVER 77915000u      /* ~800 Hz: below the low rotor, above the high rotor */
/* the stereo mics: 60 deg either side of the front (the signal's phase span between them), and the
 * pair's AM depth (mono's own curves, am[], are their mean: 40 % horn / 21 % drum) */
#define DRW_MIC_DEP_HI 18000
#define DRW_MIC_DEP_LO 15000

/* drawbar level 0..8 -> Q15, 3 dB a step */
static const uint16_t DRW_LV[9] = {0, 2920, 4125, 5827, 8231, 11627, 16423, 23198, 32767};

/* the 16 registrations, one digit per bar: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' — the REG
 * picker on the device's desk and the companion's console writes a row into P_E0..P_E8 */
static const char DRW_REG[16][DRW_NP + 1] = {
    "008000000", /* FLUTE  one soft flute */
    "008400000", /* MELLO  flute and octave */
    "800800000", /* HOLLW  sub and octave, hollow */
    "838000000", /* SMOOT  smooth comping */
    "888000000", /* 3BAR   the first three bars out */
    "888800000", /* BLUES  three bars and the octave */
    "888600006", /* GOSPL  body with a little shimmer */
    "888000888", /* ROCK   body and the top three */
    "800000888", /* TOPS   sub and the top only */
    "008080800", /* CLARI  odd harmonics, reedy-hollow */
    "008765432", /* REED   a falling slope, brassy */
    "006876540", /* STRNG  bright and thin */
    "004432000", /* CHAPL  soft, churchy */
    "008888000", /* BRITE  even bars, bright */
    "858000000", /* BASS   for the left hand */
    "888888888", /* FULL   everything */
};
static const char *const N_DRW_PERC[] = {"OFF", "2ND", "3RD", "2SOFT", "3SOFT", "2SLOW", "3SLOW"};
static const char *const N_DRW_ROTR[] = {"OFF", "SLOW", "FAST"};

/* P5: the REG knob on HOME (K6 on SOUND). It is a shortcut, not a stored value: a turn writes all
 * nine bars through wheel_reg_match / wheel_reg_apply, so the registration follows the bars. */
static const char *const DRW_REG_NAME[17] = {
    "FLUTE", "MELLO", "HOLLW", "SMOOT", "3BAR", "BLUES", "GOSPL", "ROCK",
    "TOPS", "CLARI", "REED", "STRNG", "CHAPL", "BRITE", "BASS", "FULL", "CUSTOM",
};
const char *wheel_reg_name(int32_t r) { return r >= 0 && r <= 16 ? DRW_REG_NAME[r] : "CUSTOM"; }
const char *const *wheel_reg_names(void) { return DRW_REG_NAME; }
/* the registration the nine bars currently spell, or -1 for a custom slope */
int32_t wheel_reg_match(const int16_t *p)
{
    uint32_t r, k;
    for (r = 0; r < 16u; r++) {
        for (k = 0; k < DRW_NP; k++)
            if ((int32_t)clamp(p[k], 0, 8) != DRW_REG[r][k] - '0')
                break;
        if (k == DRW_NP)
            return (int32_t)r;
    }
    return -1;
}
void wheel_reg_apply(int16_t *p, int32_t r)
{
    uint32_t k;
    if (r < 0) r = 0;
    if (r > 15) r = 15;
    for (k = 0; k < DRW_NP; k++)
        p[k] = (int16_t)(DRW_REG[r][k] - '0');
}

/* partial frequency / note frequency, as a shift-and-add of the note's increment; FOLD_LIM:
 * the note increment above which the partial would pass DRW_FOLD */
static const uint32_t DRW_FOLD_LIM[DRW_NP] = {
    DRW_FOLD * 2u, DRW_FOLD / 3u * 2u, DRW_FOLD, DRW_FOLD / 2u, DRW_FOLD / 3u, DRW_FOLD / 4u, DRW_FOLD / 5u,
    DRW_FOLD / 6u, DRW_FOLD / 8u,
};
static inline uint32_t drw_ratio(uint32_t k, uint32_t x)
{
    switch (k) {
    case 0: return x >> 1;
    case 1: return x + (x >> 1);
    case 2: return x;
    case 3: return x << 1;
    case 4: return x * 3u;
    case 5: return x << 2;
    case 6: return x * 5u;
    case 7: return x * 6u;
    default: return x << 3;
    }
}

/* rotor speeds as phase increments per block (CTL samples): Hz * CTL / FS * 2^32 */
#define DRW_HZ(h) ((int32_t)((h) * 3116457.0))
static const int32_t DRW_ROT_SPD[2][3] = {
    {0, DRW_HZ(0.8), DRW_HZ(6.7)},   /* high rotor: OFF (stops), SLOW, FAST */
    {0, DRW_HZ(0.7), DRW_HZ(5.8)},   /* low rotor */
};

typedef struct {                 /* per part, once per block (wheel_block) */
    int32_t g[DRW_NP];           /* bar gains, Q15, normalised */
    int32_t pk, plev;            /* percussion: partial (DRW_NP = off), level Q15 */
    uint32_t pdec;               /* its decay per block, Q16 */
    int32_t clk;                 /* click level Q15 */
    uint32_t rph[2];             /* rotor phases (0 high, 1 low) */
    int32_t rsp[2];              /* rotor speeds (phase increment per block) */
    int32_t rwet;                /* rotary amount, Q15 (ramps on / off) */
    int32_t am[2];               /* this block's amplitude, Q15, per rotor (mono) */
    int32_t gam[2][2];           /* the two mics' amplitude per rotor: [rotor][0 L, 1 R], Q15 */
    int32_t fm[2];               /* this block's pitch offset, Q16 of the increment, per rotor */
} drw_trk_t;

typedef struct {                 /* per voice */
    uint32_t ph[DRW_NP];
    int32_t gp[DRW_NP];          /* each partial's gain at the end of the last block (the ramp start) */
    int32_t perc;                /* percussion envelope, Q15 */
    int32_t clk;                 /* click envelope, Q15 */
    int32_t nz, hp;              /* click noise state, its high-pass state */
    uint8_t gate, init;
} drw_vc_t;

static drw_trk_t drw_t[NPART];
static drw_vc_t drw_v[NPART][DRW_POLY];

static uint32_t drw_part(const track_t *t) { return (uint32_t)(t - trk) % NPART; }

/* the horn as it is turning now, for the UI (ui_pads.c: the Leslie pad breathes once a revolution).
 * The phase is the horn's own angle, so the blink is in step with the sound; the speed is the eased
 * one wheel_block is feeding the audio, so after a ROTR flip the pad ramps up / down with the ear
 * (tau 0.37 s) instead of jumping to the new mode's rate. Both are the high rotor: it is the one the
 * AM and the Doppler ride on, and the one a player hears speed up. */
uint32_t wheel_horn_phase(const track_t *t) { return drw_t[drw_part(t)].rph[0]; }
int32_t wheel_horn_mhz(const track_t *t)
{
    int32_t sp = drw_t[drw_part(t)].rsp[0];      /* phase increment per block: DRW_HZ(Hz) */
    return sp > 0 ? sp / 3117 : 0;               /* (3116457 per Hz, so 3117 per milli-Hz) */
}

/* the bar level 0..8 of partial k: its own param (P_E0..P_E8) */
static int32_t drw_level(const int16_t *p, uint32_t k)
{
    return clamp(p[P_E0 + k], 0, 8);
}

/* An old WHEEL sound's 8 engine values -> today's 12. Before the drawbar params the block was
 * {REG, SUB, BODY, TOP, PERC, CLICK, DRV, ROTR}: the nine bars were the REG registration plus the
 * SUB (16', 5 1/3'), BODY (8', 4') and TOP (the rest) trims, and DRIVE was the engine's own
 * overdrive (now the track's DIST, P_DIST: a load cannot reach it from here). Called by the
 * loaders when a record carries 8 WHEEL values (project.c, upreset.c). p: P_E0..P_E11. */
static void wheel_migrate(int16_t *p, uint32_t np)
{
    int16_t old[8];
    int32_t reg, sub, body, top;
    uint32_t k;
    if (np >= P_E0 + 12u)                         /* the record already carries all 12 engine values */
        return;
    for (k = 0; k < 8u; k++)
        old[k] = p[k];
    reg = clamp(old[0], 0, 15);
    sub = clamp(old[1], -8, 8);
    body = clamp(old[2], -8, 8);
    top = clamp(old[3], -8, 8);
    for (k = 0; k < DRW_NP; k++)
        p[k] = (int16_t)clamp(DRW_REG[reg][k] - '0' + (k < 2u ? sub : k < 4u ? body : top), 0, 8);
    p[9] = old[4];                                /* PERC */
    p[10] = old[5];                               /* CLICK */
    p[11] = old[7];                               /* ROTR */
    (void)old[6];                                 /* DRV: now the track's DIST */
}

/* once per block, before the voices: bar gains, percussion, click, the rotors */
static void wheel_block(track_t *t)
{
    drw_trk_t *T = &drw_t[drw_part(t)];
    const int16_t *p = t->p;
    uint32_t k, perc = (uint32_t)p[P_E9] % 7u, rot = (uint32_t)p[P_E11] % 3u;
    int32_t s = 0, lv[DRW_NP];
    uint32_t norm;
    for (k = 0; k < DRW_NP; k++) {
        lv[k] = perc && k == DRW_NP - 1u ? 0 : DRW_LV[drw_level(p, k)];   /* PERC on: the 1' bar is off */
        s += lv[k];
    }
    /* sum of the gains = 2.4 s / (s + 1.5) (s in units of a full bar): one bar 0.96, three 1.6,
     * all nine 2.06 */
    norm = (uint32_t)(2u * 39322u * 32768u) / (uint32_t)(s + 49152);
    if (perc == 1u || perc == 2u || perc == 5u || perc == 6u)
        norm = (norm * 23170u) >> 15;                   /* normal percussion: the bars -3 dB */
    for (k = 0; k < DRW_NP; k++)
        T->g[k] = (int32_t)(((uint32_t)lv[k] * norm) >> 15);
    T->pk = perc ? (int32_t)(3u + ((perc - 1u) & 1u)) : DRW_NP;   /* 2ND: 4', 3RD: 2 2/3' */
    T->plev = perc == 3u || perc == 4u ? 12000 : 24000;
    T->pdec = perc >= 5u ? 65457u : 65219u;             /* tau 0.6 s / 0.15 s */
    T->clk = p[P_E10] * 180;
    /* rotors: each speed eases to its target (high rotor tau ~0.37 s, low ~1.5 s) */
    for (k = 0; k < 2u; k++) {
        int32_t sp, c, sn, dep, t, ca;
        T->rsp[k] += (DRW_ROT_SPD[k][rot] - T->rsp[k]) >> (k ? 11 : 9);
        sp = T->rsp[k];
        T->rph[k] += (uint32_t)sp;
        sn = sine_i(T->rph[k]);
        c = sine_i(T->rph[k] + 0x40000000u);
        dep = k ? 7000 : 13000;                         /* AM depth: low rotor 21 %, high 40 % */
        T->am[k] = 32767 - mulq15(dep, (32767 - c) >> 1);
        T->fm[k] = (sn * (sp >> (k ? 17 : 16))) >> 15;  /* Doppler: +-0.5 % fast (high), +-0.2 % (low) */
        /* the stereo pair: two mics 60 deg off the front, so their AM curves are 120 deg apart and
         * each channel hears the rotor coming and going at a different moment. The depth is the
         * pair's (deeper than the mono curve, which is their mean): 55 % on the horn, 46 % on the
         * drum, the chop a stereo Leslie recording has. cos(p +- 60) = cos(p)/2 +- sin 60 sin(p):
         * the two mics cost one multiply, no extra table lookups. */
        ca = c >> 1;
        t = (sn * 28378) >> 15;                         /* sin 60 * sin(p), Q15 */
        dep = k ? DRW_MIC_DEP_LO : DRW_MIC_DEP_HI;
        T->gam[k][0] = 32767 - mulq15(dep, (32767 - (ca + t)) >> 1);   /* cos(p - 60) */
        T->gam[k][1] = 32767 - mulq15(dep, (32767 - (ca - t)) >> 1);   /* cos(p + 60) */
    }
    T->rwet += ((rot ? 32767 : 0) - T->rwet) >> 5;      /* ~25 ms in or out */
    for (k = 0; k < 2u; k++) {
        uint32_t ch;
        T->am[k] = 32767 - mulq15(T->rwet, 32767 - T->am[k]);
        T->fm[k] = mulq15(T->fm[k], T->rwet);
        for (ch = 0; ch < 2u; ch++)
            T->gam[k][ch] = 32767 - mulq15(T->rwet, 32767 - T->gam[k][ch]);
    }
}

static void wheel_note_on(track_t *t, voice_t *v)
{
    uint32_t vi = v->sslot % DRW_POLY, k, held = 0;
    drw_vc_t *V = &drw_v[drw_part(t)][vi];
    for (k = 0; k < VOICE_POOL; k++)                    /* single trigger: another key of the part held? */
        if (&vpool[k] != v && vpool[k].part == (uint8_t)(t - trk) && vpool[k].active &&
            vpool[k].gate && vpool[k].stage != 4u)
            held = 1;
    if (!V->init) {                                     /* the wheels: spread start phases, then free running */
        for (k = 0; k < DRW_NP; k++)
            V->ph[k] = (k * 0x9E3779B9u) ^ (vi * 0x7F4A7C15u);
        V->nz = 0x13579BDF + (int32_t)vi;
        V->init = 1;
    }
    V->perc = held ? 0 : 32767;
    V->clk = 32767;
    V->hp = 0;
    V->gate = 1;
}

/* One voice: acc is the mono sum, or (with side) the high rotor's and acc2 the low rotor's, so the
 * block's two mic gains can be applied once for all the partials of a rotor. side 0: the mono path,
 * sample for sample what it always was. */
static void wheel_voice(track_t *t, voice_t *v, int32_t *out, int32_t *side, uint32_t n, const vmod_t *m)
{
    const drw_trk_t *T = &drw_t[drw_part(t)];
    drw_vc_t *V = &drw_v[drw_part(t)][v->sslot % DRW_POLY];
    int32_t acc[CTL], acc2[CTL], perc1, clk0, clk1;
    uint32_t i, k;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        acc[i] = acc2[i] = 0;
    perc1 = (int32_t)(((uint32_t)V->perc * T->pdec) >> 16);
    for (k = 0; k < DRW_NP; k++) {
        uint32_t x = m->inc, inc, ph = V->ph[k];
        int32_t g1 = T->g[k], g0 = V->gp[k], r, *dst;
        if ((int32_t)k == T->pk)
            g1 += mulq15(T->plev, perc1);
        while (x > DRW_FOLD_LIM[k])                     /* fold back above ~6 kHz */
            x >>= 1;
        inc = drw_ratio(k, x);
        r = inc < DRW_XOVER;                            /* 1: the low rotor */
        if (!side)
            g1 = mulq15(g1, T->am[r]);                  /* mono: the rotor's one AM curve here */
        dst = side && r ? acc2 : acc;                   /* (in stereo the mic gains go on at the end) */
        inc += (uint32_t)((int32_t)(inc >> 16) * T->fm[r]);
        V->gp[k] = g1;
        if (!g0 && !g1) {
            V->ph[k] = ph + inc * n;
            continue;
        }
        if (g0 == g1) {
            for (i = 0; i < n; i++) {
                ph += inc;
                dst[i] += (SINE[ph >> 22] * g1) >> 15;
            }
        } else {                                        /* gain ramp over the block (CTL = 32) */
            int32_t gq = g0 << 5, dg = g1 - g0;
            for (i = 0; i < n; i++) {
                ph += inc;
                gq += dg;
                dst[i] += (SINE[ph >> 22] * (gq >> 5)) >> 15;
            }
        }
        V->ph[k] = ph;
    }
    V->perc = perc1;
    /* key click: high-passed noise, at the key contact and (half) at the release */
    if (V->gate && !v->gate) {
        V->gate = 0;
        V->clk = 16384;
    }
    clk0 = mulq15(V->clk, T->clk);
    if (clk0 > 16) {
        int32_t nst = V->nz, hp = V->hp, dc;
        V->clk = (V->clk * 3) >> 2;                     /* tau ~2.5 ms */
        clk1 = mulq15(V->clk, T->clk);
        dc = clk1 - clk0;
        clk0 <<= 5;
        for (i = 0; i < n; i++) {
            int32_t z = (int32_t)(noise32(&nst) >> 17) - 16384, y = z - hp;
            hp += y >> 3;
            clk0 += dc;
            acc[i] += (y * (clk0 >> 5)) >> 15;          /* (the horn: a real key click is spun too) */
        }
        V->nz = nst;
        V->hp = hp;
    }
    if (!side) {
        for (i = 0; i < n; i++)                         /* acc: Q15 with headroom to 4.0 */
            out[i] += voice_amp(acc[i] >> 1, m, i) << 1;
        return;
    }
    /* stereo: M = (L + R) / 2, S = (L - R) / 2 from the two rotors' sums and the mics' gains */
    {
        int32_t mh = (T->gam[0][0] + T->gam[0][1]) >> 1, sh = (T->gam[0][0] - T->gam[0][1]) >> 1;
        int32_t ml = (T->gam[1][0] + T->gam[1][1]) >> 1, sl = (T->gam[1][0] - T->gam[1][1]) >> 1;
        for (i = 0; i < n; i++) {
            int32_t h = acc[i] >> 1, l = acc2[i] >> 1;
            int32_t mo = ((h * mh) >> 15) + ((l * ml) >> 15);
            int32_t so = ((h * sh) >> 15) + ((l * sl) >> 15);
            out[i] += voice_amp(mo, m, i) << 1;
            side[i] += voice_amp(so, m, i) << 1;
        }
    }
}

static void wheel_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    wheel_voice(t, v, out, 0, n, m);
}

static void wheel_render_stereo(track_t *t, voice_t *v, int32_t *out, int32_t *side, uint32_t n,
                                const vmod_t *m)
{
    wheel_voice(t, v, out, side, n, m);
}

static const preset_t WHEEL_PRESETS[] = {
    /* name, {9 drawbars 16'..1', PERC, CLICK, ROTR}, {A D S R}, fenv, mono, fx, pat */
    {"FULL ORGAN", {8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 30, 1}, {0, 64, 127, 45}, 0, 0, FX(20, 0, 10, 35), PAT(5)},
    {"JAZZ PERC", {8, 8, 8, 0, 0, 0, 0, 0, 0, 2, 50, 1}, {0, 64, 127, 40}, 0, 0, FX(8, 0, 0, 25), PAT(3)},
    {"GOSPEL", {8, 8, 8, 6, 0, 0, 0, 0, 6, 1, 60, 2}, {0, 64, 127, 45}, 0, 0, FX(40, 0, 0, 40), PAT(6)},
    {"SOFT FLUTE", {0, 0, 6, 2, 0, 0, 0, 0, 0, 0, 10, 1}, {0, 64, 127, 55}, 0, 0, FX(0, 20, 15, 55), PAT(5)},
    {"ROCK DRIVE", {8, 8, 8, 0, 0, 0, 8, 8, 8, 0, 70, 2}, {0, 64, 127, 40}, 0, 0, FX(100, 0, 10, 25), PAT(4)},
};

static const engine_t ENG_WHEEL = {
    .name = "WHEEL",
    .cost = 86,          /* (preset total - 307 idle) / 8 voices: tests/cpu_baseline.txt */
    .page_title = {"BARS", "TONE"},
    .edit = {
        {"16'", F_INT, 0, 8, 8, 0, 0},              /* the nine drawbars: one param each (P5) */
        {"5.3'", F_INT, 0, 8, 0, 0, 0},
        {"8'", F_INT, 0, 8, 8, 0, 0},
        {"4'", F_INT, 0, 8, 0, 0, 0},
        {"2.7'", F_INT, 0, 8, 0, 0, 0},
        {"2'", F_INT, 0, 8, 0, 0, 0},
        {"1.6'", F_INT, 0, 8, 0, 0, 0},
        {"1.3'", F_INT, 0, 8, 0, 0, 0},
        {"1'", F_INT, 0, 8, 0, 0, 0},
        {"PERC", F_ENUM, 0, 6, 0, N_DRW_PERC, 0},
        {"CLICK", F_PCT, 0, 127, 40, 0, 0},
        {"ROTR", F_ENUM, 0, 2, 1, N_DRW_ROTR, 0},
    },
    .presets = WHEEL_PRESETS,
    .npresets = NELEM(WHEEL_PRESETS),
    .note_on = wheel_note_on,
    .render = wheel_render,
    .render_stereo = wheel_render_stereo,             /* P5f: the rotary in stereo (L/R mics) */
    .knob = {P_E0, P_E1, P_E2, P_E3},               /* HOME SOUND: 16' 5 1/3' 8' 4' (P5: 5 knobs) */
    .block = wheel_block,
    .poly = DRW_POLY,                               /* 9 partials of state a voice, so it keeps its own cap */
};
_Static_assert(DRW_POLY <= NVOICE, "WHEEL poly beyond the shared budget");
