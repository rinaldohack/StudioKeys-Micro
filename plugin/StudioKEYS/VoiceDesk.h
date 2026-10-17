#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** VOICE desk — a formant map (F1/F2 of A E I O U) with the two vowel points that VOWL and
 *  VOWL2 select, the VOWEL/TONE blocks and the shared envelope.
 *  E0 VOWL · E1 VOWL2 · E2 TALK · E3 SHIFT · E4 BUZZ · E5 BRTH · E6 Q · E7 RAND */
namespace skvoice {

using namespace iplug;
using namespace igraphics;

/** The Klatt vowel plane. F1 horizontal (200..900 Hz), F2 vertical (700..2400 Hz, inverted like
 *  a vowel chart), the five vowels as named nodes, the picked ones lit. */
class FormantMap final : public IControl
{
public:
  FormantMap(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = skengui::ScopeWell(g, mRECT);
    const int v1 = std::clamp((int) sk_host_get_param(mPe0), 0, 4);
    const int v2 = std::clamp((int) sk_host_get_param(mPe0 + 1), 0, 4);
    const float shift = (float) sk_host_get_param(mPe0 + 3) / 127.f; /* SHIFT: -12..12 st */
    const float semis = -12.f + 24.f * shift;

    /* Vowel chart axes: F1 across, F2 up (the closer the vowel, the higher the point). */
    auto px = [&](float f1) { return plot.L + plot.W() * std::clamp((f1 - 200.f) / 750.f, 0.f, 1.f); };
    auto py = [&](float f2) { return plot.B - plot.H() * std::clamp((f2 - 700.f) / 1800.f, 0.f, 1.f); };
    const float scale = std::pow(2.f, semis / 12.f);

    /* The vowel quadrilateral: A - E - I - O - U, drawn as the traditional trapezoid. */
    g.PathMoveTo(px(kF1[0] * scale), py(kF2[0] * scale));
    for (int i = 1; i < 5; ++i)
      g.PathLineTo(px(kF1[i] * scale), py(kF2[i] * scale));
    g.PathClose();
    g.PathFill(IColor(24, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B));
    for (int i = 0; i < 5; ++i)
      g.DrawLine(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                 px(kF1[i] * scale), py(kF2[i] * scale), px(kF1[(i + 1) % 5] * scale), py(kF2[(i + 1) % 5] * scale),
                 0, 1.f);

    /* TALK draws the interpolation path between the two vowels. */
    if (sk_host_get_param(mPe0 + 2) > 0)
      g.DrawLine(IColor(120, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B),
                 px(kF1[v1] * scale), py(kF2[v1] * scale), px(kF1[v2] * scale), py(kF2[v2] * scale), 0, 1.5f);

    for (int i = 0; i < 5; ++i)
    {
      const float x = px(kF1[i] * scale), y = py(kF2[i] * scale);
      const bool a = (i == v1), b = (i == v2);
      if (a || b)
        g.FillCircle(IColor(90, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), x, y, 8.f);
      g.FillCircle(a ? skmint::kAccent : b ? skmint::kTheme : skmint::kWell, x, y, a || b ? 4.5f : 3.5f);
      g.DrawCircle(skmint::kBg, x, y, a || b ? 4.5f : 3.5f, 0, 1.f);
      g.DrawText(IText(12.f, a ? skmint::kAccent : b ? skmint::kTheme : skmint::kText,
                       skmint::kFont, EAlign::Center, EVAlign::Middle),
                 kVowelName[i], IRECT(x - 8.f, y - 22.f, x + 8.f, y - 8.f));
    }

    char val[24];
    snprintf(val, sizeof val, "%s>%s", kVowelName[v1], kVowelName[v2]);
    skengui::DrawChip(g, IRECT(mRECT.L + 6.f, mRECT.T + 5.f, mRECT.L + 118.f, mRECT.T + 23.f), "", val,
                      sk_host_get_param(mPe0 + 2) > 0);
    skengui::ParamText(mPe0 + 3, val, sizeof val);
    skengui::DrawChip(g, IRECT(mRECT.R - 66.f, mRECT.T + 5.f, mRECT.R - 6.f, mRECT.T + 23.f), "SH", val);
  }

private:
  /* Adult-male means from Hillenbrand et al. (1995), A E I O U. */
  static constexpr float kF1[5] = {768.f, 580.f, 342.f, 497.f, 378.f};
  static constexpr float kF2[5] = {1333.f, 1799.f, 2322.f, 910.f, 997.f};
  static constexpr const char* kVowelName[5] = {"A", "E", "I", "O", "U"};

  uint32_t mPe0;
};

inline void AttachVoiceDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                            const IRECT& body, const IVStyle& style,
                            int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT mapCol = top.GetFromLeft(w * 0.40f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(mapCol, "FORMANT MAP · A E I O U"));
  attach(new FormantMap(mapCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT vowelCol = top.GetReducedFromLeft(w * 0.40f).GetFromLeft(w * 0.28f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, vowelCol, "VOWEL", 0);

  const IRECT toneCol = top.GetFromRight(w * 0.32f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, toneCol, "SOURCE · TONE", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace skvoice
