#pragma once

#include "IControls.h"
#include "SkMint.h"
#include "sk_host.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

using namespace iplug;
using namespace igraphics;

// Panel label bits match web/emu (panel.c B_*): bit b in sk_host_buttons().
enum SkBtnBit : uint32_t
{
  kBtnFX = 0, kBtnSCL, kBtnENV, kBtnLFO, kBtnEDIT, kBtnGLO,
  kBtnHOME, kBtnSAVE, kBtnARP, kBtnSEQ, kBtnPLAY, kBtnREC,
  kBtnOCTDN, kBtnOCTUP, kBtnCount
};

enum SkEncRole : uint32_t
{
  kEncSelect = 0, kEncAlgo, kEncPreset, kEncK1, kEncK2, kEncK3, kEncK4
};

// Fader visual order L→R is probe order 2,3,4,1 (VST_IMPLEMENTATION_PLAN).
static constexpr int kFaderHwIndex[4] = {2, 3, 4, 1};

class SkScreenControl final : public IControl
{
public:
  explicit SkScreenControl(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    const uint16_t* fb = sk_host_screen();
    g.FillRect(COLOR_BLACK, mRECT);
    if (!fb)
      return;

    const float sx = mRECT.W() / (float) SK_HOST_FB_W;
    const float sy = mRECT.H() / (float) SK_HOST_FB_H;
    constexpr int kStep = 2;
    for (int y = 0; y < (int) SK_HOST_FB_H; y += kStep)
    {
      for (int x = 0; x < (int) SK_HOST_FB_W; x += kStep)
      {
        const uint16_t p = fb[y * (int) SK_HOST_FB_W + x];
        const int r = ((p >> 11) & 31) * 255 / 31;
        const int gv = ((p >> 5) & 63) * 255 / 63;
        const int b = (p & 31) * 255 / 31;
        g.FillRect(IColor(255, r, gv, b),
                   IRECT(mRECT.L + x * sx, mRECT.T + y * sy,
                         mRECT.L + (x + kStep) * sx, mRECT.T + (y + kStep) * sy));
      }
    }
  }
};

class SkHoldButton final : public IControl
{
public:
  SkHoldButton(const IRECT& bounds, uint32_t bit, const char* label,
               std::function<void(uint32_t /*bit*/, bool /*down*/)> onBit)
  : IControl(bounds)
  , mBit(bit)
  , mLabel(label ? label : "")
  , mOnBit(std::move(onBit))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(mDown ? skmint::kAccent : skmint::kSurf, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f);
    g.DrawText(IText(11.f, skmint::kText, "Roboto-Regular"), mLabel.c_str(), mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    mDown = true;
    if (mOnBit)
      mOnBit(mBit, true);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    mDown = false;
    if (mOnBit)
      mOnBit(mBit, false);
    SetDirty(false);
  }

private:
  uint32_t mBit = 0;
  std::string mLabel;
  std::function<void(uint32_t, bool)> mOnBit;
  bool mDown = false;
};

class SkPadControl final : public IControl
{
public:
  SkPadControl(const IRECT& bounds, int padIndex, std::function<void(int, bool)> onHit)
  : IControl(bounds)
  , mPad(padIndex)
  , mOnHit(std::move(onHit))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(mDown ? skmint::kAccent : skmint::kSurf, mRECT, 6.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 6.f);
    char lab[8];
    snprintf(lab, sizeof lab, "%d", mPad + 1);
    g.DrawText(IText(12.f, skmint::kText, "Roboto-Regular"), lab, mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    mDown = true;
    if (mOnHit)
      mOnHit(mPad, true);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    mDown = false;
    if (mOnHit)
      mOnHit(mPad, false);
    SetDirty(false);
  }

private:
  int mPad = 0;
  std::function<void(int, bool)> mOnHit;
  bool mDown = false;
};

class SkEncKnob final : public IControl
{
public:
  SkEncKnob(const IRECT& bounds, uint32_t role, const char* label)
  : IControl(bounds)
  , mRole(role)
  , mLabel(label ? label : "")
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillEllipse(skmint::kSurf, mRECT.GetPadded(-6.f));
    g.DrawEllipse(skmint::kTheme, mRECT.GetPadded(-6.f));
    const float ang = (-135.f + mNorm * 270.f) * 0.01745329252f;
    const float cx = mRECT.MW(), cy = mRECT.MH(), r = mRECT.W() * 0.28f;
    g.DrawLine(skmint::kAccent, cx, cy, cx + std::cos(ang - 1.5708f) * r, cy + std::sin(ang - 1.5708f) * r, 0, 2.f);
    g.DrawText(IText(10.f, skmint::kText, "Roboto-Regular"), mLabel.c_str(), mRECT.GetFromBottom(16.f));
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    const float delta = (std::abs(dX) > std::abs(dY) ? dX : -dY) * 0.02f;
    mNorm = std::clamp(mNorm + delta, 0.f, 1.f);
    mAcc += delta * 40.f;
    const int steps = (int) mAcc;
    if (steps != 0)
    {
      mAcc -= (float) steps;
      sk_host_enc(mRole, steps);
    }
    SetDirty(false);
  }

private:
  uint32_t mRole = 0;
  std::string mLabel;
  float mNorm = 0.5f;
  float mAcc = 0.f;
};

class SkFaderControl final : public IControl
{
public:
  SkFaderControl(const IRECT& bounds, int hwIndex /*1..4*/, const char* label)
  : IControl(bounds)
  , mHw(hwIndex)
  , mLabel(label ? label : "")
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT track = mRECT.GetPadded(-8.f).GetMidHPadded(8.f);
    g.FillRect(skmint::kBg, track);
    const float y = track.B - mNorm * track.H();
    g.FillRect(skmint::kTheme, IRECT(track.L, y, track.R, track.B));
    g.FillRect(skmint::kAccent, IRECT(track.L - 4.f, y - 4.f, track.R + 4.f, y + 4.f));
    g.DrawText(IText(10.f, skmint::kText, "Roboto-Regular"), mLabel.c_str(), mRECT.GetFromBottom(14.f));
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) x;
    (void) dX;
    (void) mod;
    const IRECT track = mRECT.GetPadded(-8.f);
    mNorm = std::clamp(1.f - (y - track.T) / track.H(), 0.f, 1.f);
    const int16_t adc = (int16_t) (mNorm * 1023.f);
    /* Probe order L→R is HW 2,3,4,1 → SMK_FADER* indices 0,1,2,3 in ui_input. */
    static const uint32_t kAn[5] = {0, 3, 0, 1, 2}; /* hwIndex 1..4 → smk_an */
    if (mHw >= 1 && mHw <= 4)
      sk_host_smk_fader(kAn[mHw], adc);
    if (mHw == 1)
      sk_host_master((uint32_t) adc);
    SetDirty(false);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override { OnMouseDrag(x, y, 0, 0, mod); }

private:
  int mHw = 1;
  std::string mLabel;
  float mNorm = 0.7f;
};
