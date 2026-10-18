/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

/** The bridge between the firmware's fixed 44.1 kHz render and the DAW's own rate.
 *
 * Why not linear interpolation: linear is sinc^2, so it is 1.5 dB down at 10 kHz and 3.4 dB down at
 * 15 kHz - the plug-in sounded duller than the keyboard it mirrors, and a 44.1 edit heard at 48 kHz
 * lost its top octave. This is a windowed-sinc (Kaiser) polyphase interpolator with linear
 * interpolation between phase rows, so the passband is flat and the images sit below the noise.
 *
 * Pull-based: the caller supplies a fill functor, because the source (sk_host_render_f32) renders on
 * demand and must not be driven past what the block needs.
 */
class SkResampler
{
public:
  static constexpr int kPhases = 128;        /* phase rows per input sample (one interpolated between two) */
  static constexpr int kHalf = 12;           /* input samples each side of the read point -> 24 taps */
  static constexpr int kTaps = 2 * kHalf;

  /** inRate/outRate; equal rates take the bypass below, so a native 44.1 session stays bit-exact. */
  void Configure(double inRate, double outRate)
  {
    mIn = inRate > 1.0 ? inRate : 44100.0;
    mOut = outRate > 1.0 ? outRate : mIn;
    mRatio = mIn / mOut;
    mBypass = std::fabs(mIn - mOut) < 1e-6;
    if (!mBypass)
      BuildKernel();
    Reset();
  }

  void Reset()
  {
    mPos = kHalf - 1;
    mFrac = 0.0;
    mBufL.assign(kHalf - 1, 0.f);            /* pre-roll: the taps to the left of sample 0 are silence */
    mBufR.assign(kHalf - 1, 0.f);
    mBufL.reserve(2048);
    mBufR.reserve(2048);
  }

  bool Bypass() const { return mBypass; }
  double Ratio() const { return mRatio; }
  double OutRate() const { return mOut; }

  /** Produce nOut frames into outL/outR, pulling input through fill(l, r, n). */
  template <class Fill>
  void Process(Fill& fill, float* outL, float* outR, int nOut)
  {
    if (nOut <= 0 || !outL || !outR)
      return;
    if (mBypass)
    {
      for (int at = 0; at < nOut;)
      {
        const int chunk = nOut - at > 128 ? 128 : nOut - at;
        fill(outL + at, outR + at, chunk);
        at += chunk;
      }
      return;
    }

    /* the furthest integer read position this block will reach, plus the taps to its right */
    const int last = mPos + (int) std::ceil((nOut - 1) * mRatio + mFrac);
    Ensure(fill, last + kHalf + 1);

    for (int i = 0; i < nOut; ++i)
    {
      const double pf = mFrac * kPhases;
      int p0 = (int) pf;
      if (p0 >= kPhases)
        p0 = kPhases - 1;
      const float pt = (float) (pf - p0);
      const float* a = &mTab[(size_t) p0 * kTaps];
      const float* b = a + kTaps;
      const float* sl = mBufL.data() + (mPos - kHalf + 1);
      const float* sr = mBufR.data() + (mPos - kHalf + 1);

      float l = 0.f, r = 0.f;
      for (int j = 0; j < kTaps; ++j)
      {
        const float c = a[j] + (b[j] - a[j]) * pt;
        l += sl[j] * c;
        r += sr[j] * c;
      }
      outL[i] = l;
      outR[i] = r;

      mFrac += mRatio;
      while (mFrac >= 1.0)
      {
        mFrac -= 1.0;
        ++mPos;
      }
    }
    Consume();
  }

private:
  template <class Fill>
  void Ensure(Fill& fill, int need)
  {
    while ((int) mBufL.size() < need)
    {
      const int at = (int) mBufL.size();
      const int chunk = need - at > 128 ? 128 : need - at;
      mBufL.resize((size_t) (at + chunk));
      mBufR.resize((size_t) (at + chunk));
      fill(mBufL.data() + at, mBufR.data() + at, chunk);
    }
  }

  void Consume()
  {
    const int drop = mPos - (kHalf - 1);
    if (drop <= 0)
      return;
    const int keep = (int) mBufL.size() - drop;
    if (keep <= 0)
    {
      Reset();
      return;
    }
    std::memmove(mBufL.data(), mBufL.data() + drop, (size_t) keep * sizeof(float));
    std::memmove(mBufR.data(), mBufR.data() + drop, (size_t) keep * sizeof(float));
    mBufL.resize((size_t) keep);
    mBufR.resize((size_t) keep);
    mPos -= drop;
  }

  static double BesselI0(double x)
  {
    double sum = 1.0, term = 1.0;
    const double q = x * x * 0.25;
    for (int k = 1; k < 32; ++k)
    {
      term *= q / ((double) k * k);
      sum += term;
      if (term < sum * 1e-12)
        break;
    }
    return sum;
  }

  static double Sinc(double x)
  {
    if (std::fabs(x) < 1e-9)
      return 1.0;
    return std::sin(kPi * x) / (kPi * x);
  }

  void BuildKernel()
  {
    /* Cutoff in cycles per *input* sample.
     *   upsampling   - the images sit above the input Nyquist and are inaudible, so the cutoff can go
     *                  close to it: keep the passband flat (0.96 -> -6 dB at 21.2 kHz).
     *   downsampling - the kernel is the only anti-alias filter, so pull it in (0.90) for a deeper notch. */
    const double guard = mRatio > 1.0 ? 0.90 : 0.96;
    const double fc = 0.5 * guard * (mRatio > 1.0 ? 1.0 / mRatio : 1.0);
    const double beta = 8.0;
    const double i0b = BesselI0(beta);

    mTab.assign((size_t) (kPhases + 1) * kTaps, 0.f);
    for (int p = 0; p <= kPhases; ++p)
    {
      const double frac = (double) p / kPhases;
      double sum = 0.0;
      float* row = &mTab[(size_t) p * kTaps];
      for (int j = 0; j < kTaps; ++j)
      {
        const double t = frac + (kHalf - 1) - j;          /* distance from the read point, input samples */
        const double u = t / kHalf;
        const double w = std::fabs(u) < 1.0 ? BesselI0(beta * std::sqrt(1.0 - u * u)) / i0b : 0.0;
        const double v = 2.0 * fc * Sinc(2.0 * fc * t) * w;
        row[j] = (float) v;
        sum += v;
      }
      if (sum != 0.0)                                     /* unity DC gain at every phase: no thump */
        for (int j = 0; j < kTaps; ++j)
          row[j] = (float) (row[j] / sum);
    }
  }

  static constexpr double kPi = 3.14159265358979323846;

  bool mBypass = true;
  double mIn = 44100.0, mOut = 44100.0;
  double mRatio = 1.0;
  double mFrac = 0.0;
  int mPos = kHalf - 1;
  std::vector<float> mBufL, mBufR;
  std::vector<float> mTab;
};
