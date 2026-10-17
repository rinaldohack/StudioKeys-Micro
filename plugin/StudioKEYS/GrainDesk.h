/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** GRAIN desk — the grain cloud as a picture: playhead position, grain size, density and the
 *  spray, with the GRAIN/SPRAY blocks and the shared envelope.
 *  E0 SRC · E1 POS · E2 SIZE · E3 DENS · E4 PTCH · E5 SPRD · E6 RAND · E7 TONE */
namespace skgrain {

using namespace iplug;
using namespace igraphics;

/** Grains over the buffer: x = read position around POS, width = SIZE, count = DENS, vertical
 *  spread = SPRD, jitter = RAND. A stable pseudo-random field, so the picture only moves when a
 *  knob does (the audio itself is the engine's own random). */
class GrainCloud final : public IControl
{
public:
  GrainCloud(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT);
    const int src = std::clamp((int) sk_host_get_param(mPe0), 0, 127);
    const int pos = std::clamp((int) sk_host_get_param(mPe0 + 1), 0, 127);
    const int size = std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127);
    const int dens = std::clamp((int) sk_host_get_param(mPe0 + 3), 0, 127);
    const int sprd = std::clamp((int) sk_host_get_param(mPe0 + 5), 0, 127);
    const int rnd = std::clamp((int) sk_host_get_param(mPe0 + 6), 0, 127);

    /* The buffer: a soft floor of bars, brighter around SRC (the chosen user sample). */
    const int bars = 48;
    for (int i = 0; i < bars; ++i)
    {
      const float t = (float) i / (float)(bars - 1);
      const float d = std::fabs(t - (float) src / 127.f);
      const float h = 0.16f + 0.5f * std::exp(-d * 6.f) * (0.6f + 0.4f * std::sin(t * 37.f));
      const IRECT b(plot.L + plot.W() * t, plot.B - plot.H() * h * 0.4f,
                    plot.L + plot.W() * (t + 1.f / bars) - 1.f, plot.B);
      g.FillRect(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), b);
    }

    /* Playhead. */
    const float px = plot.L + plot.W() * (float) pos / 127.f;
    g.DrawLine(skmint::kAccent, px, plot.T, px, plot.B, 0, 1.5f);

    const int n = 4 + dens * 60 / 127;
    const float gw = 0.012f + 0.16f * (float) size / 127.f;
    for (int i = 0; i < n; ++i)
    {
      const uint32_t h = (uint32_t)(i * 2654435761u) ^ (uint32_t)(src * 40503u);
      const float r1 = (float)((h >> 8) & 0xFFFF) / 65535.f - 0.5f;
      const float r2 = (float)((h >> 17) & 0x7FFF) / 32767.f - 0.5f;
      const float jitter = (float) rnd / 127.f * r1 * 0.5f;
      const float gx = std::clamp((float) pos / 127.f + jitter, 0.f, 1.f);
      const float gy = 0.5f + r2 * ((float) sprd / 127.f) * 0.8f;
      const IRECT grain(plot.L + plot.W() * gx, plot.T + plot.H() * (1.f - gy) - 3.f,
                        plot.L + plot.W() * std::min(1.f, gx + gw), plot.T + plot.H() * (1.f - gy) + 3.f);
      g.FillRoundRect(IColor(150, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), grain, 1.5f);
    }

    char val[24];
    skengui::ParamText(mPe0 + 1, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 88.f, mRECT.T + 23.f), "POS", val);
    snprintf(val, sizeof val, "%d", n);
    skengui::DrawChip(g, IRECT(mRECT.R - 70.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "GRAINS", val);
  }

private:
  uint32_t mPe0;
};

inline void AttachGrainDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                            const IRECT& body, const IVStyle& style,
                            int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT cloudCol = top.GetFromLeft(w * 0.40f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(cloudCol, "GRAIN CLOUD · POS / SIZE / DENS"));
  attach(new GrainCloud(cloudCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT grainCol = top.GetReducedFromLeft(w * 0.40f).GetFromLeft(w * 0.28f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, grainCol, "GRAIN", 0);

  const IRECT sprayCol = top.GetFromRight(w * 0.32f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, sprayCol, "SPRAY · PITCH / RANDOM", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace skgrain
