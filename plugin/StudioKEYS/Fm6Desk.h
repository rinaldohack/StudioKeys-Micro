#pragma once

#include "IControls.h"
#include "SkMint.h"
#include "DeskParams.h"
#include "sk_host.h"
#include "../../firmware/src/lc_logo.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace iplug;
using namespace igraphics;

/** Dexed-style FM6 desk — the full 155-byte single voice edited byte by byte
 * through sk_host_fm6_*. Layout mirrors Dexed: the algorithm chart on the left
 * with the six operators as selectable blocks, the whole selected operator in
 * the middle (EG graph + every parameter), and the voice-global modules on the
 * right (pitch EG, LFO, transpose, name). Operator bytes are 0..20 relative to
 * an operator base; voice bytes are absolute 126..154 (fm6_core.c FP_*). */
namespace skfm6 {

/* fm6_core.c FP_* — operator 0 = OP6. */
enum : uint32_t {
  kOp = 21u,
  kR1 = 0, kL1 = 4, kBp = 8, kLd = 9, kRd = 10,
  kLc = 11, kRc = 12, kRs = 13,
  kAms = 14, kKvs = 15, kOl = 16, kMode = 17, kFc = 18, kFf = 19, kDet = 20,
  kPr1 = 126, kPl1 = 130, kAlg = 134, kFb = 135, kOks = 136,
  kLfs = 137, kLfd = 138, kLpmd = 139, kLamd = 140, kLks = 141, kLfw = 142,
  kLpms = 143, kTrnsp = 144, kName = 145
};

/* The operator the middle column edits (Dexed's OP select). 1..6. */
inline int& SelOp()
{
  static int s = 1;
  return s;
}

inline uint32_t OpBase(int op1to6)
{
  const int n = std::clamp(op1to6, 1, 6);
  return (uint32_t)(6 - n) * kOp;
}

/** An operator byte 0..20 is resolved against the current SelOp(). */
inline uint32_t Resolve(uint32_t off, bool opRel)
{
  return opRel ? OpBase(SelOp()) + off : off;
}

inline uint8_t MaxFor(uint32_t off)
{
  static const uint8_t kOpMax[21] = {
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
  static const uint8_t kVMax[19] = {
    99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
  if (off < 126u)
    return kOpMax[off % 21u];
  if (off < 145u)
    return kVMax[off - 126u];
  return 126u;
}

/* Eg graphs that must redraw the moment a voice byte changes (Dexed-style). */
inline std::vector<IControl*>& LiveEgGraphs()
{
  static std::vector<IControl*> s;
  return s;
}

inline void PulseEgGraphs()
{
  for (IControl* c : LiveEgGraphs())
  {
    if (c)
      c->SetDirty(false);
  }
}

/* Layout constants (shared with tools/check_fm6_layout.cpp). The FM6 desk is a
 * wide, short band, so the operator page is a dense grid and every module is a
 * computed sub-rect — nothing is placed at a fixed pixel size, so cells can
 * never overlap when the window scales. */
namespace fm6layout {
static constexpr float kLabelH = 11.f;   /* the strip a knob label lives in */
static constexpr float kPanelTitleH = 15.f;
static constexpr float kHeaderH = 22.f;
} // namespace fm6layout

/** Dropdown-free value names for a byte (LFO wave). nullptr-terminated. */
inline const char* NamedValue(const char* const* names, uint8_t v)
{
  if (!names)
    return nullptr;
  for (uint32_t i = 0; names[i]; ++i)
  {
    if (i == v)
      return names[i];
  }
  return nullptr;
}

/* ------------------------------------------------------------- controls --- */

/** A byte knob. `opRel` binds the offset to the selected operator.
 * Ring shows the value; the label sits under the ring. Nothing overlaps: the
 * dial is sized from the live rect so any cell works. */
class ByteKnob final : public IControl
{
public:
  ByteKnob(const IRECT& bounds, uint32_t off, const char* label, float labelPt = 11.f,
           bool opRel = false, const char* const* names = nullptr)
  : IControl(bounds)
  , mOff(off)
  , mLabel(label ? label : "")
  , mLabelPt(labelPt)
  , mOpRel(opRel)
  , mNames(names)
  {
    Sync();
  }

  uint32_t Off() const { return Resolve(mOff, mOpRel); }

  void Sync()
  {
    if (mDragging)
      return;
    const uint8_t mx = MaxFor(Off());
    double v = mx ? (double)sk_host_fm6_get_byte(Off()) / (double)mx : 0.0;
    mValue = std::clamp(v, 0.0, 1.0);
  }

  void Draw(IGraphics& g) override
  {
    Sync();
    const bool hasLabel = !mLabel.empty();
    /* Label lives in a strip at the top; on a short cell the strip shrinks so
     * the dial keeps a usable size instead of being squeezed to nothing. */
    const float labH = hasLabel ? std::min(fm6layout::kLabelH, mRECT.H() * 0.34f) : 0.f;
    const IRECT dial = hasLabel ? mRECT.GetReducedFromTop(labH) : mRECT;
    const float side = std::min(dial.W(), dial.H()) * 0.96f;
    if (side <= 4.f)
      return;
    const IRECT ring = dial.GetCentredInside(side);
    g.FillEllipse(skmint::kWell, ring);
    g.DrawEllipse(skmint::kTheme, ring, 0, 1.25f);
    const float cx = ring.MW(), cy = ring.MH(), r = ring.W() * 0.36f;
    const float angle = -135.f + (float) mValue * 270.f;
    g.DrawRadialLine(skmint::kAccent, cx, cy, angle, 0.f, r, nullptr, std::max(1.5f, side * 0.05f));

    const uint8_t byteVal = sk_host_fm6_get_byte(Off());
    char vstr[16];
    const char* nm = NamedValue(mNames, byteVal);
    if (nm)
      snprintf(vstr, sizeof vstr, "%s", nm);
    else
      snprintf(vstr, sizeof vstr, "%u", (unsigned) byteVal);
    g.DrawText(IText(std::min(12.f, side * 0.36f), skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle),
               vstr, ring);

    if (hasLabel)
      g.DrawText(IText(std::min(mLabelPt, labH - 0.5f), skmint::kText, skmint::kFont,
                       EAlign::Center, EVAlign::Middle),
                 mLabel.c_str(), mRECT.GetFromTop(labH));
  }

  void OnMouseDown(float, float, const IMouseMod&) override { mDragging = true; }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    mDragging = false;
    Sync();
    SetDirty(false);
  }

  void OnMouseDrag(float, float, float, float dY, const IMouseMod& mod) override
  {
    const uint8_t mx = MaxFor(Off());
    /* Fine = shift; Dexed-like: every pixel step updates the host + EG graph. */
    const double sens = mod.S ? 0.0025 : 0.008;
    mValue = std::clamp(mValue - (double) dY * sens, 0.0, 1.0);
    sk_host_fm6_set_byte(Off(), (uint8_t) std::lround(mValue * (double) mx));
    sk_host_ui_pump();
    SetDirty(false);
    PulseEgGraphs(); /* OP cards are registered here too; redraw them immediately */
  }

  void OnMouseWheel(float, float, const IMouseMod& mod, float d) override
  {
    const uint8_t mx = MaxFor(Off());
    const double step = mod.S ? (1.0 / std::max(1, (int) mx)) : (2.0 / std::max(1, (int) mx));
    mValue = std::clamp(mValue + (d > 0.f ? step : -step), 0.0, 1.0);
    sk_host_fm6_set_byte(Off(), (uint8_t) std::lround(mValue * (double) mx));
    sk_host_ui_pump();
    SetDirty(false);
    PulseEgGraphs();
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

  void OnMouseDblClick(float, float, const IMouseMod&) override
  {
    sk_host_fm6_set_byte(Off(), 0);
    sk_host_ui_pump();
    Sync();
    SetDirty(false);
    PulseEgGraphs();
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

private:
  uint32_t mOff = 0;
  double mValue = 0.0;
  std::string mLabel;
  float mLabelPt = 11.f;
  bool mOpRel = false;
  const char* const* mNames = nullptr;
  bool mDragging = false;
};

/** Ratio / FIXED toggle (MODE 0). */
class ModeToggle final : public IControl
{
public:
  ModeToggle(const IRECT& bounds, uint32_t off, bool opRel = false)
  : IControl(bounds)
  , mOff(off)
  , mOpRel(opRel)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool fixed = sk_host_fm6_get_byte(Resolve(mOff, mOpRel)) != 0;
    const IRECT a = mRECT.GetFromLeft(mRECT.W() * 0.5f).GetPadded(-1.f);
    const IRECT b = mRECT.GetFromRight(mRECT.W() * 0.5f).GetPadded(-1.f);
    g.FillRoundRect(fixed ? skmint::kTheme : skmint::kWell, a, 3.f);
    g.FillRoundRect(!fixed ? skmint::kTheme : skmint::kWell, b, 3.f);
    g.DrawText(IText(11.f, fixed ? skmint::InkOn(skmint::kTheme) : skmint::kText,
                     skmint::kFont, EAlign::Center, EVAlign::Middle),
               "RATIO", a);
    g.DrawText(IText(11.f, !fixed ? skmint::InkOn(skmint::kTheme) : skmint::kText,
                     skmint::kFont, EAlign::Center, EVAlign::Middle),
               "FIXED", b);
  }

  void OnMouseDown(float x, float, const IMouseMod&) override
  {
    /* left = ratio (MODE 0), right = fixed freq (MODE 1). */
    sk_host_fm6_set_byte(Resolve(mOff, mOpRel), x < mRECT.MW() ? 0 : 1);
    sk_host_ui_pump();
    SetDirty(false);
    PulseEgGraphs();
  }

private:
  uint32_t mOff = 0;
  bool mOpRel = false;
};

/** A 0/1 byte as a two-label toggle (LFO key sync, osc key sync). */
class ByteToggle final : public IControl
{
public:
  ByteToggle(const IRECT& bounds, uint32_t off, const char* offLabel, const char* onLabel, bool opRel = false)
  : IControl(bounds)
  , mOff(off)
  , mOffLabel(offLabel ? offLabel : "OFF")
  , mOnLabel(onLabel ? onLabel : "ON")
  , mOpRel(opRel)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool on = sk_host_fm6_get_byte(Resolve(mOff, mOpRel)) != 0;
    g.FillRoundRect(on ? skmint::kTheme : skmint::kWell, mRECT, 3.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 3.f, 0, 1.f);
    char t[24];
    snprintf(t, sizeof t, "%s %s", mOffLabel.c_str(), on ? mOnLabel.c_str() : "off");
    g.DrawText(IText(11.f, on ? skmint::InkOn(skmint::kTheme) : skmint::kText,
                     skmint::kFont, EAlign::Center, EVAlign::Middle),
               t, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    const uint32_t o = Resolve(mOff, mOpRel);
    sk_host_fm6_set_byte(o, sk_host_fm6_get_byte(o) ? 0 : 1);
    sk_host_ui_pump();
    SetDirty(false);
  }

private:
  uint32_t mOff = 0;
  std::string mOffLabel;
  std::string mOnLabel;
  bool mOpRel = false;
};

/** OP select (Dexed's 1..6 blocks): picks SelOp(); a fill shows output level. */
class OpSelectButton final : public IControl
{
public:
  OpSelectButton(const IRECT& bounds, int op1to6)
  : IControl(bounds)
  , mOp(std::clamp(op1to6, 1, 6))
  {
    LiveEgGraphs().push_back(this); /* share the per-byte immediate refresh pulse */
  }

  ~OpSelectButton() override
  {
    auto& v = LiveEgGraphs();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }

  void Draw(IGraphics& g) override
  {
    const bool sel = SelOp() == mOp;
    const uint32_t base = OpBase(mOp);
    const uint8_t lvl = sk_host_fm6_get_byte(base + kOl);
    const IColor ink = skmint::kText;

    g.FillRoundRect(skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(sel ? skmint::kAccent : skmint::kTheme, mRECT, 4.f, 0, sel ? 2.f : 1.f);

    /* Clear three-tier hierarchy: operator number, level meter, then its own
     * live envelope. The earlier label/mode rects overlapped and hid OP text. */
    const float labelH = std::min(13.f, mRECT.H() * 0.28f);
    const IRECT label = mRECT.GetFromTop(labelH);
    char t[8];
    snprintf(t, sizeof t, "OP%d", mOp);
    g.DrawText(IText(std::min(13.f, labelH), ink, skmint::kFont, EAlign::Center, EVAlign::Middle), t, label);

    const float meterH = std::max(3.f, std::min(5.f, mRECT.H() * 0.11f));
    const IRECT meter = IRECT(mRECT.L + 5.f, label.B + 1.f, mRECT.R - 5.f, label.B + 1.f + meterH);
    g.FillRoundRect(skmint::kSurf, meter, 2.f);
    const float frac = std::clamp((float) lvl / 99.f, 0.f, 1.f);
    if (frac > 0.f)
      g.FillRoundRect(skmint::kAccent, IRECT(meter.L, meter.T, meter.L + meter.W() * frac, meter.B), 2.f);
    char lvTxt[8];
    snprintf(lvTxt, sizeof lvTxt, "%u", (unsigned) lvl);
    g.DrawText(IText(8.f, ink, skmint::kFont, EAlign::Far, EVAlign::Middle),
               lvTxt, IRECT(mRECT.L, meter.T - 1.f, mRECT.R - 3.f, meter.B + 1.f));

    const IRECT eg = IRECT(mRECT.L + 5.f, meter.B + 2.f, mRECT.R - 5.f, mRECT.B - 4.f);
    if (eg.W() > 8.f && eg.H() > 4.f)
    {
      float rate[4], level[4];
      for (int i = 0; i < 4; ++i)
      {
        rate[i] = (float) sk_host_fm6_get_byte(base + kR1 + (uint32_t)i);
        level[i] = (float) sk_host_fm6_get_byte(base + kL1 + (uint32_t)i);
      }
      auto yAt = [&](float v) { return eg.B - eg.H() * (std::clamp(v, 0.f, 99.f) / 99.f); };
      auto segW = [&](float v) {
        const float slow = 1.f - std::clamp(v, 0.f, 99.f) / 99.f;
        return eg.W() * (0.08f + 0.22f * slow);
      };
      float px = eg.L;
      float py = yAt(level[3]);
      const float x0 = px;
      for (int i = 0; i < 4; ++i)
      {
        const float nx = std::min(eg.R, px + segW(rate[i]));
        const float ny = yAt(level[i]);
        g.DrawLine(ink, px, py, nx, ny, 0, 1.35f);
        px = nx;
        py = ny;
      }
      if (px < eg.R)
        g.DrawLine(ink, px, py, eg.R, py, 0, 1.f);
      (void) x0;
    }
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    SelOp() = mOp;
    PulseEgGraphs();
    if (GetUI())
      GetUI()->SetAllControlsDirty();
    SetDirty(false);
  }

private:
  int mOp = 1;
};

/** Amplitude EG graph of the selected operator (Dexed's draggable EG). */
class EgGraph final : public IControl
{
public:
  explicit EgGraph(const IRECT& bounds)
  : IControl(bounds)
  {
    LiveEgGraphs().push_back(this);
  }

  ~EgGraph() override
  {
    auto& v = LiveEgGraphs();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }

  void Draw(IGraphics& g) override
  {
    LayoutPoints();
    g.FillRect(skmint::kBg, mRECT);
    g.DrawRect(skmint::kTheme, mRECT);

    for (int i = 0; i < 4; ++i)
    {
      const float midX = (mX[i] + mX[i + 1]) * 0.5f;
      g.DrawLine(IColor(40, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                 midX, mPlot.B, midX, (mY[i] + mY[i + 1]) * 0.5f, 0, 1.f);
    }
    for (int i = 0; i < 4; ++i)
      g.DrawLine(skmint::kTheme, mX[i], mY[i], mX[i + 1], mY[i + 1], 0, 2.f);
    if (mX[4] < mPlot.R)
      g.DrawLine(skmint::kTheme, mX[4], mY[4], mPlot.R, mY[4], 0, 1.25f);

    for (int i = 0; i < 5; ++i)
    {
      const bool hot = (mDragPt == i) || (mHoverPt == i);
      g.FillCircle(hot ? skmint::kAccent : skmint::kTheme, mX[i], mY[i], hot ? 4.5f : 3.f);
      if (hot)
        g.DrawCircle(skmint::kInk, mX[i], mY[i], 4.5f, 0, 1.f);
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod&) override
  {
    LayoutPoints();
    const int hit = HitTest(x, y);
    if (hit != mHoverPt)
    {
      mHoverPt = hit;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    if (mHoverPt >= 0)
    {
      mHoverPt = -1;
      SetDirty(false);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    LayoutPoints();
    mDragPt = HitTest(x, y);
    if (mDragPt < 0)
      mDragPt = NearestPoint(x, y); /* grab nearest if close enough to the curve */
    SetDirty(false);
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    mDragPt = -1;
    SetDirty(false);
    PulseEgGraphs();
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override
  {
    if (mDragPt < 0)
      return;
    LayoutPoints(); /* keep mPlot current */

    /* Vertical -> level; horizontal -> rate of the segment into this point. */
    const float lv = LevelFromY(y);
    if (mDragPt == 0 || mDragPt == 4)
      sk_host_fm6_set_byte(LevelOff() + 3u, (uint8_t) std::lround(lv)); /* L4 */
    else
      sk_host_fm6_set_byte(LevelOff() + (uint32_t)(mDragPt - 1), (uint8_t) std::lround(lv));

    if (mDragPt >= 1)
    {
      const float prevX = mX[mDragPt - 1];
      const float w = std::clamp(x - prevX, mPlot.W() * 0.06f, mPlot.W() * 0.34f);
      sk_host_fm6_set_byte(RateOff() + (uint32_t)(mDragPt - 1), RateFromSegW(w));
    }

    sk_host_ui_pump();
    LayoutPoints();
    SetDirty(false);
    PulseEgGraphs();
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

private:
  uint32_t RateOff() const { return OpBase(SelOp()) + kR1; }
  uint32_t LevelOff() const { return OpBase(SelOp()) + kL1; }

  void LayoutPoints()
  {
    float rate[4], level[4];
    for (int i = 0; i < 4; ++i)
    {
      rate[i] = (float) sk_host_fm6_get_byte(RateOff() + (uint32_t) i);
      level[i] = (float) sk_host_fm6_get_byte(LevelOff() + (uint32_t) i);
    }
    mPlot = mRECT.GetPadded(-3.f);
    auto yAt = [&](float lv) { return mPlot.B - mPlot.H() * (std::clamp(lv, 0.f, 99.f) / 99.f); };
    auto segW = [&](float r) {
      const float t = 1.f - std::clamp(r, 0.f, 99.f) / 99.f;
      return mPlot.W() * (0.08f + 0.22f * t);
    };
    mX[0] = mPlot.L;
    mY[0] = yAt(level[3]);
    mX[1] = mX[0] + segW(rate[0]);
    mY[1] = yAt(level[0]);
    mX[2] = mX[1] + segW(rate[1]);
    mY[2] = yAt(level[1]);
    mX[3] = mX[2] + segW(rate[2]);
    mY[3] = yAt(level[2]);
    mX[4] = std::min(mPlot.R, mX[3] + segW(rate[3]));
    mY[4] = yAt(level[3]);
  }

  float LevelFromY(float y) const
  {
    if (mPlot.H() <= 1.f)
      return 0.f;
    return std::clamp((mPlot.B - y) / mPlot.H(), 0.f, 1.f) * 99.f;
  }

  uint8_t RateFromSegW(float w) const
  {
    if (mPlot.W() <= 1.f)
      return 0;
    const float nw = std::clamp(w / mPlot.W(), 0.08f, 0.30f);
    const float t = (nw - 0.08f) / 0.22f; /* 1 = slow/wide, 0 = fast/narrow */
    return (uint8_t) std::lround(std::clamp(99.f * (1.f - t), 0.f, 99.f));
  }

  int HitTest(float x, float y) const
  {
    const float hitR = 10.f;
    int best = -1;
    float bestD = hitR * hitR;
    for (int i = 0; i < 5; ++i)
    {
      const float dx = x - mX[i], dy = y - mY[i];
      const float d = dx * dx + dy * dy;
      if (d <= bestD)
      {
        bestD = d;
        best = i;
      }
    }
    return best;
  }

  int NearestPoint(float x, float y) const
  {
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < 5; ++i)
    {
      const float dx = x - mX[i], dy = y - mY[i];
      const float d = dx * dx + dy * dy;
      if (d < bestD)
      {
        bestD = d;
        best = i;
      }
    }
    return bestD < 10.f * 10.f ? best : -1;
  }

  IRECT mPlot;
  float mX[5] {}, mY[5] {};
  int mDragPt = -1;
  int mHoverPt = -1;
};

/** Pitch EG graph of the whole voice (FP_PR1..PR4 / FP_PL1..PL4). Levels are
 * drawn around the 50 centre line; drag a handle vertically for the level and
 * horizontally for the rate of the segment into it, exactly like the op EG. */
class PitchEgGraph final : public IControl
{
public:
  explicit PitchEgGraph(const IRECT& bounds)
  : IControl(bounds)
  {
    LiveEgGraphs().push_back(this);
  }

  ~PitchEgGraph() override
  {
    auto& v = LiveEgGraphs();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }

  void Draw(IGraphics& g) override
  {
    LayoutPoints();
    g.FillRect(skmint::kBg, mRECT);
    g.DrawRect(skmint::kTheme, mRECT);
    g.DrawLine(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
               mPlot.L, mYc, mPlot.R, mYc, 0, 1.f); /* 50 == centre */
    for (int i = 0; i < 4; ++i)
      g.DrawLine(skmint::kTheme, mX[i], mY[i], mX[i + 1], mY[i + 1], 0, 2.f);
    if (mX[4] < mPlot.R)
      g.DrawLine(skmint::kTheme, mX[4], mY[4], mPlot.R, mY[4], 0, 1.25f);
    for (int i = 0; i < 5; ++i)
    {
      const bool hot = (mDragPt == i) || (mHoverPt == i);
      g.FillCircle(hot ? skmint::kAccent : skmint::kTheme, mX[i], mY[i], hot ? 4.5f : 3.f);
      if (hot)
        g.DrawCircle(skmint::kInk, mX[i], mY[i], 4.5f, 0, 1.f);
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod&) override
  {
    LayoutPoints();
    const int hit = HitTest(x, y);
    if (hit != mHoverPt)
    {
      mHoverPt = hit;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    if (mHoverPt >= 0)
    {
      mHoverPt = -1;
      SetDirty(false);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    LayoutPoints();
    mDragPt = HitTest(x, y);
    if (mDragPt < 0)
      mDragPt = NearestPoint(x, y);
    SetDirty(false);
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    mDragPt = -1;
    SetDirty(false);
    PulseEgGraphs();
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override
  {
    if (mDragPt < 0)
      return;
    LayoutPoints();

    const float lv = LevelFromY(y);
    if (mDragPt == 0 || mDragPt == 4)
      sk_host_fm6_set_byte(kPl1 + 3u, (uint8_t) std::lround(lv)); /* PL4 */
    else
      sk_host_fm6_set_byte(kPl1 + (uint32_t)(mDragPt - 1), (uint8_t) std::lround(lv));

    if (mDragPt >= 1)
    {
      const float prevX = mX[mDragPt - 1];
      const float w = std::clamp(x - prevX, mPlot.W() * 0.06f, mPlot.W() * 0.34f);
      sk_host_fm6_set_byte(kPr1 + (uint32_t)(mDragPt - 1), RateFromSegW(w));
    }

    sk_host_ui_pump();
    LayoutPoints();
    SetDirty(false);
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

private:
  void LayoutPoints()
  {
    float rate[4], level[4];
    for (int i = 0; i < 4; ++i)
    {
      rate[i] = (float) sk_host_fm6_get_byte(kPr1 + (uint32_t) i);
      level[i] = (float) sk_host_fm6_get_byte(kPl1 + (uint32_t) i);
    }
    mPlot = mRECT.GetPadded(-3.f);
    mYc = mPlot.B - mPlot.H() * (50.f / 99.f); /* the 0..99 level's centre */
    auto yAt = [&](float lv) { return mPlot.B - mPlot.H() * (std::clamp(lv, 0.f, 99.f) / 99.f); };
    auto segW = [&](float r) {
      const float t = 1.f - std::clamp(r, 0.f, 99.f) / 99.f;
      return mPlot.W() * (0.08f + 0.22f * t);
    };
    mX[0] = mPlot.L;
    mY[0] = yAt(level[3]);
    mX[1] = mX[0] + segW(rate[0]);
    mY[1] = yAt(level[0]);
    mX[2] = mX[1] + segW(rate[1]);
    mY[2] = yAt(level[1]);
    mX[3] = mX[2] + segW(rate[2]);
    mY[3] = yAt(level[2]);
    mX[4] = std::min(mPlot.R, mX[3] + segW(rate[3]));
    mY[4] = yAt(level[3]);
  }

  float LevelFromY(float y) const
  {
    if (mPlot.H() <= 1.f)
      return 0.f;
    return std::clamp((mPlot.B - y) / mPlot.H(), 0.f, 1.f) * 99.f;
  }

  uint8_t RateFromSegW(float w) const
  {
    if (mPlot.W() <= 1.f)
      return 0;
    const float nw = std::clamp(w / mPlot.W(), 0.08f, 0.30f);
    const float t = (nw - 0.08f) / 0.22f;
    return (uint8_t) std::lround(std::clamp(99.f * (1.f - t), 0.f, 99.f));
  }

  int HitTest(float x, float y) const
  {
    const float hitR = 10.f;
    int best = -1;
    float bestD = hitR * hitR;
    for (int i = 0; i < 5; ++i)
    {
      const float dx = x - mX[i], dy = y - mY[i];
      const float d = dx * dx + dy * dy;
      if (d <= bestD)
      {
        bestD = d;
        best = i;
      }
    }
    return best;
  }

  int NearestPoint(float x, float y) const
  {
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < 5; ++i)
    {
      const float dx = x - mX[i], dy = y - mY[i];
      const float d = dx * dx + dy * dy;
      if (d < bestD)
      {
        bestD = d;
        best = i;
      }
    }
    return bestD < 10.f * 10.f ? best : -1;
  }

  IRECT mPlot;
  float mX[5] {}, mY[5] {};
  float mYc = 0.f;
  int mDragPt = -1;
  int mHoverPt = -1;
};

/* 32 FM algorithms (0..31), each specifies carrier layout and connections.
 * Copied from fm6_core.c / ui_graph.c so the VST draws the same chart. */
static const uint8_t FM6_CELL[32][6] = {
    {0x00, 0x10, 0x01, 0x11, 0x21, 0x31}, {0x00, 0x10, 0x02, 0x12, 0x22, 0x32}, {0x00, 0x10, 0x20, 0x01, 0x11, 0x21},
    {0x00, 0x10, 0x20, 0x01, 0x11, 0x21}, {0x00, 0x10, 0x01, 0x11, 0x02, 0x12}, {0x00, 0x10, 0x01, 0x11, 0x02, 0x12},
    {0x00, 0x10, 0x01, 0x11, 0x12, 0x22}, {0x00, 0x10, 0x01, 0x12, 0x11, 0x21}, {0x00, 0x10, 0x02, 0x12, 0x13, 0x23},
    {0x00, 0x10, 0x20, 0x01, 0x11, 0x12}, {0x00, 0x10, 0x20, 0x01, 0x11, 0x12}, {0x00, 0x10, 0x03, 0x12, 0x13, 0x14},
    {0x00, 0x10, 0x02, 0x11, 0x12, 0x13}, {0x00, 0x10, 0x01, 0x11, 0x20, 0x21}, {0x00, 0x10, 0x02, 0x12, 0x21, 0x22},
    {0x01, 0x10, 0x11, 0x21, 0x12, 0x22}, {0x01, 0x12, 0x10, 0x20, 0x11, 0x21}, {0x01, 0x10, 0x12, 0x11, 0x21, 0x31},
    {0x00, 0x10, 0x20, 0x01, 0x02, 0x11}, {0x00, 0x01, 0x10, 0x02, 0x12, 0x13}, {0x00, 0x01, 0x10, 0x02, 0x03, 0x12},
    {0x00, 0x10, 0x01, 0x02, 0x03, 0x12}, {0x00, 0x01, 0x11, 0x02, 0x03, 0x12}, {0x00, 0x01, 0x02, 0x03, 0x04, 0x13},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x13}, {0x00, 0x01, 0x11, 0x02, 0x12, 0x13}, {0x00, 0x01, 0x11, 0x03, 0x13, 0x14},
    {0x00, 0x10, 0x01, 0x11, 0x21, 0x02}, {0x00, 0x01, 0x02, 0x12, 0x03, 0x13}, {0x00, 0x01, 0x02, 0x12, 0x22, 0x03},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x14}, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05}};

static const uint8_t FM6_ALG[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};

/* m[op] bit j: operator j modulates operator op (as fm6_routes). */
static void GetAlgRoutes(uint32_t a, uint8_t* m)
{
  const uint8_t* f = FM6_ALG[a & 31u];
  uint8_t bus[4] = {0, 0, 0, 0};
  for (uint32_t k = 0; k < 6u; k++)
  {
    uint32_t op = 5u - k, in = (f[k] >> 4) & 3u, o = f[k] & 3u;
    m[op] = in ? bus[in] : 0u;
    bus[o] = (uint8_t)(((f[k] & 0x08u) ? bus[o] : 0u) | 1u << op);
  }
}

static uint32_t GetAlgCarriers(uint32_t a)
{
  uint32_t r = 0;
  const uint8_t* f = FM6_ALG[a & 31u];
  for (uint32_t k = 0; k < 6u; k++)
    if ((f[k] & 3u) == 0)
      r |= 1u << (5u - k);
  return r;
}

/* the operator with the feedback loop (0..5), as fm6_fb_op. */
static uint32_t GetAlgFeedbackOp(uint32_t a)
{
  uint32_t k;
  for (k = 0; k < 5u && (FM6_ALG[a & 31u][5u - k] & 0xC0u) != 0xC0u; k++)
    ;
  return k;
}

/** The algorithm chart — Dexed-style routing with feedback loop and output bus.
 * Ports ui_graph.c:graph_fm6 to the widget. Click / wheel picks the algorithm. */
class AlgDiagram final : public IControl
{
public:
  explicit AlgDiagram(const IRECT& bounds)
  : IControl(bounds)
  {
    skfm6::LiveEgGraphs().push_back(this);
  }

  ~AlgDiagram() override
  {
    auto& v = skfm6::LiveEgGraphs();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }

  void Draw(IGraphics& g) override
  {
    const uint32_t sel = sk_host_selected_track();
    const int pE0 = sk_host_trk_param(sel, skdesk::kP_E0);
    const int curAlgByte = (int) sk_host_fm6_get_byte(kAlg) & 31;
    const int alg = (pE0 >= 1 && pE0 <= 32) ? (pE0 - 1) : curAlgByte;
    if (alg != curAlgByte)
      sk_host_fm6_set_byte(kAlg, (uint8_t) alg);

    g.FillRoundRect(skmint::kSurf, mRECT, 6.f);
    g.DrawRoundRect(IColor(90, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), mRECT, 6.f, 0, 1.25f);

    /* The chart is designed on a 240 x 100 canvas (ui_graph.c). */
    const float cw = 240.f, ch = 104.f;
    const float s = std::min(mRECT.W() / cw, mRECT.H() / ch);
    const float ox = mRECT.MW() - cw * s * 0.5f;
    const float oy = mRECT.MH() - ch * s * 0.5f + 2.f * s;
    auto SX = [&](float x) { return ox + x * s; };
    auto SY = [&](float y) { return oy + y * s; };
    const float lw = std::max(1.f, s * 1.4f);

    auto vline = [&](float x, float ya, float yb, const IColor& c) {
      g.DrawLine(c, SX(x), SY(ya), SX(x), SY(yb), 0, lw);
    };
    auto hline = [&](float xa, float xb, float y, const IColor& c) {
      g.DrawLine(c, SX(xa), SY(y), SX(xb), SY(y), 0, lw);
    };
    auto dot = [&](float x, float y, const IColor& c) {
      g.FillCircle(c, SX(x), SY(y), std::max(1.5f, s * 1.4f));
    };

    uint8_t m[6];
    GetAlgRoutes((uint32_t) alg, m);
    const uint32_t car = GetAlgCarriers((uint32_t) alg);
    const uint32_t fbop = GetAlgFeedbackOp((uint32_t) alg);

    uint32_t top = 0, hi = 0;
    uint8_t fan[6] = {0, 0, 0, 0, 0, 0};
    for (int k = 0; k < 6; ++k)
    {
      const uint32_t col = FM6_CELL[alg][k] & 7u, row = FM6_CELL[alg][k] >> 4;
      hi = col > hi ? col : hi;
      top = row > top ? row : top;
      for (int j = 0; j < 6; ++j)
        fan[j] = (uint8_t)(fan[j] + ((m[k] >> j) & 1u));
    }

    const float BH = 17.f, BW = 21.f;
    const float bus = 50.f + (27.f + 23.f * (float) top) * 0.5f;
    float x[6], y[6];
    float cl = cw, cr = 0.f;
    for (int k = 0; k < 6; ++k)
    {
      x[k] = 120.f + (float)(FM6_CELL[alg][k] & 7u) * 24.f - (float) hi * 12.f;
      y[k] = bus - 6.f - BH - 23.f * (float)(FM6_CELL[alg][k] >> 4);
      if (car & (1u << k))
      {
        cl = x[k] < cl ? x[k] : cl;
        cr = x[k] > cr ? x[k] : cr;
      }
    }

    const IColor routeCol = skmint::kTheme;
    for (int k = 0; k < 6; ++k)
    {
      for (int j = 0; j < 6; ++j)
      {
        if ((m[k] >> j) & 1u)
        {
          /* down from j, across, down into k */
          const float jy = y[k] - 3.f;
          vline(x[j], y[j] + BH, jy, routeCol);
          hline(x[j], x[k], jy, routeCol);
          vline(x[k], jy, y[k], routeCol);
          if (fan[j] > 1u)
            dot(x[j], jy, routeCol);
          if (m[k] & (uint8_t)(m[k] - 1u))
            dot(x[k], jy, routeCol);
        }
      }
      if (car & (1u << k))
      {
        vline(x[k], y[k] + BH, bus, skmint::kTheme);
        if (x[k] != cl)
          dot(x[k], bus, skmint::kTheme);
      }
    }

    /* output bus and its arrow */
    hline(cl, cr + 24.f, bus, skmint::kTheme);
    g.DrawLine(skmint::kTheme, SX(cr + 20.f), SY(bus - 3.f), SX(cr + 23.f), SY(bus), 0, lw);
    g.DrawLine(skmint::kTheme, SX(cr + 20.f), SY(bus + 3.f), SX(cr + 23.f), SY(bus), 0, lw);

    /* feedback loop on its operator */
    {
      const float fx = x[fbop], fy = y[fbop];
      const uint8_t fb = sk_host_fm6_get_byte(kFb);
      const IColor fc = fb ? skmint::kAccent : skmint::kTheme;
      g.DrawLine(fc, SX(fx + 11.f), SY(fy + 5.f), SX(fx + 16.f), SY(fy + 5.f), 0, lw);
      g.DrawLine(fc, SX(fx + 16.f), SY(fy + 5.f), SX(fx + 16.f), SY(fy - 4.f), 0, lw);
      g.DrawLine(fc, SX(fx + 16.f), SY(fy - 4.f), SX(fx - 10.f), SY(fy - 4.f), 0, lw);
      g.DrawLine(fc, SX(fx - 10.f), SY(fy - 4.f), SX(fx - 10.f), SY(fy + 1.f), 0, lw);
    }

    for (int k = 0; k < 6; ++k)
    {
      const bool isCar = (car >> k) & 1u;
      const uint8_t ol = sk_host_fm6_get_byte(OpBase(k + 1) + kOl);
      const bool on = ol != 0;
      const IColor fill = (isCar && on) ? skmint::kTheme : skmint::kWell;
      const IRECT box(SX(x[k] - 10.f), SY(y[k]), SX(x[k] + 11.f), SY(y[k] + BH));
      g.FillRoundRect(fill, box, 3.f);
      g.DrawRoundRect(skmint::kTheme, box, 3.f, 0, 1.f);
      const IColor ink = (isCar && on) ? skmint::InkOn(skmint::kTheme)
                                       : (on ? skmint::kTheme : skmint::kWell);
      char n[2] = {(char)('1' + k), 0};
      g.DrawText(IText(std::max(9.f, s * 11.f), ink, skmint::kFont, EAlign::Center, EVAlign::Middle), n, box);
    }

    char lab[16];
    snprintf(lab, sizeof lab, "ALG %02d", alg + 1);
    g.DrawText(IText(14.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Top),
               lab, mRECT.GetPadded(-6.f));
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mAlgMenu.Clear();
    const uint32_t sel = sk_host_selected_track();
    const int pE0 = sk_host_trk_param(sel, skdesk::kP_E0);
    const int curAlg = (pE0 >= 1 && pE0 <= 32) ? (pE0 - 1) : ((int) sk_host_fm6_get_byte(kAlg) & 31);
    for (int i = 0; i < 32; ++i)
    {
      char item[32];
      snprintf(item, sizeof item, "Algorithm %d", i + 1);
      mAlgMenu.AddItem(item);
    }
    mAlgMenu.SetChosenItemIdx(curAlg);
    if (GetUI())
      GetUI()->CreatePopupMenu(*this, mAlgMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    (void) valIdx;
    if (pSelectedMenu)
    {
      const int chosen = pSelectedMenu->GetChosenItemIdx();
      if (chosen >= 0 && chosen < 32)
      {
        const uint32_t sel = sk_host_selected_track();
        sk_host_fm6_set_byte(kAlg, (uint8_t) chosen);
        sk_host_set_trk_param(sel, skdesk::kP_E0, chosen + 1);
        sk_host_ui_pump();
        SetDirty(false);
        PulseEgGraphs();
      }
    }
  }

  void OnMouseWheel(float, float, const IMouseMod&, float d) override
  {
    const uint32_t sel = sk_host_selected_track();
    const int pE0 = sk_host_trk_param(sel, skdesk::kP_E0);
    const int curAlg = (pE0 >= 1 && pE0 <= 32) ? (pE0 - 1) : ((int) sk_host_fm6_get_byte(kAlg) & 31);
    const int next = std::clamp(curAlg + (d > 0.f ? 1 : -1), 0, 31);
    sk_host_fm6_set_byte(kAlg, (uint8_t) next);
    sk_host_set_trk_param(sel, skdesk::kP_E0, next + 1);
    sk_host_ui_pump();
    SetDirty(false);
    PulseEgGraphs();
  }

private:
  IPopupMenu mAlgMenu;
};

using AlgGrid = AlgDiagram;

/** "OPERATOR n" header for the middle column. */
class OpHeader final : public IControl
{
public:
  explicit OpHeader(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kBg, mRECT, 3.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 3.f, 0, 1.f);
    char t[32];
    snprintf(t, sizeof t, "OPERATOR %d", SelOp());
    g.DrawText(IText(14.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Middle),
               t, mRECT.GetPadded(-8.f));
  }

  void OnMouseDown(float, float, const IMouseMod&) override {}
};

/** A titled module panel (SkMint surface + accent title). */
class DeskPanel final : public IControl
{
public:
  DeskPanel(const IRECT& bounds, const char* title)
  : IControl(bounds)
  , mTitle(title ? title : "")
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kSurf, mRECT, 6.f);
    g.DrawRoundRect(IColor(90, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), mRECT, 6.f, 0, 1.25f);
    if (!mTitle.empty())
      g.DrawText(IText(12.f, skmint::kAccent, skmint::kFont, EAlign::Near, EVAlign::Top),
                 mTitle.c_str(), mRECT.GetPadded(-8.f).GetFromTop(14.f));
  }

  void OnMouseDown(float, float, const IMouseMod&) override {}

private:
  std::string mTitle;
};

/** The 10-character patch name (FP_NAME..FP_NAME+9). */
class PatchName final : public IControl
{
public:
  explicit PatchName(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kBg, mRECT, 3.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 3.f, 0, 1.f);
    char nm[11];
    for (int i = 0; i < 10; ++i)
    {
      const uint8_t c = sk_host_fm6_get_byte(kName + (uint32_t) i);
      nm[i] = (c >= 32u && c <= 126u) ? (char) c : ' ';
    }
    nm[10] = 0;
    for (int i = 9; i >= 0 && nm[i] == ' '; --i)
      nm[i] = 0;
    g.DrawText(IText(14.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle), nm, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override {}
};

/* --------------------------------------------------------------- layout --- */

/** Place `n` controls across `row` and hand each its cell to `fn`. */
template <class Fn>
inline void GridRow(const IRECT& row, int n, float pad, Fn&& fn)
{
  for (int i = 0; i < n; ++i)
    fn(row.GetGridCell(i, 1, n).GetPadded(-pad), i);
}

/** One of `n` stacked rows of `area`. */
inline IRECT StackedRow(const IRECT& area, int i, int n)
{
  const float rh = area.H() / (float) std::max(1, n);
  return IRECT(area.L, area.T + (float) i * rh, area.R, area.T + (float)(i + 1) * rh);
}

/* Layout constants live near the top of the file (before ByteKnob). */

inline void AttachFm6Desk(const std::function<void(IControl*)>& attach,
                          const IRECT& body, const IVStyle& mintStyle)
{
  (void) mintStyle;
  const float W = body.W();
  const float H = body.H();
  if (W < 60.f || H < 60.f)
    return;

  /* Three columns: algorithm chart + OP select + globals | operator page |
   * pitch EG + LFO + voice. */
  const float c0w = W * 0.26f;
  const float c1w = W * 0.44f;
  const IRECT c0(body.L, body.T, body.L + c0w, body.B);
  const IRECT c1(body.L + c0w, body.T, body.L + c0w + c1w, body.B);
  const IRECT c2(body.L + c0w + c1w, body.T, body.R, body.B);

  /* ---- Left column: algorithm chart, OP select, globals ---- */
  {
    const IRECT col = c0.GetPadded(-2.f);
    const float algH = col.H() * 0.37f;
    const float opsH = col.H() * 0.42f;
    const IRECT algArea(col.L, col.T, col.R, col.T + algH);
    attach(new AlgDiagram(algArea.GetPadded(-2.f)));

    const IRECT selArea(col.L, algArea.B + 4.f, col.R, algArea.B + 4.f + opsH);
    for (int op = 1; op <= 6; ++op)
      attach(new OpSelectButton(selArea.GetGridCell(op - 1, 2, 3).GetPadded(-3.f), op));

    /* Globals: ALG (numeric), feedback, transpose. */
    const IRECT gRow = col.GetFromBottom(col.H() * 0.18f).GetPadded(-1.f);
    static const uint32_t kGOff[3] = {kAlg, kFb, kTrnsp};
    static const char* kGLab[3] = {"ALG", "FEEDBACK", "TRANSPOSE"};
    GridRow(gRow, 3, 3.f, [&](const IRECT& cell, int i) {
      attach(new ByteKnob(cell, kGOff[i], kGLab[i], 11.f));
    });
  }

  /* ---- Middle column: the selected operator (Dexed's operator page) ---- */
  {
    const IRECT col = c1.GetPadded(-3.f);
    const IRECT hdr = col.GetFromTop(fm6layout::kHeaderH);
    attach(new OpHeader(hdr.GetReducedFromRight(std::min(150.f, hdr.W() * 0.42f))));
    attach(new ModeToggle(hdr.GetFromRight(std::min(150.f, hdr.W() * 0.42f)).GetPadded(-2.f), kMode, true));

    const IRECT rest = col.GetReducedFromTop(fm6layout::kHeaderH + 4.f);
    const IRECT egArea = rest.GetFromTop(rest.H() * 0.30f).GetPadded(-1.f);
    attach(new EgGraph(egArea));

    const IRECT grid = rest.GetReducedFromTop(egArea.H() + 3.f);
    /* Four deliberate rows keep frequency tuning easy to find:
     * EG rates/levels; output + coarse/fine/detune; keyboard scaling; curves/sens. */
    static const uint32_t kEgOff[8] = {kR1, kR1 + 1, kR1 + 2, kR1 + 3, kL1, kL1 + 1, kL1 + 2, kL1 + 3};
    static const char* kEgLab[8] = {"R1", "R2", "R3", "R4", "L1", "L2", "L3", "L4"};
    static const uint32_t kFreqOff[4] = {kOl, kFc, kFf, kDet};
    static const char* kFreqLab[4] = {"LEVEL", "COARSE", "FINE", "TUNE"};
    static const uint32_t kScaleOff[4] = {kBp, kLd, kRd, kRs};
    static const char* kScaleLab[4] = {"BRK PT", "L DEPTH", "R DEPTH", "RATE SCALE"};
    static const uint32_t kSensOff[4] = {kLc, kRc, kAms, kKvs};
    static const char* kSensLab[4] = {"L CURVE", "R CURVE", "AMS", "KVS"};

    const float rowH = grid.H() * 0.25f;
    const IRECT row0(grid.L, grid.T, grid.R, grid.T + rowH);
    const IRECT row1(grid.L, grid.T + rowH, grid.R, grid.T + rowH * 2.f);
    const IRECT row2(grid.L, grid.T + rowH * 2.f, grid.R, grid.T + rowH * 3.f);
    const IRECT row3(grid.L, grid.T + rowH * 3.f, grid.R, grid.B);
    GridRow(row0.GetPadded(-1.f), 8, 2.f, [&](const IRECT& cell, int i) {
      attach(new ByteKnob(cell, kEgOff[i], kEgLab[i], 10.f, true));
    });
    GridRow(row1.GetPadded(-1.f), 4, 2.f, [&](const IRECT& cell, int i) {
      attach(new ByteKnob(cell, kFreqOff[i], kFreqLab[i], 10.f, true));
    });
    GridRow(row2.GetPadded(-1.f), 4, 2.f, [&](const IRECT& cell, int i) {
      attach(new ByteKnob(cell, kScaleOff[i], kScaleLab[i], 9.f, true));
    });
    GridRow(row3.GetPadded(-1.f), 4, 2.f, [&](const IRECT& cell, int i) {
      attach(new ByteKnob(cell, kSensOff[i], kSensLab[i], 9.f, true));
    });
  }

  /* ---- Right column: pitch EG, LFO, voice ---- */
  {
    static const char* kLfoWave[7] = {"TRI", "SAW DN", "SAW UP", "SQUARE", "SINE", "S&H", nullptr};

    const IRECT col = c2.GetPadded(-3.f);
    /* Compute the top and bottom modules first, then let LFO take exactly what
     * is left between them — otherwise the percentages can sum past 100 and the
     * panels overlap. */
    const IRECT peg = col.GetFromTop(col.H() * 0.36f);
    const IRECT info = col.GetFromBottom(col.H() * 0.22f);
    const IRECT lfo = col.GetReducedFromTop(peg.H() + 4.f).GetReducedFromBottom(info.H() + 4.f);

    /* PITCH EG */
    attach(new DeskPanel(peg, "PITCH EG"));
    {
      const IRECT in = peg.GetReducedFromTop(fm6layout::kPanelTitleH).GetPadded(-4.f);
      const IRECT gph = in.GetFromLeft(in.W() * 0.40f).GetPadded(-2.f).GetMidVPadded(in.H() * 0.34f);
      attach(new PitchEgGraph(gph));
      const IRECT kn = in.GetFromRight(in.W() * 0.58f);
      static const uint32_t kPr[4] = {kPr1, kPr1 + 1, kPr1 + 2, kPr1 + 3};
      static const uint32_t kPl[4] = {kPl1, kPl1 + 1, kPl1 + 2, kPl1 + 3};
      static const char* kPrN[4] = {"PR1", "PR2", "PR3", "PR4"};
      static const char* kPlN[4] = {"PL1", "PL2", "PL3", "PL4"};
      GridRow(StackedRow(kn, 0, 2).GetPadded(-1.f), 4, 2.f, [&](const IRECT& cell, int i) {
        attach(new ByteKnob(cell, kPr[i], kPrN[i], 10.f));
      });
      GridRow(StackedRow(kn, 1, 2).GetPadded(-1.f), 4, 2.f, [&](const IRECT& cell, int i) {
        attach(new ByteKnob(cell, kPl[i], kPlN[i], 10.f));
      });
    }

    /* LFO */
    attach(new DeskPanel(lfo, "LFO"));
    {
      static const char* kLfoNames[6] = {"WAVE", "SPEED", "DELAY", "PMD", "AMD", "PMS"};
      const uint32_t off[6] = {kLfw, kLfs, kLfd, kLpmd, kLamd, kLpms};
      const IRECT in = lfo.GetReducedFromTop(fm6layout::kPanelTitleH).GetPadded(-4.f);
      GridRow(StackedRow(in, 0, 2).GetPadded(-1.f), 3, 2.f, [&](const IRECT& cell, int i) {
        attach(new ByteKnob(cell, off[i], kLfoNames[i], 10.f, false, i == 0 ? kLfoWave : nullptr));
      });
      GridRow(StackedRow(in, 1, 2).GetPadded(-1.f), 3, 2.f, [&](const IRECT& cell, int i) {
        attach(new ByteKnob(cell, off[i + 3], kLfoNames[i + 3], 10.f));
      });
    }

    /* VOICE: patch name and the two key-sync toggles. */
    attach(new DeskPanel(info, "VOICE"));
    {
      const IRECT in = info.GetReducedFromTop(fm6layout::kPanelTitleH).GetPadded(-4.f);
      attach(new PatchName(in.GetFromTop(in.H() * 0.5f).GetPadded(-2.f)));
      const IRECT tog = in.GetFromBottom(in.H() * 0.5f).GetPadded(-2.f);
      attach(new ByteToggle(tog.GetGridCell(0, 1, 2).GetPadded(-3.f), kLks, "LFO", "KEY"));
      attach(new ByteToggle(tog.GetGridCell(1, 1, 2).GetPadded(-3.f), kOks, "OSC", "KEY"));
    }
  }
}

/* ------------------------------------------------------- shared pieces --- */

/** One MIXER strip — firmware TRACKS page / web editor mixer. */
class MixStrip final : public IControl
{
public:
  using SelectFn = std::function<void(uint32_t tr)>;

  MixStrip(const IRECT& bounds, uint32_t tr, SelectFn onSelect = nullptr)
  : IControl(bounds)
  , mTr(tr)
  , mOnSelect(std::move(onSelect))
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool sel = sk_host_selected_track() == mTr;
    const int32_t lvl = sk_host_trk_param(mTr, 0);   /* P_LEVEL */
    const int32_t pan = sk_host_trk_param(mTr, 39);  /* P_PAN */
    const int32_t rev = sk_host_trk_param(mTr, 36);  /* P_REV */
    const bool mute = sk_host_trk_param(mTr, 40) != 0;
    const char* nm = sk_host_trk_name(mTr);

    g.FillRoundRect(skmint::kBg, mRECT, 5.f);
    g.DrawRoundRect(sel ? skmint::kTheme : skmint::kWell, mRECT, 5.f, 0, sel ? 2.f : 1.f);

    char num[8];
    snprintf(num, sizeof num, "T%u", mTr + 1u);
    g.DrawText(IText(14.f, sel ? skmint::kTheme : skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Top),
               num, mRECT.GetPadded(-6.f).GetFromTop(10.f));
    g.DrawText(IText(13.f, mute ? skmint::kWell : skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Top),
               nm && nm[0] ? nm : "—", mRECT.GetPadded(-6.f).GetReducedFromTop(22.f).GetFromTop(28.f));

    /* Level fader (left) */
    const IRECT fader = mRECT.GetReducedFromTop(56.f).GetReducedFromBottom(72.f)
                            .GetFromLeft(mRECT.W() * 0.42f).GetCentredInside(28.f, mRECT.H() * 0.45f);
    const IRECT rail = fader.GetMidHPadded(3.f);
    g.FillRoundRect(skmint::kWell, rail, 2.f);
    const float fy = fader.B - fader.H() * ((float) std::clamp(lvl, 0, 127) / 127.f);
    g.FillRoundRect(mute ? skmint::kWell : skmint::kTheme,
                    IRECT(fader.L, fy - 6.f, fader.R, fy + 6.f), 3.f);
    char lv[8];
    snprintf(lv, sizeof lv, "%d", (int) lvl);
    g.DrawText(IText(13.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Top),
               lv, IRECT(fader.L, fader.B + 2.f, fader.R, fader.B + 10.f));

    /* PAN + REV knobs */
    const IRECT knobs = mRECT.GetReducedFromTop(56.f).GetReducedFromBottom(72.f).GetFromRight(mRECT.W() * 0.52f);
    DrawMiniKnob(g, knobs.GetFromTop(knobs.H() * 0.5f).GetPadded(-2.f), "PAN",
                 (double)(pan + 64) / 127.0, mute);
    DrawMiniKnob(g, knobs.GetFromBottom(knobs.H() * 0.5f).GetPadded(-2.f), "REV",
                 (double) std::clamp(rev, 0, 127) / 127.0, mute);

    /* MUTE / SEL */
    const IRECT row = mRECT.GetFromBottom(56.f).GetPadded(-6.f);
    const IRECT muteR = row.GetFromTop(24.f);
    const IRECT selR = row.GetFromBottom(24.f);
    g.FillRoundRect(mute ? skmint::kAccent : skmint::kWell, muteR, 3.f);
    g.DrawText(IText(13.f, mute ? skmint::InkOn(skmint::kAccent) : skmint::kInk,
                     skmint::kFont, EAlign::Center, EVAlign::Middle),
               "MUTE", muteR);
    g.FillRoundRect(sel ? skmint::kTheme : skmint::kWell, selR, 3.f);
    g.DrawText(IText(13.f, sel ? skmint::InkOn(skmint::kTheme) : skmint::kInk,
                     skmint::kFont, EAlign::Center, EVAlign::Middle),
               sel ? "SEL" : "SELECT", selR);

    mFader = fader;
    mMute = muteR;
    mSel = selR;
    mPan = knobs.GetFromTop(knobs.H() * 0.5f);
    mRev = knobs.GetFromBottom(knobs.H() * 0.5f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) mod;
    const IRECT hdr = mRECT.GetFromTop(44.f);
    if (hdr.Contains(x, y) || mSel.Contains(x, y))
    {
      sk_host_select_track(mTr);
      if (mOnSelect)
        mOnSelect(mTr);
    }
    else if (mMute.Contains(x, y))
    {
      const int32_t on = sk_host_trk_param(mTr, 40) ? 0 : 1;
      sk_host_set_trk_param(mTr, 40, on);
    }
    else if (mFader.Contains(x, y))
      mDrag = Drag::Level;
    else if (mPan.Contains(x, y))
      mDrag = Drag::Pan;
    else if (mRev.Contains(x, y))
      mDrag = Drag::Rev;
    else
      mDrag = Drag::None;
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float, float dY, const IMouseMod&) override
  {
    if (mDrag == Drag::Level)
    {
      const double n = std::clamp((double)((mFader.B - y) / mFader.H()), 0.0, 1.0);
      sk_host_set_trk_param(mTr, 0, (int32_t) std::lround(n * 127.0));
    }
    else if (mDrag == Drag::Pan)
    {
      int32_t v = sk_host_trk_param(mTr, 39) - (int32_t) std::lround(dY * 0.5);
      sk_host_set_trk_param(mTr, 39, std::clamp(v, -64, 63));
    }
    else if (mDrag == Drag::Rev)
    {
      int32_t v = sk_host_trk_param(mTr, 36) - (int32_t) std::lround(dY * 0.5);
      sk_host_set_trk_param(mTr, 36, std::clamp(v, 0, 127));
    }
    (void) x;
    SetDirty(false);
  }

  void OnMouseUp(float, float, const IMouseMod&) override { mDrag = Drag::None; }

private:
  enum class Drag { None, Level, Pan, Rev };

  static void DrawMiniKnob(IGraphics& g, const IRECT& r, const char* lab, double norm, bool dim)
  {
    g.DrawText(IText(12.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Top),
               lab, r.GetFromTop(12.f));
    const IRECT ring = r.GetReducedFromTop(12.f).GetCentredInside(std::min(r.W(), r.H() - 12.f) * 0.85f);
    g.FillEllipse(skmint::kWell, ring);
    g.DrawEllipse(dim ? skmint::kWell : skmint::kTheme, ring, 0, 1.2f);
    const float angle = -135.f + (float) norm * 270.f;
    const float cx = ring.MW(), cy = ring.MH(), rad = ring.W() * 0.32f;
    g.DrawRadialLine(dim ? skmint::kWell : skmint::kAccent, cx, cy, angle, 0.f, rad, nullptr, 1.5f);
  }

  uint32_t mTr = 0;
  Drag mDrag = Drag::None;
  IRECT mFader, mMute, mSel, mPan, mRev;
  SelectFn mOnSelect;
};

/** LangitCinta! mark — 4-bit coverage (firmware lc_logo.h). */
class LogoMark final : public IControl
{
public:
  explicit LogoMark(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(skmint::kWell, mRECT);
    const float cellW = mRECT.W() / (float) LC_LOGO_W;
    const float cellH = mRECT.H() / (float) LC_LOGO_H;
    const float scale = std::min(cellW, cellH);
    const float ox = mRECT.MW() - 0.5f * LC_LOGO_W * scale;
    const float oy = mRECT.MH() - 0.5f * LC_LOGO_H * scale;
    /* Tight nibble pack (cv_alpha): odd width does NOT pad to a byte per row. */
    for (uint32_t y = 0; y < LC_LOGO_H; ++y)
    {
      for (uint32_t x = 0; x < LC_LOGO_W; ++x)
      {
        const uint32_t k = y * LC_LOGO_W + x;
        const uint8_t packed = LC_LOGO[k >> 1];
        const uint8_t nib = (k & 1u) ? (packed & 0x0Fu) : (packed >> 4);
        if (!nib)
          continue;
        const int a = 255 * (int) nib / 15;
        g.FillRect(IColor(a, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                   IRECT(ox + x * scale, oy + y * scale,
                         ox + (x + 1) * scale, oy + (y + 1) * scale));
      }
    }
  }
};

} // namespace skfm6
