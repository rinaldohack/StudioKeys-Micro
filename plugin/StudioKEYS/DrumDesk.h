#pragma once

#include "EngineDeskUI.h"

#include <cmath>
#include <cstdio>

/** DRUM desk — the kit as eight pads (the GM families the DRUM engine voices), each pad sized by
 *  DECY and lit by SNAP, with the KIT/HIT blocks and the shared envelope.
 *  E0 KIT · E1 TUNE · E2 TONE · E3 DECY · E4 SNAP · E5 ACC · E6 KICK · E7 DRV */
namespace skdrum {

using namespace iplug;
using namespace igraphics;

/** Eight pads in two rows: the kit's voice families. The pad heights read the shared DECY /
 *  SNAP / ACC, so the grid is a live picture of the kit rather than eight static keys. */
class PadGrid final : public IControl
{
public:
  PadGrid(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT well = mRECT.GetPadded(-1.f);
    g.FillRoundRect(skmint::kBg, well, 4.f);
    g.DrawRoundRect(IColor(90, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), well, 4.f, 0, 1.25f);

    const float decay = (float) std::clamp((int) sk_host_get_param(mPe0 + 3), 0, 127) / 127.f;
    const float snap = (float) std::clamp((int) sk_host_get_param(mPe0 + 4), 0, 127) / 127.f;
    const float acc = (float) std::clamp((int) sk_host_get_param(mPe0 + 5), 0, 127) / 127.f;
    const float tone = (float) std::clamp((int) sk_host_get_param(mPe0 + 2), 0, 127) / 127.f;

    const IRECT grid = well.GetPadded(-6.f);
    for (int i = 0; i < 8; ++i)
    {
      const IRECT cell = grid.GetGridCell(i, 2, 4).GetPadded(-3.f);
      /* Each family has its own natural length: a kick rings, a clap snaps. */
      const float base[8] = {0.95f, 0.55f, 0.28f, 0.22f, 0.72f, 0.30f, 0.42f, 0.60f};
      const float len = std::clamp(base[i] * (0.35f + 0.65f * decay), 0.06f, 1.f);
      const float hot = std::clamp(0.25f + 0.75f * (i == 0 ? snap : (0.5f + (tone - 0.5f) * 0.5f)), 0.f, 1.f);

      const IRECT pad = cell.GetFromBottom(cell.H() * len);
      g.FillRoundRect(skmint::kSurf, cell, 3.f);
      g.FillRoundRect(i == 0 ? skmint::kAccent : skmint::kTheme, pad, 3.f);
      g.DrawRoundRect(skmint::kBg, cell, 3.f, 0, 1.f);
      /* ACC: the pad's glow bar along its bottom. */
      const IRECT glow(cell.L + 2.f, cell.B - 3.f, cell.L + 2.f + (cell.W() - 4.f) * acc, cell.B - 1.f);
      g.FillRect(skmint::kInk, glow);

      g.DrawText(IText(10.f, hot > 0.6f ? skmint::kInk : skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 kPadName[i], cell);
    }

    char val[24];
    skengui::ParamText(mPe0, val, sizeof val);
    skengui::DrawChip(g, IRECT(well.L + 6.f, well.T + 5.f, well.L + 96.f, well.T + 23.f), "KIT", val);
    skengui::ParamText(mPe0 + 3, val, sizeof val);
    skengui::DrawChip(g, IRECT(well.R - 78.f, well.T + 5.f, well.R - 6.f, well.T + 23.f), "DECY", val);
  }

private:
  static constexpr const char* kPadName[8] = {"KICK", "SNARE", "CLAP", "RIM", "TOM", "HAT", "OPEN", "CRASH"};

  uint32_t mPe0;
};

inline void AttachDrumDesk(const skengui::AttachFn& attach, const skengui::AttachParamFn& param,
                           const IRECT& body, const IVStyle& style,
                           int pAtk, int pDec, int pSus, int pRel)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();
  const float stripH = std::clamp(body.H() * 0.28f, 60.f, 92.f);
  const IRECT top = body.GetReducedFromBottom(stripH + 6.f);
  const IRECT strip = body.GetFromBottom(stripH);

  const IRECT padCol = top.GetFromLeft(w * 0.36f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(padCol, "KIT · EIGHT VOICES"));
  attach(new PadGrid(padCol.GetReducedFromTop(22.f).GetPadded(-5.f), pe0));

  const IRECT kitCol = top.GetReducedFromLeft(w * 0.36f).GetFromLeft(w * 0.30f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, kitCol, "KIT", 0);

  const IRECT hitCol = top.GetFromRight(w * 0.34f).GetPadded(-3.f);
  skengui::AttachParamBlock(attach, param, hitCol, "HIT · SNAP / ACC / DRIVE", 4);

  skengui::AttachAdsrStrip(attach, strip, style, pAtk, pDec, pSus, pRel);
}

} // namespace skdrum
