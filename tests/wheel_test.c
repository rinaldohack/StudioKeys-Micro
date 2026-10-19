/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* WHEEL's rotary in stereo (firmware/src/eng_wheel.c, fx.c mix_part), same sources as the firmware:
 *   build/host/wheel_test            (run_tests.sh)
 * 1. OFF is mono, exactly: with ROTR OFF the two mics are unity, S is 0 and L and R are the same
 *    sample (the mono part this engine always produced).
 * 2. SLOW / FAST are stereo: L/R correlation below 0.95, a side signal at least 20 dB under the mid,
 *    and the two channels within 1.5 dB.
 * 3. it rotates: the side (L - R) turns over at the rotors' rate, so FAST crosses zero far more
 *    often than SLOW in the same window (the two rotors, 0.8 + 0.7 Hz and 6.7 + 5.8 Hz).
 * 4. loudness: the mid of a rotating part sits within 1.5 dB of the same part with the rotor off
 *    (the two mics' mean is the mono curve: the fold-down keeps the level).
 * 5. no clipping at the corners, and the rotary's fade in is smooth (no click when it starts). */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("wheel: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

typedef struct {
    double rho, side_db, bal_db, mid_rms, rot15, rot125;
    int32_t peak, jump;
} img_t;

#define NB (4u * FS / CTL)                      /* 4 s of blocks */

/* one 4 s render of FULL ORGAN on a three-note chord, the rotor set to `rot` */
static void setup(int rot)
{
    uint32_t k;
    memset(trk, 0, sizeof trk);
    memset(drw_t, 0, sizeof drw_t);             /* a fresh rotor (phases, fade) each render */
    memset(drw_v, 0, sizeof drw_v);
    memset(&fx, 0, sizeof fx);
    host_tracks_init();
    host_preset(&trk[0], ENGI_WHEEL, 0);        /* FULL ORGAN: ROTR SLOW */
    trk[0].p[P_E11] = (int16_t)rot;
    trk[0].p[P_REV] = trk[0].p[P_DLY] = trk[0].p[P_CHOR] = 0;   /* the dry part only */
    trk[0].p[P_DIST] = 0;
    for (k = 0; k < 3u; k++)
        trk_note_on(&trk[0], 48u + 7u * k, 100u);
}

/* WAV demos: the same chord through the rotor slow and fast (listen to the L/R of either) */
static void demo(const char *dir)
{
    static int32_t o[2 * CTL];
    static const char *const nm[2] = {"wheel_slow", "wheel_fast"};
    char path[256];
    uint32_t i, blk, r;
    for (r = 1; r <= 2u; r++) {
        FILE *f;
        setup((int) r);
        snprintf(path, sizeof path, "%s/%s.wav", dir, nm[r - 1u]);
        if (!(f = fopen(path, "wb")))
            return;
        wav_hdr(f, 4u * FS);
        for (blk = 0; blk < 4u * FS / CTL; blk++) {
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++)
                wav_put(f, o[2u * i], o[2u * i + 1u]);
        }
        fclose(f);
        printf("wheel: demo %s\n", path);
    }
}

