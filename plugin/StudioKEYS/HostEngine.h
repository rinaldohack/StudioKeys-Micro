/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Resampler.h"
#include "sk_host.h"

/** Firmware host bridge. The firmware renders at its own fixed 44.1 kHz; SkResampler takes that to
 *  whatever the DAW runs at (48, 88.2, 96, ...) without the old linear interpolation's droop. */
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
    }
  }

  bool Ready() const { return mReady; }

  void SetSampleRate(double sr)
  {
    mHostSr = sr > 1.0 ? sr : 44100.0;
    mResampler.Configure(44100.0, mHostSr);
    mBootFrames = (int) (0.015 * mHostSr);   /* silence until ~15 ms of firmware frames are live */
  }

  void Process(float** outputs, int nFrames)
  {
    if (!mReady || !outputs || !outputs[0] || !outputs[1] || nFrames <= 0)
      return;

    auto fill = [](float* l, float* r, int n) { sk_host_render_f32(l, r, (uint32_t) n); };
    mResampler.Process(fill, outputs[0], outputs[1], nFrames);

    if (mBootFrames > 0)
    {
      const int mute = mBootFrames < nFrames ? mBootFrames : nFrames;
      std::memset(outputs[0], 0, (size_t) mute * sizeof(float));
      std::memset(outputs[1], 0, (size_t) mute * sizeof(float));
      mBootFrames -= mute;
    }
  }

  void Midi(uint8_t status, uint8_t d1, uint8_t d2) { sk_host_midi(status, d1, d2); }

private:
  bool mReady = false;
  int mBootFrames = 0;
  double mHostSr = 44100.0;
  SkResampler mResampler;
};
