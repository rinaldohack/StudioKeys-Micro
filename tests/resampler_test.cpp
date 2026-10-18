/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The bridge between the firmware's 44.1 kHz render and the DAW's rate (plugin/StudioKEYS/Resampler.h).
 *
 * What it has to prove, because the old linear interpolator failed both:
 *   1. flat passband - linear is sinc^2, -1.5 dB at 10 kHz and -3.4 dB at 15 kHz, so the VST sounded
 *      duller than the keyboard it mirrors;
 *   2. images / aliases out of band, so a 44.1 edit at 48 kHz (or a 44.1 render at 96) is clean;
 *   3. no pitch error and no drift - the output frequency is the input frequency whatever the ratio;
 *   4. block-size independence: the audio must not depend on how the host chops it, and repeated
 *      small blocks must not leak (the old code resynced and dropped samples when it fell behind);
 *   5. a native 44.1 session is bit-identical, so nothing colours the sound when no conversion is needed.
 *
 *   cc -O2 -o build/host/resampler_test tests/resampler_test.cpp -lm */
#include "../plugin/StudioKEYS/Resampler.h"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

static int bad;
static void check(const char *what, int ok, const char *detail = "")
{
    std::printf("resampler: %-72s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    bad += !ok;
}

/* ---- a signal source: one sine (or two) at a fixed input rate, rendered on demand ---- */
struct Tone
{
    double hz, hz2 = 0.0, amp2 = 0.0;
    double phase = 0.0, phase2 = 0.0;
    long produced = 0;

    void Fill(float *l, float *r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            double v = std::sin(phase);
            if (hz2 > 0.0)
                v += amp2 * std::sin(phase2);
            phase += 2.0 * 3.14159265358979323846 * hz / 44100.0;
            phase2 += 2.0 * 3.14159265358979323846 * hz2 / 44100.0;
            if (phase > 1e9)
                phase -= 2.0 * 3.14159265358979323846 * std::floor(phase / (2.0 * 3.14159265358979323846));
            l[i] = r[i] = (float) v;
        }
        produced += n;
    }
};

/* Amplitude of the output at `hz` (Goertzel-free: correlate against the output rate's own sine). */
static double Measure(double outRate, double hz, const std::vector<float> &y, size_t skip)
{
    const double w = 2.0 * 3.14159265358979323846 * hz / outRate;
    double re = 0.0, im = 0.0;
    size_t n = 0;
    for (size_t i = skip; i < y.size(); ++i, ++n)
    {
        re += y[i] * std::cos(w * (double) (i - skip));
        im += y[i] * std::sin(w * (double) (i - skip));
    }
    return n ? 2.0 * std::sqrt(re * re + im * im) / (double) n : 0.0;
}

static double Rms(const std::vector<float> &y, size_t skip)
{
    double s = 0.0;
    size_t n = 0;
    for (size_t i = skip; i < y.size(); ++i, ++n)
        s += (double) y[i] * y[i];
    return n ? std::sqrt(s / (double) n) : 0.0;
}

/* Run the resampler over `outFrames` of output in blocks of `block`; returns the left channel. */
static std::vector<float> Run(double outRate, Tone &tone, int outFrames, int block)
{
    SkResampler rs;
    rs.Configure(44100.0, outRate);
    std::vector<float> y((size_t) outFrames);
    std::vector<float> r((size_t) outFrames);
    auto fill = [&tone](float *l, float *rr, int n) { tone.Fill(l, rr, n); };
    for (int at = 0; at < outFrames; at += block)
    {
        const int chunk = outFrames - at < block ? outFrames - at : block;
        rs.Process(fill, y.data() + at, r.data() + at, chunk);
    }
    return y;
}

/* What the previous code did, for the comparison: linear interpolation is sinc^2, so its droop at hz
 * is 20log10(sinc(hz/44100)^2) - the number the new kernel has to beat. */
static double LinearDb(double outRate, double hz)
{
    (void) outRate;
    const double s = std::sin(3.14159265358979323846 * hz / 44100.0) /
                     (3.14159265358979323846 * hz / 44100.0);
    return 20.0 * std::log10(s * s);
}

