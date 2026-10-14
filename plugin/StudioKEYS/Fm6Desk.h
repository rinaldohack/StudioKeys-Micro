#pragma once

#include "IControls.h"
#include "SkMint.h"
#include "sk_host.h"
#include "../../firmware/src/lc_logo.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace iplug;
using namespace igraphics;

/** Dexed/Texed-style FM6 desk — voice bytes via sk_host_fm6_*. */
namespace skfm6 {

/* fm6_core.c FP_* — operator 0 = OP6. */
enum : uint32_t {
  kOp = 21u,
  kR1 = 0, kL1 = 4, kBp = 8, kLd = 9, kRd = 10,
  kAms = 14, kKvs = 15, kOl = 16, kMode = 17, kFc = 18, kFf = 19, kDet = 20,
  kPr1 = 126, kPl1 = 130, kAlg = 134, kFb = 135,
  kLfs = 137, kLfd = 138, kLpmd = 139, kLamd = 140, kLfw = 142
};

inline uint32_t OpBase(int op1to6)
{
  const int n = std::clamp(op1to6, 1, 6);
  return (uint32_t)(6 - n) * kOp;
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

class ByteKnob final : public IControl
{
public:
  ByteKnob(const IRECT& bounds, uint32_t off, const char* label, float labelPt = 12.f)
  : IControl(bounds)
  , mOff(off)
  , mLabel(label ? label : "")
  , mLabelPt(labelPt)
  {
    Sync();
  }

  void Sync()
  {
    if (mDragging)
      return;
    const uint8_t mx = MaxFor(mOff);
    double v = mx ? (double)sk_host_fm6_get_byte(mOff) / (double)mx : 0.0;
    mValue = std::clamp(v, 0.0, 1.0);
  }

  void Draw(IGraphics& g) override
  {
    Sync();
    const IRECT dial = mLabel.empty() ? mRECT : mRECT.GetReducedFromBottom(14.f);
    const float side = std::min(dial.W(), dial.H()) * 0.90f;
    const IRECT ring = dial.GetCentredInside(side);
    g.FillEllipse(skmint::kWell, ring);
    g.DrawEllipse(skmint::kTheme, ring, 0, 1.25f);
    const float cx = ring.MW(), cy = ring.MH(), r = ring.W() * 0.34f;
    const float angle = -135.f + (float) mValue * 270.f;
    g.DrawRadialLine(skmint::kAccent, cx, cy, angle, 0.f, r, nullptr, 1.75f);
    if (!mLabel.empty())
    {
      const uint8_t byteVal = sk_host_fm6_get_byte(mOff);
      char valStr[16];
      snprintf(valStr, sizeof valStr, "%s %u", mLabel.c_str(), (unsigned)byteVal);
      g.DrawText(IText(mLabelPt, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Bottom),
                 valStr, mRECT.GetFromBottom(14.f));
    }
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
    const uint8_t mx = MaxFor(mOff);
    /* Fine = shift; Dexed-like: every pixel step updates the host + EG graph. */
    const double sens = mod.S ? 0.0025 : 0.008;
    mValue = std::clamp(mValue - (double)dY * sens, 0.0, 1.0);
    sk_host_fm6_set_byte(mOff, (uint8_t)std::lround(mValue * (double)mx));
    SetDirty(false);
    PulseEgGraphs();
  }

  void OnMouseWheel(float, float, const IMouseMod& mod, float d) override
  {
    const uint8_t mx = MaxFor(mOff);
    const double step = mod.S ? (1.0 / std::max(1, (int)mx)) : (2.0 / std::max(1, (int)mx));
    mValue = std::clamp(mValue + (d > 0.f ? step : -step), 0.0, 1.0);
    sk_host_fm6_set_byte(mOff, (uint8_t)std::lround(mValue * (double)mx));
    SetDirty(false);
    PulseEgGraphs();
  }

private:
  uint32_t mOff = 0;
  double mValue = 0.0;
  std::string mLabel;
  float mLabelPt = 9.f;
  bool mDragging = false;
};

class ModeToggle final : public IControl
{
public:
  ModeToggle(const IRECT& bounds, uint32_t off)
  : IControl(bounds)
  , mOff(off)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool fixed = sk_host_fm6_get_byte(mOff) != 0;
    const IRECT a = mRECT.GetFromLeft(mRECT.W() * 0.5f).GetPadded(-1.f);
    const IRECT b = mRECT.GetFromRight(mRECT.W() * 0.5f).GetPadded(-1.f);
    g.FillRoundRect(fixed ? skmint::kTheme : skmint::kWell, a, 3.f);
    g.FillRoundRect(!fixed ? skmint::kTheme : skmint::kWell, b, 3.f);
    g.DrawText(IText(12.f, fixed ? skmint::InkOn(skmint::kTheme) : skmint::kInk,
                     skmint::kFont, EAlign::Center, EVAlign::Middle),
               "freq", a);
    g.DrawText(IText(12.f, !fixed ? skmint::InkOn(skmint::kTheme) : skmint::kInk,
                     skmint::kFont, EAlign::Center, EVAlign::Middle),
               "ratio", b);
  }

  void OnMouseDown(float x, float, const IMouseMod&) override
  {
    /* left = fixed freq, right = ratio (MODE 0). */
    sk_host_fm6_set_byte(mOff, x < mRECT.MW() ? 1 : 0);
    SetDirty(false);
  }

private:
  uint32_t mOff = 0;
};

class EgGraph final : public IControl
{
public:
  EgGraph(const IRECT& bounds, uint32_t rate0, uint32_t level0)
  : IControl(bounds)
  , mRate0(rate0)
  , mLevel0(level0)
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

