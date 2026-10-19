/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "EngineDeskUI.h"
#include "SampleSlotBay.h"

#include <cmath>
#include <cstdio>

/** SLICE desk — the loop under its slice markers: DIV divisions, the START offset and the REV
 *  direction, with the SLICE/PLAY blocks and the shared envelope.
 *  E0 SRC · E1 DIV · E2 START · E3 PTCH · E4 MODE · E5 REV · E6 DCAY · E7 TONE */
namespace skslice {

using namespace iplug;
using namespace igraphics;

/** The sample as an envelope skeleton, cut into DIV slices, the START marker where the first
 *  trigger lands, and an arrow for REV. */
class SliceMap final : public IControl
{
public:
  SliceMap(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT);
    const int div = std::clamp((int) sk_host_get_param(mPe0 + 1), 0, 127);
    const int start = std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127);
    const int mode = std::clamp((int) sk_host_get_param(mPe0 + 4), 0, 127);
    const bool rev = sk_host_get_param(mPe0 + 5) > 64;

    /* The loop skeleton: the live output peek, mirrored, scaled by a fixed window. */
    float buf[128];
    sk_host_waveform_peek(buf, 128);
    const float mid = plot.MH();
    for (int i = 1; i < 128; ++i)
    {
      const float x0 = plot.L + plot.W() * (float)(i - 1) / 127.f;
      const float x1 = plot.L + plot.W() * (float) i / 127.f;
      const float y0 = mid - buf[i - 1] * plot.H() * 0.40f;
      const float y1 = mid - buf[i] * plot.H() * 0.40f;
      g.DrawLine(IColor(120, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), x0, y0, x1, y1, 0, 1.f);
    }

    /* DIV: 1..32 slices (log feel, so the first few are far apart). */
    const int n = 1 + (int) std::lround(std::pow((float) div / 127.f, 1.6f) * 31.f);
    for (int i = 1; i <= n; ++i)
    {
      const float x = plot.L + plot.W() * (float) i / (float)(n + 1);
      g.DrawLine(IColor(150, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), x, plot.T, x, plot.B, 0, 1.f);
      const IRECT idx(x - 10.f, plot.B - 12.f, x + 10.f, plot.B);
      char s[8];
      snprintf(s, sizeof s, "%d", i);
      g.DrawText(IText(10.f, skmint::kText, skmint::kFont), s, idx);
    }

    /* START: the first slice boundary, lit. */
    const float sx = plot.L + plot.W() * (float) start / 127.f;
    g.FillRect(IColor(50, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B),
               IRECT(plot.L, plot.T, sx, plot.B));
    g.DrawLine(skmint::kAccent, sx, plot.T, sx, plot.B, 0, 2.f);

    /* REV: a triangle pointing left at the marker, right at the end. */
    const float ay = plot.T + 10.f;
    if (rev)
      g.DrawTriangle(skmint::kAccent, sx, plot.T + 4.f, sx + 9.f, ay, sx, ay + 6.f);
    else
      g.DrawTriangle(IColor(120, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), sx, ay + 6.f, sx + 9.f, ay, sx, plot.T + 4.f);

    char val[24];
    snprintf(val, sizeof val, "%d", n);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 78.f, mRECT.T + 23.f), "SLICES", val);
    skengui::ParamText(mPe0 + 4, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.R - 92.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "MODE", val,
                      mode != 0);
  }

private:
  uint32_t mPe0;
};

inline void AttachSliceDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                            const IRECT& body, const IVStyle& style,
                            int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT strip = body.GetFromBottom(stripH);
  /* P19: same fix as the SAMPLE desk — the USR1..USR3 drop bay lives under the slice map in the
   * left column, so the SLICE / PLAY panels keep the full height above the envelope and their
   * menu labels no longer sit on their values. */
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);

  const IRECT mapCol = top.GetFromLeft(w * 0.40f).GetPadded(-3.f);
  const float bayH = std::clamp(mapCol.H() * 0.30f, 42.f, 54.f);
  const IRECT bay = mapCol.GetFromBottom(bayH).GetPadded(-2.f);
  const IRECT mapBox = mapCol.GetReducedFromBottom(bayH + 8.f);

  attach(new skengui::ModulePanel(mapBox, "SLICE MAP · DIV / START"));
  attach(new SliceMap(mapBox.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));
  skslot::AttachUserSlotBay(attach, bay);

  const IRECT sliceCol = top.GetReducedFromLeft(w * 0.40f).GetFromLeft(w * 0.28f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, sliceCol, "SLICE", 0);

  const IRECT playCol = top.GetFromRight(w * 0.32f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, playCol, "PLAY · REV / DECAY", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace skslice