int main()
{
    const double RATES[] = {48000.0, 88200.0, 96000.0, 192000.0};
    const double BAND[] = {200.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0};

    /* 1. passband: every rate, every frequency, within 0.5 dB (linear would be 1.1 dB at 5 k, 3.4 at 15 k) */
    for (double out : RATES)
    {
        char line[128];
        double worst = 0.0;
        for (double hz : BAND)
        {
            Tone t;
            t.hz = hz;
            std::vector<float> y = Run(out, t, (int) (out * 0.5), 512);
            const double db = 20.0 * std::log10(Measure(out, hz, y, y.size() / 4));   /* peak, the tone's is 1 */
            if (std::fabs(db) > std::fabs(worst))
                worst = db;
        }
        std::snprintf(line, sizeof line, "worst %.2f dB, linear would be %.2f dB at 15 kHz", worst,
                      LinearDb(out, 15000.0));
        check("passband flat within 0.5 dB from 200 Hz to 18 kHz", std::fabs(worst) < 0.5, line);
    }

    /* 2. images. An interpolator's images of a tone sit at the input rate +- the tone; nothing may fold
     *    back into the audible band, and the in-band floor must be far below the signal. */
    {
        Tone t;
        t.hz = 8000.0;
        std::vector<float> y = Run(48000.0, t, 48000, 512);
        const size_t skip = y.size() / 4;
        const double want = Measure(48000.0, 8000.0, y, skip);
        const double total = Rms(y, skip);
        const double rest = std::sqrt(std::fmax(0.0, total * total - want * want / 2.0));
        const double db = 20.0 * std::log10(want / std::sqrt(2.0) / (rest + 1e-12));
        check("an 8 kHz tone upsampled: in-band floor 80 dB below it", db > 80.0,
              std::to_string((int) db).c_str());
    }
    {
        /* 20 kHz is 0.907 of the source Nyquist: its image lands at 24.1 kHz, which is 0.1 kHz above
         * the output's Nyquist, so removing it needs a brick wall (~58 taps for 75 dB). What matters
         * audibly is that nothing folds *down*: the band below the tone must stay empty. The image
         * itself only has to be well clear of the tone, since no one hears 24.1 kHz. */
        Tone t;
        t.hz = 20000.0;
        std::vector<float> y = Run(48000.0, t, 48000, 512);
        const size_t skip = y.size() / 4;
        const double want = Measure(48000.0, 20000.0, y, skip);
        const double total = Rms(y, skip);
        const double rest = std::sqrt(std::fmax(0.0, total * total - want * want / 2.0));
        const double db = 20.0 * std::log10(want / std::sqrt(2.0) / (rest + 1e-12));
        check("a 20 kHz tone: its 24.1 kHz image is 25 dB down (linear: 0)", db > 25.0,
              std::to_string((int) db).c_str());

        const double below = Measure(48000.0, 18500.0, y, skip);
        char line[96];
        std::snprintf(line, sizeof line, "%.0f dB below the tone", 20.0 * std::log10(want / (below + 1e-12)));
        check("a 20 kHz tone leaves nothing below it (no fold-down)", below < want * 0.001, line);
    }

    /* 3. pitch: the output frequency is exactly the input frequency (no ratio drift over 2 s) */
    for (double out : RATES)
    {
        Tone t;
        t.hz = 1000.0;
        std::vector<float> y = Run(out, t, (int) (out * 2.0), 512);
        const size_t skip = y.size() / 8;
        size_t n = y.size() - skip;
        const double expect = 1000.0;
        /* measure at +-1 % of the expected frequency: the peak must be at 1000, not beside it */
        const double on = Measure(out, expect, y, skip);
        const double off = Measure(out, expect * 1.01, y, skip);
        char line[96];
        std::snprintf(line, sizeof line, "1000 Hz: on %.3f, +1%% %.3f (n %zu)", on, off, n);
        check("a 1 kHz tone comes out at 1 kHz (no pitch error)", on > 0.68 && off < on * 0.2, line);
    }

    /* 4. block size: the audio does not depend on how the host chops it, and it does not leak */
    {
        Tone a, b;
        a.hz = b.hz = 440.0;
        std::vector<float> big = Run(48000.0, a, 48000, 48000);
        std::vector<float> small = Run(48000.0, b, 48000, 37);   /* a nasty, non-power-of-two block */
        double diff = 0.0, ref = 0.0;
        const size_t skip = big.size() / 4;
        for (size_t i = skip; i < big.size(); ++i)
        {
            diff += (big[i] - small[i]) * (big[i] - small[i]);
            ref += big[i] * big[i];
        }
        const double db = 10.0 * std::log10((ref + 1e-20) / (diff + 1e-20));
        char line[96];
        std::snprintf(line, sizeof line, "%.0f dB below the signal", db);
        check("one 48000 block and 1297 blocks of 37 give the same audio", db > 80.0, line);
    }

    /* 5. downsampling: band-limit below the output Nyquist instead of aliasing. 32 kHz is rare but
     *    the linear code aliased here at full level: an 18 kHz tone folded straight onto 14 kHz. */
    {
        Tone t;
        t.hz = 18000.0;                       /* above 32 kHz's 16 kHz Nyquist -> folds to 14 kHz */
        std::vector<float> y = Run(32000.0, t, 32000, 512);
        const double total = Rms(y, y.size() / 4);
        char line[96];
        std::snprintf(line, sizeof line, "residual %.1f dB (linear interpolator: 0 dB)", 20.0 * std::log10(total + 1e-12));
        check("an 18 kHz tone downsampled to 32 kHz is rejected (no alias)", total < 0.02, line);
    }

    /* 6. native rate: bit-identical, so a 44.1 session hears the render, not the resampler */
    {
        SkResampler rs;
        rs.Configure(44100.0, 44100.0);
        check("44.1 kHz in and out is a bypass", rs.Bypass());
        Tone t;
        t.hz = 997.0;
        std::vector<float> in(2048), outL(2048), outR(2048);
        auto fill = [&t](float *l, float *r, int n) { t.Fill(l, r, n); };
        rs.Process(fill, outL.data(), outR.data(), 2048);
        bool same = true;
        for (int i = 0; i < 2048; ++i)
            same = same && outL[i] == (float) std::sin(2.0 * 3.14159265358979323846 * 997.0 * i / 44100.0);
        check("the bypass copies the render unchanged", same);
    }

    /* 7. cost: this runs on the audio thread, so the kernel stays short - report it and catch a
     *    regression to something quadratic. 24 taps, two phase rows, two channels. */
    {
        Tone t;
        t.hz = 440.0;
        SkResampler rs;
        rs.Configure(44100.0, 96000.0);
        std::vector<float> l(256), r(256);
        auto fill = [&t](float *ll, float *rr, int n) { t.Fill(ll, rr, n); };
        const int blocks = 4000;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i)
            rs.Process(fill, l.data(), r.data(), 256);
        auto t1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        const double per = ns / (double) (blocks * 256);
        char line[96];
        std::snprintf(line, sizeof line, "%.1f ns per output sample at 96 kHz", per);
        check("cost stays under 500 ns per output sample", per < 500.0, line);
    }

    std::printf("resampler: %s (%d failure%s)\n", bad ? "FAILED" : "all passed", bad, bad == 1 ? "" : "s");
    return bad != 0;
}
