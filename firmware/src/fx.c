/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb). The chorus and the delay are mono buses, mixed into
 * both channels; the dry mix is stereo; the reverb bus is stereo from one mono tank: its side signal
 * S rides on top of the tank M (L = M + S, R = M - S), so the mono sum L + R is exactly the tank and
 * a mono fold-down never combs (see rev_room / rev_spring). */
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
} fx;

/* SPRING (G_RTYPE 1): one spring of a spring tank, mono like the other buses, in the ROOM's own buffers (no
 * RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass) ->
 * SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain (straight into tanh over the full band, it would sound like a
 * broken digital fuzz). State per part (track_t dist_*). */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* MENU > USB LEVEL FIXED (for #42: record over USB with the speaker turned down): the mix goes to master_out at the
 * full MASTER level, USB audio takes that (audio.c uac_tap), and only then does MASTER scale what the DAC gets
 * (usb_fixed_dac). MASTER (0, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after uac_tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    for (i = 0; i < 2u * n; i++)
        out[i] = (out[i] * m) >> 12;                /* (|out| <= 32767 after the soft clip: fits) */
}
static volatile uint8_t fx_lowcut;     /* settings: 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the small speaker):
                                        * 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset of up
 * to +-31 at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+: what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is clipped at its
 * own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and added.
 * The low-pass has 4 poles (#42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself, late,
 * and cancelled up to 6 dB of it; now under 0.5 dB) */
static int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;
static inline int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    sb_lp1 += ((m - sb_lp1) * 692) >> 15;
    sb_lp2 += ((sb_lp1 - sb_lp2) * 692) >> 15;
    sb_lp3 += ((sb_lp2 - sb_lp3) * 692) >> 15;
    sb_lp4 += ((sb_lp3 - sb_lp4) * 692) >> 15;
    a = sb_lp4 < 0 ? -sb_lp4 : sb_lp4;
    if (a > sb_env)
        sb_env += (a - sb_env) >> 2;
    else if (sb_env > 0)
        sb_env -= (sb_env >> 11) + 1;
    t = clamp(sb_lp4 * 8, -sb_env, sb_env);
    sb_h1 += (t - sb_h1) >> 5;
    u = t - sb_h1;
    sb_h2 += (u - sb_h2) >> 5;
    u -= sb_h2;
    sb_hl += (u - sb_hl) >> 3;
    return sb_hl * 3;
}

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        uint32_t sh = fx_lowcut == 2u ? 5u : 6u;    /* rounded step stopped at |x - lc| < 32: an offset) */
        int32_t b = fx_lowcut == 2u ? spk_bass((*l + *r) >> 1) : 0;
        *l = lowcut1(*l, &lc_l1, &lce[0], sh);
        *l = lowcut1(*l, &lc_l2, &lce[1], sh) + b;
        *r = lowcut1(*r, &lc_r1, &lce[2], sh);
        *r = lowcut1(*r, &lc_r2, &lce[3], sh) + b;
    }
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = softclip(*l);
    *r = softclip(*r);
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    uint32_t bpm = song.g[G_BPM] > 0 ? (uint32_t)song.g[G_BPM] : 120u;
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / bpm;
}
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

#include "perform.c"                                 /* the FX hold layer's effects (the master) */

static uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like) form one mono tank M — the same
 * signal this bus always produced — and the combs' spread gives a side signal S:
 *   D = o0 - o1 + o2 - o3   (the parity split: the combs are near-equal in power, so E[M D] is
 *                            small and the two channels stay level — measured +0.09 dB)
 *   L = M + S, R = M - S        (S = 1.19 D: about as much side as a Freeverb's two comb banks)
 * The two channels are therefore one tank heard with two different comb balances (as Freeverb's
 * two sets of combs do, from a quarter of the memory), and L + R cancels S exactly: a mono
 * fold-down is the old mono bus, no comb filtering and nothing lost.
 * Reads 4 extra multiplies per sample; no extra RAM. */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out_l, int32_t *out_r, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0, d = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB: level as before the allpass fix */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            d += (k & 1u) ? -o : o;                     /* Q1: o0 - o1 + o2 - o3 (the parity split) */
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v is a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        d = (d * 19) >> 4;                              /* S = 1.1875 D (d < 4 * 32767: no overflow) */
        out_l[i] += a + d;
        out_r[i] += a - d;
    }
}