    /* Vertical → level; horizontal → rate of the segment into this point. */
    const float lv = LevelFromY(y);
    if (mDragPt == 0 || mDragPt == 4)
      sk_host_fm6_set_byte(mLevel0 + 3u, (uint8_t)std::lround(lv)); /* L4 */
    else
      sk_host_fm6_set_byte(mLevel0 + (uint32_t)(mDragPt - 1), (uint8_t)std::lround(lv));

    if (mDragPt >= 1)
    {
      const float prevX = mX[mDragPt - 1];
      const float w = std::clamp(x - prevX, mPlot.W() * 0.06f, mPlot.W() * 0.34f);
      sk_host_fm6_set_byte(mRate0 + (uint32_t)(mDragPt - 1), RateFromSegW(w));
    }

    LayoutPoints();
    SetDirty(false);
    PulseEgGraphs();
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

private:
  void LayoutPoints()
  {
    float rate[4], level[4];
    for (int i = 0; i < 4; ++i)
    {
      rate[i] = (float)sk_host_fm6_get_byte(mRate0 + (uint32_t)i);
      level[i] = (float)sk_host_fm6_get_byte(mLevel0 + (uint32_t)i);
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
    return (uint8_t)std::lround(std::clamp(99.f * (1.f - t), 0.f, 99.f));
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

  uint32_t mRate0 = 0;
  uint32_t mLevel0 = 0;
  IRECT mPlot;
  float mX[5] {}, mY[5] {};
  int mDragPt = -1;
  int mHoverPt = -1;
};

// 32 FM algorithms (0..31), each specifies carrier layout and connections
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

static void GetAlgRoutes(uint32_t a, uint8_t *m)
{
    const uint8_t *f = FM6_ALG[a & 31u];
    uint8_t bus[4] = {0, 0, 0, 0};
    for (uint32_t k = 0; k < 6u; k++) {
        uint32_t op = 5u - k, in = (f[k] >> 4) & 3u, o = f[k] & 3u;
        m[op] = in ? bus[in] : 0u;
        bus[o] = (uint8_t)(((f[k] & 0x08u) ? bus[o] : 0u) | 1u << op);
    }
}

static uint32_t GetAlgCarriers(uint32_t a)
{
    uint32_t r = 0;
    const uint8_t *f = FM6_ALG[a & 31u];
    for (uint32_t k = 0; k < 6u; k++)
        if ((f[k] & 3u) == 0) r |= 1u << (5u - k);
    return r;
}

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
    const int curAlgByte = (int)sk_host_fm6_get_byte(kAlg) & 31;
    const int alg = (pE0 >= 1 && pE0 <= 32) ? (pE0 - 1) : curAlgByte;
    if (alg != curAlgByte)
      sk_host_fm6_set_byte(kAlg, (uint8_t)alg);

    g.FillRect(skmint::kBg, mRECT);

    uint8_t m[6];
    GetAlgRoutes(alg, m);
    uint32_t car = GetAlgCarriers(alg);

    const float bw = 20.f;
    const float bh = 15.f;
    float x[6], y[6];
    float cl = mRECT.R, cr = mRECT.L;

    int hi = 0;
    for (int k = 0; k < 6; k++) {
        int col = FM6_CELL[alg][k] & 7;
        if (col > hi) hi = col;
    }

    const float scale = std::min(mRECT.W() / (hi * 28.f + 40.f), mRECT.H() / 100.f);
    const float cx = mRECT.MW();
    const float cy = mRECT.MH() + 10.f;

    for (int k = 0; k < 6; ++k) {
        float px = (float)(FM6_CELL[alg][k] & 7) * 28.f - (hi * 14.f);
        float py = -10.f - bh - 24.f * (float)(FM6_CELL[alg][k] >> 4);
        x[k] = cx + px * scale;
        y[k] = cy + py * scale;
        if (car & (1 << k)) {
            if (x[k] < cl) cl = x[k];
            if (x[k] > cr) cr = x[k];
        }
    }

    const float busY = cy + 6.f;

    for (int k = 0; k < 6; ++k) {
        for (int j = 0; j < 6; ++j) {
            if (m[k] & (1 << j)) {
                g.DrawLine(skmint::kTheme, x[j], y[j] + bh/2.f, x[k], y[k] - bh/2.f, 0, 1.5f);
            }
        }
        if (car & (1 << k)) {
            g.DrawLine(skmint::kTheme, x[k], y[k] + bh/2.f, x[k], busY, 0, 1.5f);
        }
    }
    g.DrawLine(skmint::kTheme, cl - 4.f, busY, cr + 4.f, busY, 0, 2.f);

    for (int k = 0; k < 6; ++k) {
        IRECT box(x[k] - bw/2.f, y[k] - bh/2.f, x[k] + bw/2.f, y[k] + bh/2.f);
        const bool isCar = (car & (1 << k)) != 0;
        g.FillRoundRect(isCar ? skmint::kTheme : skmint::kWell, box, 2.f);
        g.DrawRoundRect(skmint::kTheme, box, 2.f);
        char n[2] = {(char)('1' + k), 0};
        g.DrawText(IText(13.f, isCar ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont), n, box);
    }

    char lab[16];
    snprintf(lab, sizeof lab, "ALG %d", alg + 1);
    g.DrawText(IText(15.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Top),
               lab, mRECT.GetPadded(-4.f));
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mAlgMenu.Clear();
    const uint32_t sel = sk_host_selected_track();
    const int pE0 = sk_host_trk_param(sel, skdesk::kP_E0);
    const int curAlg = (pE0 >= 1 && pE0 <= 32) ? (pE0 - 1) : ((int)sk_host_fm6_get_byte(kAlg) & 31);
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
        sk_host_fm6_set_byte(kAlg, (uint8_t)chosen);
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
    const int curAlg = (pE0 >= 1 && pE0 <= 32) ? (pE0 - 1) : ((int)sk_host_fm6_get_byte(kAlg) & 31);
    const int next = std::clamp(curAlg + (d > 0.f ? 1 : -1), 0, 31);
    sk_host_fm6_set_byte(kAlg, (uint8_t)next);
    sk_host_set_trk_param(sel, skdesk::kP_E0, next + 1);
    sk_host_ui_pump();
    SetDirty(false);
    PulseEgGraphs();
  }

private:
  IPopupMenu mAlgMenu;
};

using AlgGrid = AlgDiagram;

class OpChrome final : public IControl
{
public:
  OpChrome(const IRECT& bounds, int op1to6)
  : IControl(bounds)
  , mOp(op1to6)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kBg, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.f);
    char lab[8];
    snprintf(lab, sizeof lab, "OP%d", mOp);
    g.DrawText(IText(14.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Top),
               lab, mRECT.GetPadded(-6.f).GetFromTop(12.f));
  }

  void OnMouseDown(float, float, const IMouseMod&) override {}

