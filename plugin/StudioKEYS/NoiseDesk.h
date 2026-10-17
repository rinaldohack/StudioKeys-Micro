/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** NOISE desk — the spectrum as a picture: the colour slant, the FREQ/RES peak and the density
 *  of the source, with the SOURCE/MOTION blocks and the shared envelope.
 *  E0 MODE · E1 COLR · E2 FREQ · E3 RES · E4 TRK · E5 DENS · E6 DRFT · E7 CRSH */
namespace sknoise {

using namespace iplug;
using namespace igraphics;

/** 32 log-spaced bands: a slope from COLR, a resonant peak at FREQ whose height and width
 *  follow RES, and the band levels jittered by DENS (sparse = gaps, dense = a full floor). */
class NoiseSpectrum final : public IControl
{
public:
  NoiseSpectrum(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT);
    const int mode = std::clamp((int) sk_host_get_param(mPe0), 0, 127);
    const int colr = std::clamp((int) sk_host_get_param(mPe0 + 1), 0, 127);
    const int freq = std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127);
    const int res = std::clamp((int) sk_host_get_param(mPe0 + 3), 0, 127);
    const int dens = std::clamp((int) sk_host_get_param(mPe0 + 5), 0, 127);

    const int bands = 32;
    const float bw = plot.W() / (float) bands;
    const float slope = ((float) colr / 127.f - 0.5f) * 2.2f; /* + = bright, - = dark */
    const float peakT = (float) freq / 127.f;
    const float q = 0.05f + 0.30f * (1.f - (float) res / 127.f);
    const float floorFrac = 0.10f + 0.55f * (float) dens / 127.f;

    for (int i = 0; i < bands; ++i)
    {
      const float t = (float) i / (float)(bands - 1);
      float h = floorFrac + 0.30f + slope * (t - 0.5f);
      h += 0.75f * std::exp(-((t - peakT) * (t - peakT)) / (q * q));
      /* Deterministic jitter: the picture moves with DENS, not with the clock. */
      const uint32_t r = (uint32_t)(i * 2246822519u) ^ (uint32_t)(dens * 3266489917u);
      h *= 0.72f + 0.28f * (float)((r >> 16) & 0x7FFFu) / 32767.f;
      h = std::clamp(h, 0.03f, 1.f);

      const IRECT bar(plot.L + bw * (float) i + 1.f, plot.B - plot.H() * h,
                      plot.L + bw * (float)(i + 1) - 1.f, plot.B);
      const bool lit = std::fabs(t - peakT) < q * 1.2f;
      g.FillRect(lit ? skmint::kAccent : skmint::kTheme, bar);
    }

    char val[24];
    skengui::ParamText(mPe0 + 2, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 96.f, mRECT.T + 23.f), "FRQ", val);
    skengui::ParamText(mPe0 + 3, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.R - 76.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "RES", val);
    (void) mode;
  }

private:
  uint32_t mPe0;
};

inline void AttachNoiseDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                            const IRECT& body, const IVStyle& style,
                            int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT specCol = top.GetFromLeft(w * 0.40f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(specCol, "SPECTRUM · COLR / FREQ / RES"));
  attach(new NoiseSpectrum(specCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT srcCol = top.GetReducedFromLeft(w * 0.40f).GetFromLeft(w * 0.28f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, srcCol, "SOURCE", 0);

  const IRECT motionCol = top.GetFromRight(w * 0.32f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, motionCol, "MOTION · TRACK / DRIFT", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace sknoise
