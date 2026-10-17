#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** PHYS desk — the resonator as a picture: the string with its exciter position, the brightness
 *  as the harmonic content of the trace, the damping as its decay, plus the BODY/EXCITE blocks
 *  and the shared envelope.
 *  E0 MODEL · E1 STRC · E2 BRIT · E3 DAMP · E4 POS · E5 ACC · E6 BOW · E7 EXC */
namespace skphys {

using namespace iplug;
using namespace igraphics;

/** MODEL (MODAL / STRNG / MEMB / SYMP) picks the picture: a struck string, a bowed one, a
 *  membrane's rings, or the sympathetic strings above the played note. */
class ResonatorView final : public IControl
{
public:
  ResonatorView(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT, false);
    const int model = std::clamp((int) sk_host_get_param(mPe0), 0, 3);
    const float bright = (float) std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127) / 127.f;
    const float damp = (float) std::clamp((int) sk_host_get_param(mPe0 + 3), 0, 127) / 127.f;
    const float pos = 0.04f + 0.92f * (float) std::clamp((int) sk_host_get_param(mPe0 + 4), 0, 127) / 127.f;

    const float mid = plot.MH();
    const int px = 160;

    if (model == 2) /* MEMB: concentric rings that die off with DAMP */
    {
      const float cx = plot.MW(), cy = plot.MH();
      const float maxR = std::min(plot.W(), plot.H()) * 0.46f;
      const int rings = 3 + (int) std::lround(bright * 5.f);
      for (int i = 0; i < rings; ++i)
      {
        const float t = (float) i / (float) rings;
        const float r = maxR * (0.25f + 0.75f * t);
        const int a = (int) std::lround(190.f * std::pow(1.f - damp * 0.7f, (float) i));
        g.DrawEllipse(IColor(std::max(25, a), skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                      cx, cy, r, r * 0.62f, 0, 0, 1.4f);
      }
      /* The strike point. */
      g.FillCircle(skmint::kAccent, plot.L + plot.W() * pos, cy, 4.f);
      return;
    }

    if (model == 3) /* SYMP: the played string plus the sympathetic ones above it */
    {
      for (int i = 0; i < 3; ++i)
      {
        const float y = plot.T + plot.H() * (0.22f + 0.24f * (float) i);
        const float amp = 5.f + 10.f * bright * (1.f - (float) i / 4.f);
        float lastX = plot.L, lastY = y;
        for (int k = 1; k <= 96; ++k)
        {
          const float t = (float) k / 96.f;
          const float v = std::sin(t * 6.28318f * (float)(2 + i)) * amp * std::sin(t * 3.14159f);
          const float x = plot.L + plot.W() * t;
          g.DrawLine(i == 0 ? skmint::kTheme : IColor(150, 120, 190, 240), lastX, lastY, x, y - v, 0, 1.2f);
          lastX = x;
          lastY = y - v;
        }
      }
      g.DrawLine(IColor(80, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B),
                 plot.L + plot.W() * pos, plot.T, plot.L + plot.W() * pos, plot.B, 0, 1.f);
      return;
    }

    /* MODAL / STRNG: one string, the envelope decaying with DAMP, harmonics rising with BRIT. */
    const float harmonics = 1.f + bright * 4.f;
    const float decay = 0.25f + damp * 0.75f;
    float lastX = plot.L, lastY = mid;
    for (int k = 0; k <= px; ++k)
    {
      const float t = (float) k / (float) px;
      float v = 0.f;
      for (int h = 1; h <= 5; ++h)
      {
        const float w = std::pow(1.f - bright * 0.6f, (float)(h - 1));
        v += w * std::sin(t * 3.14159f * (float) h) * std::sin(t * 3.14159f * harmonics) * 0.34f;
      }
      v *= (model == 1 ? 1.f : 0.8f) * std::exp(-t * decay * 1.6f);
      const float x = plot.L + plot.W() * t;
      const float y = mid - v * plot.H() * 0.42f;
      g.DrawLine(skmint::kTheme, lastX, lastY, x, y, 0, 1.4f);
      lastX = x;
      lastY = y;
    }

    /* Exciter position: the pluck / bow point, with ACC as its strength. */
    const float ex = plot.L + plot.W() * pos;
    const float acc = (float) std::clamp((int) sk_host_get_param(mPe0 + 5), 0, 127) / 127.f;
    g.DrawLine(IColor(140, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), ex, plot.T, ex, plot.B, 0, 1.f);
    g.FillCircle(skmint::kAccent, ex, mid, 3.f + 4.f * acc);

    char val[24];
    skengui::ParamText(mPe0, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 104.f, mRECT.T + 23.f), "MDL", val);
    snprintf(val, sizeof val, "%d%%", (int) std::lround(damp * 100.f));
    skengui::DrawChip(g, IRECT(mRECT.R - 76.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "DAMP", val);
  }

private:
  uint32_t mPe0;
};

inline void AttachPhysDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                           const IRECT& body, const IVStyle& style,
                           int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT viewCol = top.GetFromLeft(w * 0.38f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(viewCol, "RESONATOR · MODEL / DAMP"));
  attach(new ResonatorView(viewCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT bodyCol = top.GetReducedFromLeft(w * 0.38f).GetFromLeft(w * 0.30f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, bodyCol, "BODY", 0);

  const IRECT exciteCol = top.GetFromRight(w * 0.32f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, exciteCol, "EXCITER", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace skphys