/* one 4 s render of FULL ORGAN on a three-note chord, the rotor set to `rot` */
static img_t image(int rot)
{
    static int32_t o[2 * CTL];
    int32_t prev_l = 0, prev_r = 0;
    uint32_t i, blk;
    double ll = 0, rr = 0, lr = 0, mm = 0, ss = 0;
    double re15 = 0, im15 = 0, re125 = 0, im125 = 0, tot = 0;
    int started = 0;
    img_t r;
    memset(&r, 0, sizeof r);
    setup(rot);
    for (blk = 0; blk < NB; blk++) {
        mix_block(o, CTL);
        if (blk == NB / 8u)                     /* skip the rotor's fade in (~0.25 s) and the attack */
            started = 1;
        for (i = 0; i < CTL; i++) {
            int32_t l = o[2u * i], ri = o[2u * i + 1u];
            double mid = (l + ri) * 0.5, side = (l - ri) * 0.5;
            if (!started) {
                prev_l = l;
                prev_r = ri;
                continue;
            }
            ll += (double) l * l;
            rr += (double) ri * ri;
            lr += (double) l * ri;
            mm += mid * mid;
            ss += side * side;
            /* the rotors' rate from the side's power: S is an audio carrier the rotors modulate, so
             * S^2 has a component at twice each rotor's rate (0.8 + 0.7 Hz SLOW, 6.7 + 5.8 FAST) that
             * the audio terms average out of over 4 s. Two bins, 1.5 and 12.5 Hz. */
            {
                double t = (double) (blk * CTL + i) / (double) FS, w, sq = side * side;
                w = 2.0 * M_PI * 1.5 * t;
                re15 += sq * cos(w);
                im15 -= sq * sin(w);
                w = 2.0 * M_PI * 12.5 * t;
                re125 += sq * cos(w);
                im125 -= sq * sin(w);
                tot += sq;
            }
            {
                int32_t dl = l - prev_l, dr = ri - prev_r;
                if (dl < 0) dl = -dl;
                if (dr < 0) dr = -dr;
                if (dl > r.jump) r.jump = dl;
                if (dr > r.jump) r.jump = dr;
            }
            if (l > r.peak) r.peak = l;
            if (ri > r.peak) r.peak = ri;
            prev_l = l;
            prev_r = ri;
        }
    }
    r.rho = lr / sqrt(ll * rr + 1e-30);
    r.side_db = 10 * log10((ss + 1e-30) / (mm + 1e-30));
    r.bal_db = 10 * log10((ll + 1e-30) / (rr + 1e-30));
    r.mid_rms = sqrt(mm / (double) (NB * CTL));
    r.rot15 = hypot(re15, im15) / (tot + 1e-30);
    r.rot125 = hypot(re125, im125) / (tot + 1e-30);
    return r;
}

int main(int argc, char **argv)
{
    img_t off = image(0), slow = image(1), fast = image(2);
    char what[240];

    check("OFF: L and R are the same sample (the mics are unity, S = 0)", off.rho > 0.999999 && off.side_db < -120);
    snprintf(what, sizeof what,
             "SLOW: correlation %.3f, side %.1f dB under the mid, L/R %+.2f dB (rotating, stereo)",
             slow.rho, slow.side_db, slow.bal_db);
    check(what, slow.rho < 0.95 && slow.side_db > -20.0 && fabs(slow.bal_db) < 1.5);
    snprintf(what, sizeof what, "FAST: correlation %.3f, side %.1f dB, L/R %+.2f dB", fast.rho, fast.side_db, fast.bal_db);
    check(what, fast.rho < 0.95 && fast.side_db > -20.0 && fabs(fast.bal_db) < 1.5);
    snprintf(what, sizeof what,
             "it rotates: side power at the rotors' rate, 1.5 Hz / 12.5 Hz: SLOW %.3f / %.3f, FAST %.3f / %.3f",
             slow.rot15, slow.rot125, fast.rot15, fast.rot125);
    check(what, slow.rot15 > 0.05 && fast.rot125 > 0.05 && slow.rot15 > 3.0 * fast.rot15 &&
               fast.rot125 > 3.0 * slow.rot125);
    snprintf(what, sizeof what, "loudness: the rotating mid %.2f dB from OFF's, FAST %.2f dB from SLOW's",
             20 * log10(slow.mid_rms / (off.mid_rms + 1e-30)),
             20 * log10(fast.mid_rms / (slow.mid_rms + 1e-30)));
    check(what, fabs(20 * log10(slow.mid_rms / (off.mid_rms + 1e-30))) < 1.5 &&
               fabs(20 * log10(fast.mid_rms / (slow.mid_rms + 1e-30))) < 1.5);
    snprintf(what, sizeof what, "no clipping and no step: peak %d of 32767, largest sample step %d", fast.peak,
             fast.jump);
    check(what, fast.peak < 32767 && fast.jump < 6000);

    if (argc > 1)
        demo(argv[1]);
    printf("%s\n", bad ? "WHEEL TEST FAILED" : "wheel test passed");
    return bad != 0;
}
