#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** TRIO desk — the three-oscillator stack as a picture: each oscillator's wave, its interval
 *  above osc 1 and its detune, with the TONE block and the shared envelope.
 *  E0 WAVE · E1 INT2 · E2 INT3 · E3 DTN · E4 MODE · E5 CUT · E6 RES · E7 PW */
namespace sktrio {

using namespace iplug;
using namespace igraphics;

/** Three stacked one-cycle traces: osc 1 at unison, osc 2 / 3 at their intervals (INT2 / INT3
 *  semitones, quantised to the nearest tone of the 12-TET grid) and DTN in cents. */
class OscStack final : public IControl
{
public:
  OscStack(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT);
    const int wave = std::clamp((int) sk_host_get_param(mPe0), 0, 15);
    const int int2 = std::clamp((int) sk_host_get_param(mPe0 + 1), 0, 127);
    const int int3 = std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127);
    const int dtn = std::clamp((int) sk_host_get_param(mPe0 + 3), 0, 127);
    const float pw = std::max(0.08f, (float) sk_host_get_param(mPe0 + 7) / 127.f);

    const float rowH = plot.H() / 3.f;
    const int semis[3] = {0, (int) std::lround(int2 * 24.f / 127.f) - 12, (int) std::lround(int3 * 24.f / 127.f) - 12};
    const char* names[3] = {"OSC1", "OSC2", "OSC3"};

    for (int o = 0; o < 3; ++o)
    {
      const IRECT row(plot.L, plot.T + rowH * (float) o, plot.R, plot.T + rowH * (float)(o + 1));
      const IRECT lane = row.GetPadded(-2.f);
      const float mid = lane.MH();
      const IColor col = o == 0 ? skmint::kTheme : o == 1 ? skmint::kAccent : IColor(200, 120, 190, 240);

      g.DrawLine(IColor(40, col.R, col.G, col.B), lane.L, mid, lane.R, mid, 0, 1.f);
      /* Interval decides how many cycles fit: = 2^(semi/12) cycles per osc-1 cycle. */
      const float cycles = std::pow(2.f, (float) semis[o] / 12.f);
      float lastX = lane.L, lastY = mid;
      for (int i = 1; i <= 96; ++i)
      {
        const float t = (float) i / 96.f * cycles;
        const float ph = t - std::floor(t);
        const float det = (o == 0 ? 0.f : (o == 1 ? 1.f : -1.f) * (float) dtn / 127.f * 0.02f);
        const float v = Wave(ph, wave, pw) * (1.f - std::fabs(det) * 3.f);
        const float x = lane.L + lane.W() * (float) i / 96.f;
        const float y = mid - v * lane.H() * 0.38f;
        g.DrawLine(col, lastX, lastY, x, y, 0, 1.3f);
        lastX = x;
        lastY = y;
      }

      char label[32];
      snprintf(label, sizeof label, "%s  %+d st", names[o], semis[o]);
      g.DrawText(IText(11.f, col, skmint::kFont, EAlign::Near, EVAlign::Top), label,
                 IRECT(lane.L + 4.f, lane.T + 2.f, lane.R, lane.T + 16.f));
    }

    char val[24];
    skengui::ParamText(mPe0, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.B - 22.f, mRECT.L + 104.f, mRECT.B - 5.f), "", val);
    skengui::ParamText(mPe0 + 7, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.R - 76.f, mRECT.B - 22.f, mRECT.R - 6.f, mRECT.B - 5.f), "PW", val);
  }

private:
  static float Wave(float t, int set, float pw)
  {
    /* WAVE is one of 16 hardware sets; the trace only needs the dominant shape of the set. */
    const int kind = set < 2 ? (set == 0 ? 1 : 2) : set < 4 ? 2 : set < 6 ? 0 : set == 6 ? 2 : 1;
    switch (kind)
    {
      case 1: /* SAW */
        return 1.f - 2.f * t;
      case 2: /* PLS */
        return t < pw ? 1.f : -1.f;
      case 0:
      default: /* TRI */
        return t < 0.5f ? (4.f * t - 1.f) : (3.f - 4.f * t);
    }
  }

  uint32_t mPe0;
};

inline void AttachTrioDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                           const IRECT& body, const IVStyle& style,
                           int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT stackCol = top.GetFromLeft(w * 0.36f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(stackCol, "OSC STACK · 1 / 2 / 3"));
  attach(new OscStack(stackCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT oscCol = top.GetReducedFromLeft(w * 0.36f).GetFromLeft(w * 0.30f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, oscCol, "OSC · INTERVAL / DETUNE", 0);

  const IRECT toneCol = top.GetFromRight(w * 0.34f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, toneCol, "FILTER · TONE", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace sktrio
