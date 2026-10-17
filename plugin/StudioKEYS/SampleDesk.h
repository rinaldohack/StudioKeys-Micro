#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** SAMPLE desk — the loaded sample under the loop markers and the bit-depth stair, plus the
 *  SAMPLE/TONE blocks and the shared envelope.
 *  E0 SET · E1 TUNE · E2 BITS · E3 LOOP · E4 CUT · E5 — · E6 DRV · E7 — */
namespace sksam {

using namespace iplug;
using namespace igraphics;

/** Live waveform (the engine's output peek) with the LOOP bracket and the BITS stair drawn over
 *  the same grid, so a knob turn reads as a picture rather than a number. */
class SampleScope final : public IControl
{
public:
  SampleScope(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT);
    float buf[128];
    sk_host_waveform_peek(buf, 128);

    const int bits = std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127);
    const int loop = std::clamp((int) sk_host_get_param(mPe0 + 3), 0, 127);

    /* LOOP 0 = one-shot, 127 = the whole table: the bracket grows in from the right. */
    if (loop > 0)
    {
      const float l = plot.L + plot.W() * 0.06f;
      const float r = plot.L + plot.W() * (0.06f + 0.88f * (float) loop / 127.f);
      const IRECT bracket(l, plot.T + 2.f, r, plot.B - 2.f);
      g.FillRoundRect(IColor(28, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), bracket, 2.f);
      g.DrawRoundRect(IColor(120, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), bracket, 2.f, 0, 1.f);
    }

    /* Bit depth: 16 steps at BITS 0, one step at BITS 127 — a visible stair. */
    const int steps = std::max(1, (int) std::lround(16.f - 15.f * (float) bits / 127.f));
    const float grand = plot.H() * 0.46f;
    for (int i = 1; i < 128; ++i)
    {
      float v = std::clamp(buf[i], -1.f, 1.f);
      v = std::round(v * (float) steps) / (float) steps;
      const float x0 = plot.L + plot.W() * (float)(i - 1) / 127.f;
      const float x1 = plot.L + plot.W() * (float) i / 127.f;
      const float y0 = plot.MH() - buf[i - 1] * grand;
      const float y1 = plot.MH() - v * grand;
      g.DrawLine(skmint::kTheme, x0, y0, x1, y1, 0, 1.4f);
    }

    char val[24];
    skengui::ParamText(mPe0, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 110.f, mRECT.T + 23.f), "SET", val);
    skengui::ParamText(mPe0 + 2, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.R - 78.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "BITS", val);
    skengui::ParamText(mPe0 + 1, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.R - 158.f, mRECT.T + 5.f, mRECT.R - 82.f, mRECT.T + 23.f), "", val);
  }

private:
  uint32_t mPe0;
};

inline void AttachSampleDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                             const IRECT& body, const IVStyle& style,
                             int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT scopeCol = top.GetFromLeft(w * 0.40f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(scopeCol, "SAMPLE · LOOP / BITS"));
  attach(new SampleScope(scopeCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT sampleCol = top.GetReducedFromLeft(w * 0.40f).GetFromLeft(w * 0.28f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, sampleCol, "SAMPLE", 0);

  const IRECT toneCol = top.GetFromRight(w * 0.32f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, toneCol, "TONE", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace sksam
