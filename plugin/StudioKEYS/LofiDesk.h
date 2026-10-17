#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** LOFI desk — chip voice: a quantised 32-step wave scope, the CHIP/MOTION blocks and the
 *  shared envelope. E0 CHIP · E1 WAVE · E2 DUTY · E3 CRSH · E4 SWP · E5 VIB · E6 ARP · E7 TONE. */
namespace sklofi {

using namespace iplug;
using namespace igraphics;

/** The wave RAM as the chip reads it: 32 steps, amplitude quantised by CHIP, pulse duty from
 *  DUTY. STEP mode shows its 4-bit staircase; WRAM shows the table's own envelope. */
class ChipScope final : public IControl
{
public:
  ChipScope(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const int chip = std::clamp((int) sk_host_get_param(mPe0), 0, 4);
    const int wave = std::clamp((int) sk_host_get_param(mPe0 + 1), 0, 4);
    const int duty = std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127);

    const IRECT plot = skengui::ScopeWell(g, mRECT);
    const int steps = 32;
    const float colW = plot.W() / (float) steps;

    /* CHIP picks the amplitude resolution: 4BIT / STEP -> 16, 4B/2 -> 32, 8BIT -> 256, 1BIT -> 2. */
    const int levels = chip == 2 ? 256 : chip == 3 ? 2 : chip == 1 ? 32 : 16;

    float lastX = 0.f, lastY = 0.f;
    for (int i = 0; i < steps; ++i)
    {
      float v = Shape((float) i / (float) steps, wave, duty, chip);
      v *= 0.92f;
      if (levels < 256)
        v = std::round(v * (levels / 2.f)) / (levels / 2.f);

      const float x = plot.L + colW * (float) i;
      const float y = plot.MH() - v * plot.H() * 0.46f;
      const IRECT bar(x + 1.f, std::min(y, plot.MH()), x + colW - 1.f, std::max(y, plot.MH()));
      g.FillRect(i & 1 ? skmint::kTheme : skmint::kAccent, bar);

      /* The step line on top of the bars reads as a chip trace: flat between samples. */
      const float bx = x + colW * 0.5f;
      if (i)
      {
        const IColor trace(170, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B);
        g.DrawLine(trace, lastX, lastY, bx, lastY, 0, 1.f);
        g.DrawLine(trace, bx, lastY, bx, y, 0, 1.f);
      }
      lastX = bx;
      lastY = y;
    }

    char val[24];
    skengui::ParamText(mPe0, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 96.f, mRECT.T + 23.f), "", val,
                      chip == 4);
    snprintf(val, sizeof val, "%d%%", (int) std::lround(duty * 100.f / 127.f));
    skengui::DrawChip(g, IRECT(mRECT.R - 72.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "DUTY", val);
  }

private:
  /** A one-cycle chip waveform, -1..1 across the window. */
  static float Shape(float t, int wave, int duty, int chip)
  {
    const float dutyF = chip == 4 ? 0.5f : std::max(0.06f, (float) duty / 127.f);
    switch (wave)
    {
      case 1: /* TRI */
        return t < 0.5f ? (4.f * t - 1.f) : (3.f - 4.f * t);
      case 2: /* SAW */
        return 1.f - 2.f * t;
      case 3: /* NOIS */
      {
        const uint32_t x = (uint32_t)(t * 32768.f) * 1103515245u + 12345u;
        return ((float)((x >> 16) & 0x7FFFu) / 16383.5f) - 1.f;
      }
      case 4: /* WRAM: a harmonic table, quantised by the chip anyway */
        return 0.62f * std::sin(t * 6.28318f) + 0.30f * std::sin(t * 12.56637f)
             + 0.14f * std::sin(t * 18.84956f);
      case 0:
      default: /* PLS */
        return t < dutyF ? 1.f : -1.f;
    }
  }

  uint32_t mPe0;
};

inline void AttachLofiDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                           const IRECT& body, const IVStyle& style,
                           int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT scopeCol = top.GetFromLeft(w * 0.32f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(scopeCol, "CHIP SCOPE"));
  attach(new ChipScope(scopeCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT chipCol = top.GetReducedFromLeft(w * 0.32f).GetFromLeft(w * 0.30f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, chipCol, "CHIP", 0);

  const IRECT motionCol = top.GetFromRight(w * 0.38f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, motionCol, "MOTION · SWEEP / VIB / ARP", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace sklofi