/* SPRING (see the top): stereo out. A real tank holds two or three springs; this is one loop (the
 * RAM of one) read at two places — the far end at half its length (the interpolated tap, with the
 * wobble) and a second pickup at three quarters — which is a second spring of a different length
 * seen through the same tank. Their sum is the mono spring this bus always was (the tank):
 *   M = 4 A + 2 B,  S = A - 2 B  (with A and B near-equal in power and little correlated,
 *                                 E[M S] is small: the two channels stay level, measured -0.66 dB)
 *   L = M + S = 5 A,  R = M - S = 3 A + 4 B      (L + R = 2 M: mono fold-down unchanged)
 * A and B are half a loop apart, so the two channels comb differently: the tank's chirp and decay on
 * both, two different colourations across them. */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out_l, int32_t *out_r, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        {
            int32_t ta = t0 + (((t1 - t0) * f) >> 8);   /* the far end (the tank's own pre-delay) */
            int32_t tb = ln[(wp - (uint32_t)L3) & SP_MASK];
            int32_t m = ta * 4 + tb * 2, sd = ta - tb * 2;
            out_l[i] += m + sd;
            out_r[i] += m - sd;
        }
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)          /* (int16: 997 of them, an odd count the int32 view misses one of) */
        rev_u.ap[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
}

static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */
static int32_t fade_buf[CTL];                            /* the right channel of that fade (part_buf: the left) */

/* process the three buses for one block; sends in, wet stereo out. The chorus and the delay are mono
 * and go to both channels; the reverb returns its own two channels (rev_room / rev_spring). */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in,
                     int32_t *wet_l, int32_t *wet_r, uint32_t n)
{
    uint32_t i, dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t cdepth = song.g[G_CDEPTH] * 6, rt;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    for (i = 0; i < n; i++) {
        int32_t y = 0, x, r;
        /* chorus: modulated short delay, 5..15 ms */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(fx.cho_ph) + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        fx.cho_w++;
        /* delay with a low-passed feedback */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        y += mulq15(x << 1, dmix);
        wet_l[i] = y;
        wet_r[i] = y;
    }
    rt = song.g[G_RTYPE] == 1;
    if (rt != fx.rtype) {                               /* the model changed: the old one's block fades out, */
        int32_t *tl = part_buf, *tr = fade_buf, g = 65536;   /* its buffers are cleared, the new */
        int32_t d = 65536 / (int32_t)n;                      /* one starts from silence */
        for (i = 0; i < n; i++)
            tl[i] = tr[i] = 0;
        if (fx.rtype)
            rev_spring(rev_in, tl, tr, n);
        else
            rev_room(rev_in, tl, tr, n);
        for (i = 0; i < n; i++, g -= d) {
            wet_l[i] += mulq16(tl[i], (uint32_t)g);
            wet_r[i] += mulq16(tr[i], (uint32_t)g);
        }
        rev_clear();
        fx.rtype = (uint8_t)rt;
        return;
    }
    if (rt)
        rev_spring(rev_in, wet_l, wet_r, n);
    else
        rev_room(rev_in, wet_l, wet_r, n);
}

/* one block of the whole mix (shared with hostsim.c): events -> each part (with its modulation matrix)
 * -> dist -> SLICER -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet_l[CTL], wet_r[CTL], mix_l[CTL], mix_r[CTL];
static int32_t part_side[CTL];                           /* the part's stereo side signal (S = L - R) */

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
static void mix_part(track_t *t, uint32_t n)
{
    int32_t *b = part_buf;
    /* P5f: WHEEL's rotary is the one engine with a stereo image of its own: it renders M (out) and
     * S (side), and the part goes into the mix as M + S / M - S. Every other engine, and WHEEL with
     * the rotor off, leaves sd 0 and the loop below is the mono one, sample for sample. */
    int32_t *sd = ENGINES[t->engine]->render_stereo ? part_side : 0;
    uint32_t i;
    mod_begin(t);                                       /* the matrix's per-block values into t->p (mod.c) */
    if (track_render(t, b, sd, n))
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (mod.on)
            mod_end(t);
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
        int32_t xmax = c > d ? c : d;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
        if ((pf.mute >> (t - trk)) & 1u) {
            perf_mute((uint32_t)(t - trk), b, n);       /* perform.c: a black key in the FX layer */
            if (sd)
                for (i = 0; i < n; i++)
                    sd[i] = 0;                          /* (and its stereo sides, or a mute would leak them) */
        }
        for (i = 0; i < n; i++) {
            int32_t x = ((b[i] >> 2) * lvl) >> 10;      /* pre-shift: 8 loud voices */
            int32_t s2 = sd ? ((sd[i] >> 2) * lvl) >> 10 : 0;
            int32_t li = x + s2, ri = x - s2;
            int32_t a = li < 0 ? -li : li, ar = ri < 0 ? -ri : ri;
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow (the mono sum) */
            if (ar > a)
                a = ar;
            if (a > pk)
                pk = a;
            if (c)
                send_c[i] += mulq15(xs, c);
            if (d)
                send_d[i] += mulq15(xs, d);
            if (r)
                send_r[i] += mulq15(xs, r);
            mix_l[i] += (li * gl) >> 12;
            mix_r[i] += (ri * gr) >> 12;
        }
        t->peak = pk;
    }
    if (mod.on)
        mod_end(t);                                     /* the stored values back */
}