private:
  int mOp = 1;
};

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
    const float fy = fader.B - fader.H() * ((float)std::clamp(lvl, 0, 127) / 127.f);
    g.FillRoundRect(mute ? skmint::kWell : skmint::kTheme,
                    IRECT(fader.L, fy - 6.f, fader.R, fy + 6.f), 3.f);
    char lv[8];
    snprintf(lv, sizeof lv, "%d", (int)lvl);
    g.DrawText(IText(13.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Top),
               lv, IRECT(fader.L, fader.B + 2.f, fader.R, fader.B + 10.f));

    /* PAN + REV knobs */
    const IRECT knobs = mRECT.GetReducedFromTop(56.f).GetReducedFromBottom(72.f).GetFromRight(mRECT.W() * 0.52f);
    DrawMiniKnob(g, knobs.GetFromTop(knobs.H() * 0.5f).GetPadded(-2.f), "PAN",
                 (double)(pan + 64) / 127.0, mute);
    DrawMiniKnob(g, knobs.GetFromBottom(knobs.H() * 0.5f).GetPadded(-2.f), "REV",
                 (double)std::clamp(rev, 0, 127) / 127.0, mute);

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
    (void)mod;
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
      sk_host_set_trk_param(mTr, 0, (int32_t)std::lround(n * 127.0));
    }
    else if (mDrag == Drag::Pan)
    {
      int32_t v = sk_host_trk_param(mTr, 39) - (int32_t)std::lround(dY * 0.5);
      sk_host_set_trk_param(mTr, 39, std::clamp(v, -64, 63));
    }
    else if (mDrag == Drag::Rev)
    {
      int32_t v = sk_host_trk_param(mTr, 36) - (int32_t)std::lround(dY * 0.5);
      sk_host_set_trk_param(mTr, 36, std::clamp(v, 0, 127));
    }
    (void)x;
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
    const float angle = -135.f + (float)norm * 270.f;
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
    const float cellW = mRECT.W() / (float)LC_LOGO_W;
    const float cellH = mRECT.H() / (float)LC_LOGO_H;
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
        const int a = 255 * (int)nib / 15;
        g.FillRect(IColor(a, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                   IRECT(ox + x * scale, oy + y * scale,
                         ox + (x + 1) * scale, oy + (y + 1) * scale));
      }
    }
  }
};

