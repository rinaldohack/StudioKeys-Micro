/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** PHASE desk — the crossfeed picture: the live output over the sub oscillator's reference, with
 *  the DCW depth marked on the same plot, plus the WAVE 1/2 depth block, the LINE/SUB block and the
 *  shared envelope.
 *  E0 WAVE · E1 WAVE2 · E2 DCW · E3 ENV · E4 DTN · E5 LINE · E6 SUB · E7 — */
namespace skphase {

using namespace iplug;
using namespace igraphics;

/** Two traces on one grid: the live output (accent) and the sub oscillator's phase-locked
 *  reference (dim), so the LINE mix and the SUB level read as one picture. The DCW marker sits
 *  where the phase distortion wraps, because that is the parameter the ear hears as bite. */
class PhaseScope final : public IControl
{
public:
  PhaseScope(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT);
    const float mid = plot.MH();

    float buf[128];
    sk_host_waveform_peek(buf, 128);

    const int dcw = std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127);
    const int subLvl = std::clamp((int) sk_host_get_param(mPe0 + 6), 0, 127);
    const int line = std::clamp((int) sk_host_get_param(mPe0 + 5), 0, 127);

    /* The sub reference: one cycle of sine at the SUB level, drawn under the live trace. */
    const float subAmp = 0.08f + 0.78f * (float) subLvl / 127.f;
    for (int i = 1; i < 128; ++i)
    {
      const float x0 = plot.L + plot.W() * (float)(i - 1) / 127.f;
      const float x1 = plot.L + plot.W() * (float) i / 127.f;
      const float y0 = mid - std::sin((float)(i - 1) * 0.0982f) * plot.H() * 0.40f * subAmp;
      const float y1 = mid - std::sin((float) i * 0.0982f) * plot.H() * 0.40f * subAmp;
      g.DrawLine(IColor(110, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), x0, y0, x1, y1, 0, 1.f);
    }

    /* The DCW depth: where the phase accumulator wraps. */
    const float dx = plot.L + plot.W() * (float) dcw / 127.f;
    g.DrawLine(IColor(90, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), dx, plot.T, dx, plot.B,
               0, 1.f);

    /* The live output on top: the mix of wave 1, wave 2 and the sub. */
    const float gain = 0.30f + 0.60f * (float) line / 127.f;
    for (int i = 1; i < 128; ++i)
    {
      const float x0 = plot.L + plot.W() * (float)(i - 1) / 127.f;
      const float x1 = plot.L + plot.W() * (float) i / 127.f;
      const float y0 = mid - std::clamp(buf[i - 1], -1.f, 1.f) * plot.H() * 0.46f * gain;
      const float y1 = mid - std::clamp(buf[i], -1.f, 1.f) * plot.H() * 0.46f * gain;
      g.DrawLine(skmint::kAccent, x0, y0, x1, y1, 0, 1.5f);
    }

    char val[24];
    skengui::ParamText(mPe0, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 96.f, mRECT.T + 23.f), "WAVE", val);
    skengui::ParamText(mPe0 + 5, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.R - 92.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "LINE", val,
                      line > 64);
  }

private:
  uint32_t mPe0;
};

inline void AttachPhaseDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                            const IRECT& body, const IVStyle& style,
                            int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  /* P5g: three columns like the rest of the P7 desks, instead of the old loose scopes-and-knobs
   * page whose 2 x 4 knob grid straddled the waveform strip. */
  const IRECT scopeCol = top.GetFromLeft(w * 0.42f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(scopeCol, "PHASE · CROSSFEED / DCW"));
  attach(new PhaseScope(scopeCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT waveCol = top.GetReducedFromLeft(w * 0.42f).GetFromLeft(w * 0.30f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, waveCol, "WAVE 1 / 2 · DEPTH", 0);

  const IRECT lineCol = top.GetFromRight(w * 0.28f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, lineCol, "LINE · SUB / DETUNE", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace skphase