/* the master with the FX layer's effects between its level and master_out (perform.c) */
static __attribute__((noinline)) void perf_master(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    for (i = 0; i < n; i++) {
        mix_l[i] = (((mix_l[i] + wet_l[i]) >> 2) * mg) >> 10;
        mix_r[i] = (((mix_r[i] + wet_r[i]) >> 2) * mg) >> 10;
    }
    perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        int32_t l = mix_l[i], r = mix_r[i];
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}

/* ------------------------------------------------- P14: metronome --- */
/* A short high tick on the master once per beat (G_BPM, or the measured external clock). MENU > AUDIO
 * holds its level (0 = off, 1..4) and the 1-bar count-in; settings_persist.c restores both. The tick
 * rides after the master level (a monitoring aid, not part of the mix) and audio.c's REMOTE OUT mute
 * silences it with everything else. */
#define METRO_HZ 2200u                              /* the tick's pitch: high enough to cut through */
static uint8_t metro_level;                         /* 0 = off, 1..4 = the click level */
static uint8_t metro_countin;                       /* 1: a bar of clicks before a take */
static uint32_t clk_left, clk_ph, clk_amp, clk_dec; /* the tick sounding now */

static void metro_set(uint32_t level, uint32_t countin)
{
    metro_level = (uint8_t)(level % 5u);
    metro_countin = countin ? 1u : 0u;
    if (!metro_level)
        clk_left = 0;
}

/* the count-in only makes sense with the click audible */
static int metro_countin_on(void) { return metro_level && metro_countin; }

static void metro_click(uint32_t accent)
{
    uint32_t len;
    if (!metro_level)
        return;
    len = (uint32_t)FS / 80u;                       /* ~12.5 ms */
    clk_left = len;
    clk_ph = 0;
    clk_amp = (accent ? 1500u : 1000u) * (uint32_t)metro_level;   /* Q15: 6000 at level 4 */
    clk_dec = clk_amp / len;
}

/* mix the tick into the master (Q15, before audio.c's 24-bit shift) */
static void metro_out(int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && clk_left; i++, clk_left--) {
        int32_t s = (clk_ph & 0x8000u) ? (int32_t)clk_amp : -(int32_t)clk_amp;
        clk_ph += (uint32_t)METRO_HZ * 65536u / (uint32_t)FS;
        if (clk_amp > clk_dec)
            clk_amp -= clk_dec;
        else
            clk_amp = 0;
        if (!clk_amp)
            continue;
        out[2u * i] = clamp(out[2u * i] + s, -32768, 32767);
        out[2u * i + 1u] = clamp(out[2u * i + 1u] + s, -32768, 32767);
    }
}

static void mix_block(int32_t *out, uint32_t n){
    uint32_t i;
    int perf;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    events_block(n);
    perf = perf_begin(n);                               /* the FX hold layer at work (perform.c) */
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
    if (perf)
        perf_pre(mix_l, mix_r, send_d, send_r, n);
    fx_buses(send_c, send_d, send_r, wet_l, wet_r, n);
    if (perf) {
        perf_master(out, n);
        metro_out(out, n);                              /* P14: the click is not part of the FX layer */
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t l = (((mix_l[i] + wet_l[i]) >> 2) * mg) >> 10;
        int32_t r = (((mix_r[i] + wet_r[i]) >> 2) * mg) >> 10;
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
    metro_out(out, n);
}
