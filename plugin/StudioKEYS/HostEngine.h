/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "sk_host.h"

/** Firmware host + linear resampler from 44.1 kHz to the DAW rate. */
class HostEngine
{
public:
  void Init()
  {
    if (!mReady)
    {
      sk_host_init();
      sk_host_boot();
      mReady = true;
      mBootFrames = (int) (0.015 * 44100.0); /* silence until ~15 ms frames are live */
    }
  }

  bool Ready() const { return mReady; }

  void SetSampleRate(double sr)
  {
    mHostSr = sr > 1.0 ? sr : 44100.0;
    mRatio = 44100.0 / mHostSr;
    mPhase = 0.0;
    mLeftover = 0;
  }

  void Process(float** outputs, int nFrames)
  {
    if (!mReady || !outputs || !outputs[0] || !outputs[1])
      return;

    const int need = (int) std::ceil(nFrames * mRatio) + 4;
    if ((int) mL.size() < need)
    {
      mL.resize((size_t) need);
      mR.resize((size_t) need);
    }

    int got = mLeftover;
    while (got < need)
    {
      const int chunk = need - got > 128 ? 128 : need - got;
      sk_host_render_f32(mL.data() + got, mR.data() + got, (uint32_t) chunk);
      got += chunk;
    }

    for (int i = 0; i < nFrames; ++i)
    {
      if (mBootFrames > 0)
      {
        outputs[0][i] = 0.f;
        outputs[1][i] = 0.f;
        --mBootFrames;
        mPhase += mRatio;
        continue;
      }
      const double pos = mPhase;
      const int i0 = (int) pos;
      const int i1 = i0 + 1;
      const float frac = (float) (pos - i0);
      const float l0 = mL[(size_t) i0], l1 = mL[(size_t) (i1 < need ? i1 : i0)];
      const float r0 = mR[(size_t) i0], r1 = mR[(size_t) (i1 < need ? i1 : i0)];
      outputs[0][i] = l0 + (l1 - l0) * frac;
      outputs[1][i] = r0 + (r1 - r0) * frac;
      mPhase += mRatio;
    }

    const int advance = (int) mPhase;
    if (advance > 0 && advance < need)
    {
      mLeftover = need - advance;
      std::memmove(mL.data(), mL.data() + advance, (size_t) mLeftover * sizeof(float));
      std::memmove(mR.data(), mR.data() + advance, (size_t) mLeftover * sizeof(float));
      mPhase -= advance;
    }
    else
    {
      mLeftover = 0;
      mPhase = 0.0;
    }
  }

  void Midi(uint8_t status, uint8_t d1, uint8_t d2) { sk_host_midi(status, d1, d2); }

private:
  bool mReady = false;
  int mBootFrames = 0;
  double mHostSr = 44100.0;
  double mRatio = 1.0;
  double mPhase = 0.0;
  int mLeftover = 0;
  std::vector<float> mL, mR;
};