inline void AttachFm6Desk(const std::function<void(IControl*)>& attach,
                          const IRECT& body, const IVStyle& mintStyle)
{
  (void)mintStyle;
  const float w = body.W();

  // Left column: GLOBAL / ALGORITHM / MACROS (~22% width)
  const IRECT globCol = body.GetFromLeft(w * 0.22f).GetPadded(-2.f);
  const IRECT algArea = globCol.GetFromTop(globCol.H() * 0.58f).GetPadded(-2.f);
  attach(new AlgDiagram(algArea));

  const IRECT macroArea = globCol.GetFromBottom(globCol.H() * 0.42f).GetPadded(-2.f);
  const IRECT macroRow0 = macroArea.GetFromTop(macroArea.H() * 0.5f);
  const IRECT macroRow1 = macroArea.GetFromBottom(macroArea.H() * 0.5f);
  attach(new ByteKnob(macroRow0.GetGridCell(0, 1, 2).GetCentredInside(48.f), kAlg, "ALG", 12.f));
  attach(new ByteKnob(macroRow0.GetGridCell(1, 1, 2).GetCentredInside(48.f), kFb, "FB", 12.f));
  
  // Macros: LFO speed, LFO delay, PMD
  attach(new ByteKnob(macroRow1.GetGridCell(0, 1, 3).GetCentredInside(42.f), kLfs, "LFS", 11.f));
  attach(new ByteKnob(macroRow1.GetGridCell(1, 1, 3).GetCentredInside(42.f), kLfd, "LFD", 11.f));
  attach(new ByteKnob(macroRow1.GetGridCell(2, 1, 3).GetCentredInside(42.f), kLpmd, "PMD", 11.f));

  // Right area: 6 Operators (OP 1 to 6)
  const IRECT opsArea = body.GetFromRight(w * 0.78f).GetPadded(-2.f);
  for (int op = 1; op <= 6; ++op)
  {
    const IRECT opCol = opsArea.GetGridCell(op - 1, 1, 6).GetPadded(-2.f);
    attach(new OpChrome(opCol, op));

    const uint32_t base = OpBase(op);
    const IRECT content = opCol.GetReducedFromTop(18.f).GetPadded(-2.f);

    // Row 0: Mode toggle + Output Level
    const IRECT r0 = content.GetFromTop(26.f);
    attach(new ModeToggle(r0.GetFromLeft(r0.W() * 0.55f).GetPadded(-1.f), base + kMode));
    attach(new ByteKnob(r0.GetFromRight(r0.W() * 0.45f).GetCentredInside(40.f), base + kOl, "LVL", 11.f));

    // Row 1: Freq Coarse & Fine & Detune
    const IRECT r1 = content.GetReducedFromTop(28.f).GetFromTop(46.f);
    attach(new ByteKnob(r1.GetGridCell(0, 1, 3).GetCentredInside(40.f), base + kFc, "FREQ", 11.f));
    attach(new ByteKnob(r1.GetGridCell(1, 1, 3).GetCentredInside(40.f), base + kFf, "FINE", 11.f));
    attach(new ByteKnob(r1.GetGridCell(2, 1, 3).GetCentredInside(40.f), base + kDet, "DET", 11.f));

    // EG Graph
    const IRECT rEg = content.GetReducedFromTop(76.f).GetFromTop(content.H() * 0.38f).GetPadded(-2.f);
    attach(new EgGraph(rEg, base + kR1, base + kL1));

    // EG Knobs: Rates & Levels (2 rows of 4 knobs)
    const IRECT rKnobs = content.GetFromBottom(content.H() - 76.f - rEg.H()).GetPadded(-1.f);
    const IRECT rRates = rKnobs.GetFromTop(rKnobs.H() * 0.5f);
    const IRECT rLevels = rKnobs.GetFromBottom(rKnobs.H() * 0.5f);
    static const char* kRNames[4] = {"R1", "R2", "R3", "R4"};
    static const char* kLNames[4] = {"L1", "L2", "L3", "L4"};
    for (int k = 0; k < 4; ++k)
    {
      attach(new ByteKnob(rRates.GetGridCell(k, 1, 4).GetCentredInside(34.f), base + kR1 + k, kRNames[k], 10.f));
      attach(new ByteKnob(rLevels.GetGridCell(k, 1, 4).GetCentredInside(34.f), base + kL1 + k, kLNames[k], 10.f));
    }
  }
}

} // namespace skfm6
