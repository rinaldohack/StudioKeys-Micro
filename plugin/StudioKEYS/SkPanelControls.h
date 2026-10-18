/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "IControls.h"
#include "nanovg.h"
#include "SkMint.h"
#include "sk_host.h"
#include "../../firmware/src/lc_logo.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

using namespace iplug;
using namespace igraphics;

// Panel label bits = the printed buttons, in panel.c B_NAME order (P6b: 18 labels, the 12 matrix
// cells plus the six extra keys). sk_host_buttons() reads bit b as label b of the firmware's NB.
// NOTE REPEAT, CHORD, BT, FADER BANK, < and > are the six extra matrix keys: they go to
// sk_host_smk_extra() with the SK_X_* bits, never through this mask.
enum SkBtnBit : uint32_t
{
  kBtnARP = 0, kBtnREPEAT, kBtnSCL, kBtnCHRD, kBtnGLO, kBtnBT,
  kBtnSAVE, kBtnEDIT, kBtnFX, kBtnSEQ, kBtnPLAY, kBtnREC,
  kBtnHOME, kBtnFADER, kBtnLEFT, kBtnRIGHT, kBtnOCTDN, kBtnOCTUP, kBtnCount
};

enum SkEncRole : uint32_t
{
  kEncSelect = 0, kEncAlgo, kEncPreset, kEncK1, kEncK2, kEncK3, kEncK4
};

// Fader visual order L→R is probe order 2,3,4,1 (VST_IMPLEMENTATION_PLAN).
static constexpr int kFaderHwIndex[4] = {2, 3, 4, 1};

class SkThemedPanel final : public IControl
{
public:
  enum class Style {
    Surface,     // skmint::kSurf (theme surface)
    Background,  // skmint::kBg (theme bg)
    DeepBlack,   // Solid matte black (IColor(255, 14, 14, 16)) - two-tone base
    Well         // skmint::kWell (raised well)
  };

  SkThemedPanel(const IRECT& bounds, Style style = Style::Surface, float round = 0.f)
  : IControl(bounds), mStyle(style), mRound(round)
  {
  }

  void Draw(IGraphics& g) override
  {
    IColor col;
    switch (mStyle)
    {
      case Style::DeepBlack:
        col = IColor(255, 14, 14, 16);
        break;
      case Style::Background:
        col = skmint::kBg;
        break;
      case Style::Well:
        col = skmint::kWell;
        break;
      case Style::Surface:
      default:
        col = skmint::kSurf;
        break;
    }
    if (mRound > 0.f)
      g.FillRoundRect(col, mRECT, mRound);
    else
      g.FillRect(col, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override {}

private:
  Style mStyle;
  float mRound;
};

class SkBrandTitleControl final : public IControl
{
public:
  explicit SkBrandTitleControl(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    const float padY = 4.f;
    const float logoH = std::max(12.f, mRECT.H() - padY * 2.f);
    const float logoW = logoH * ((float)LC_LOGO_W / (float)LC_LOGO_H);
    const IRECT logoSlot(mRECT.L, mRECT.MH() - logoH * 0.5f, mRECT.L + logoW, mRECT.MH() + logoH * 0.5f);

    const float cellW = logoSlot.W() / (float) LC_LOGO_W;
    const float cellH = logoSlot.H() / (float) LC_LOGO_H;
    const float scale = std::min(cellW, cellH);
    const float ox = logoSlot.MW() - 0.5f * LC_LOGO_W * scale;
    const float oy = logoSlot.MH() - 0.5f * LC_LOGO_H * scale;

    for (uint32_t ly = 0; ly < LC_LOGO_H; ++ly)
    {
      for (uint32_t lx = 0; lx < LC_LOGO_W; ++lx)
      {
        const uint32_t k = ly * LC_LOGO_W + lx;
        const uint8_t packed = LC_LOGO[k >> 1];
        const uint8_t nib = (k & 1u) ? (packed & 0x0Fu) : (packed >> 4);
        if (!nib)
          continue;
        const int a = 255 * (int) nib / 15;
        g.FillRect(IColor(a, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                   IRECT(ox + lx * scale, oy + ly * scale,
                         ox + (lx + 1) * scale, oy + (ly + 1) * scale));
      }
    }

    const IRECT textSlot = mRECT.GetReducedFromLeft(logoW + 8.f);
    g.DrawText(IText(17.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Middle),
               "StudioKEYS Micro Companion", textSlot);
  }

  void OnMouseDown(float, float, const IMouseMod&) override {}
};

class TrackTabsControl final : public IControl
{
public:
  using SelectFn = std::function<void(uint32_t tr)>;

  TrackTabsControl(const IRECT& bounds, SelectFn onSelect)
  : IControl(bounds)
  , mOnSelect(std::move(onSelect))
  {
  }

  void Draw(IGraphics& g) override
  {
    const uint32_t selTr = sk_host_selected_track();
    for (uint32_t tr = 0; tr < 4; ++tr)
    {
      const IRECT cell = mRECT.GetGridCell(tr, 1, 4).GetPadded(-2.f);
      const bool active = (tr == selTr);
      const char* engName = sk_host_trk_engine_name(tr);

      g.FillRoundRect(active ? skmint::kTheme : skmint::kWell, cell, 3.f);
      g.DrawRoundRect(active ? skmint::kAccent : skmint::kTheme, cell, 3.f, 0, active ? 1.5f : 1.f);

      char label[32];
      snprintf(label, sizeof label, "T%u: %s", tr + 1, engName && engName[0] ? engName : "—");
      g.DrawText(IText(12.f, active ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 label, cell);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    for (uint32_t tr = 0; tr < 4; ++tr)
    {
      const IRECT cell = mRECT.GetGridCell(tr, 1, 4).GetPadded(-2.f);
      if (cell.Contains(x, y))
      {
        if (mOnSelect)
          mOnSelect(tr);
        SetDirty(false);
        break;
      }
    }
  }

private:
  SelectFn mOnSelect;
};

class SkScreenControl final : public IControl
{
public:
  explicit SkScreenControl(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  ~SkScreenControl() override
  {
    if (mImageId > 0 && mVG)
    {
      nvgDeleteImage(mVG, mImageId);
      mImageId = 0;
      mVG = nullptr;
    }
  }

  void Draw(IGraphics& g) override
  {
    const uint16_t* fb = sk_host_screen();
    g.FillRect(COLOR_BLACK, mRECT);
    if (!fb)
      return;

    NVGcontext* ctx = (NVGcontext*) g.GetDrawContext();
    if (!ctx)
      return;
    mVG = ctx;

    if (mImageId <= 0)
    {
      ConvertFb(fb);
      mImageId = nvgCreateImageRGBA(ctx, (int)SK_HOST_FB_W, (int)SK_HOST_FB_H, NVG_IMAGE_NEAREST, mRgba);
    }
    else if (std::memcmp(mLastFb, fb, sizeof(mLastFb)) != 0)
    {
      ConvertFb(fb);
      nvgUpdateImage(ctx, mImageId, mRgba);
    }

    if (mImageId > 0)
    {
      NVGpaint imgPaint = nvgImagePattern(ctx, mRECT.L, mRECT.T, mRECT.W(), mRECT.H(), 0.0f, mImageId, 1.0f);
      nvgBeginPath(ctx);
      nvgRect(ctx, mRECT.L, mRECT.T, mRECT.W(), mRECT.H());
      nvgFillPaint(ctx, imgPaint);
      nvgFill(ctx);
    }
  }

private:
  void ConvertFb(const uint16_t* fb)
  {
    std::memcpy(mLastFb, fb, sizeof(mLastFb));
    uint32_t* dst = reinterpret_cast<uint32_t*>(mRgba);
    for (int i = 0; i < (int)(SK_HOST_FB_W * SK_HOST_FB_H); ++i)
    {
      const uint16_t raw = fb[i];
      const uint16_t p = (uint16_t)(((raw >> 8) & 0xFFu) | ((raw & 0xFFu) << 8));
      const uint32_t r = (uint32_t)(((p >> 11) & 31) * 255 / 31);
      const uint32_t gv = (uint32_t)(((p >> 5) & 63) * 255 / 63);
      const uint32_t b = (uint32_t)((p & 31) * 255 / 31);
      dst[i] = 0xFF000000u | (b << 16) | (gv << 8) | r;
    }
  }

  NVGcontext* mVG = nullptr;
  int mImageId = 0;
  uint16_t mLastFb[SK_HOST_FB_W * SK_HOST_FB_H] = {0};
  uint8_t mRgba[SK_HOST_FB_W * SK_HOST_FB_H * 4] = {0};
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

  /* P6c: the scribble strip. Line 1 is the printed name; line 2 is what the button does *now*,
   * straight from the firmware (sk_host_button_hints / ui_hints.c), with the hold appended. */
  void SetHints(const char* act, const char* hold)
  {
    std::string a = act ? act : "";
    std::string h = hold ? hold : "";
    if (a == mAct && h == mHold)
      return;
    mAct = a;
    mHold = h;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(mDown ? skmint::kAccent : skmint::kSurf, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f);
    const IColor ink = mDown ? skmint::InkOn(skmint::kAccent) : skmint::kText;
    if (mAct.empty() && mHold.empty())
    {
      Fitted(g, mLabel, mRECT, 14.f, 8.f, ink);
      return;
    }
    std::string fn = mAct;
    if (!mHold.empty())
    {
      if (!fn.empty())
        fn += " ";
      if (mHold != mAct)
        fn += mHold;
    }
    /* Two lines that tile the button exactly (the old 0.40 / 0.64 split overlapped by 4% of the
     * height, which pushed the hint line into the printed name on the short under-screen keys). */
    Fitted(g, mLabel, mRECT.GetFromTop(mRECT.H() * 0.42f), 9.f, 6.f,
           mDown ? ink : skmint::kTheme);
    Fitted(g, fn, mRECT.GetFromBottom(mRECT.H() * 0.58f), 10.f, 6.f, ink);
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
  /* the face shrinks until the string fits the box (the printed names are long: NOTE REPEAT) */
  static void Fitted(IGraphics& g, const std::string& s, const IRECT& box, float maxSz, float minSz,
                     const IColor& color)
  {
    float sz = maxSz;
    IText text(maxSz, color, skmint::kFont);
    const float room = box.W() - 6.f;
    IRECT tb(0.f, 0.f, 0.f, 0.f);
    while (sz > minSz && g.MeasureText(text, s.c_str(), tb) > room)
    {
      sz -= 0.5f;
      text.mSize = sz;
    }
    g.DrawText(text, s.c_str(), box);
  }

  uint32_t mBit = 0;
  std::string mLabel;
  std::string mAct, mHold;
  std::function<void(uint32_t, bool)> mOnBit;
  bool mDown = false;
};

/** P12 — the chrome LINK button. Sized to its four letters (the old 64 px button clipped the
 *  label), filled while the companion <-> CFW link is up, outlined when it is not. */
class SkLinkButton final : public IControl
{
public:
  using IsOnFn = std::function<bool()>;

  SkLinkButton(const IRECT& bounds, IsOnFn isOn, std::function<void()> onClick)
  : IControl(bounds)
  , mIsOn(std::move(isOn))
  , mOnClick(std::move(onClick))
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool up = mIsOn && mIsOn();
    g.FillRoundRect(up ? skmint::kTheme : skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(up ? skmint::kTheme : skmint::kSurf, mRECT, 4.f, 0, 1.25f);
    g.DrawText(IText(14.f, up ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont),
               "LINK", mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (mOnClick)
      mOnClick();
    SetDirty(false);
  }

private:
  IsOnFn mIsOn;
  std::function<void()> mOnClick;
};

/* The hardware pads play a FIXED velocity: the scan (hal/fm1_input.h smk_pads)
 * sends a note-on with a constant velocity and rides the hit with channel
 * pressure (0xD0). We mirror that: every on-screen pad sends the same fixed
 * velocity and streams channel aftertouch while held. */
enum class SkPadEvent { Hit, Pressure, Release };
static constexpr int kPadFixedVelocity = 100;

class SkPadControl final : public IControl
{
public:
  /* onPad(pad0_15, event, value0_127) */
  using PadFn = std::function<void(int, SkPadEvent, int)>;

  SkPadControl(const IRECT& bounds, int padIndex, PadFn onPad)
  : IControl(bounds)
  , mPad(padIndex)
  , mOnPad(std::move(onPad))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(mDown ? skmint::kAccent : skmint::kSurf, mRECT, 5.f);
    g.DrawRoundRect(mDown ? skmint::kAccent : skmint::kTheme, mRECT, 5.f, 0, 1.25f);
    if (mDown)
    {
      /* aftertouch bar along the bottom: the live pressure the pad streams */
      const IRECT bar = mRECT.GetReducedFromBottom(12.f).GetMidVPadded(3.f).GetPadded(-3.f);
      g.FillRect(skmint::kBg, bar);
      const IRECT fill(bar.L, bar.T, bar.L + bar.W() * ((float) mPressure / 127.f), bar.B);
      g.FillRect(skmint::kTheme, fill);
    }
    char lab[8];
    snprintf(lab, sizeof lab, "%d", mPad + 1);
    g.DrawText(IText(14.f, mDown ? skmint::InkOn(skmint::kAccent) : skmint::kText, skmint::kFont), lab, mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    mDown = true;
    mPressure = 0;
    mHoldTicks = 0;
    if (mOnPad)
      mOnPad(mPad, SkPadEvent::Hit, kPadFixedVelocity);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) mod;
    if (!mDown)
      return;
    mDown = false;
    mPressure = 0;
    if (mOnPad)
      mOnPad(mPad, SkPadEvent::Release, 0);
    SetDirty(false);
  }

  /* While held, ramp the pressure to 127 and stream it as channel aftertouch. */
  void OnGUIIdle() override
  {
    if (!mDown)
      return;
    if (++mHoldTicks % 2)
      return; /* ~30 Hz updates at 60 fps */
    if (mPressure >= 127)
      return;
    mPressure = (int) std::min(127, mPressure + 7);
    if (mOnPad)
      mOnPad(mPad, SkPadEvent::Pressure, mPressure);
    SetDirty(false);
  }

private:
  int mPad = 0;
  PadFn mOnPad;
  bool mDown = false;
  int mPressure = 0;
  int mHoldTicks = 0;
};


class SkParamKnob final : public IControl
{
public:
  SkParamKnob(const IRECT& bounds, uint32_t fwId, const char* label, const IVStyle& style, int pluginParamIdx = -1)
  : IControl(bounds)
  , mFwId(fwId)
  , mPluginParam(pluginParamIdx)
  , mLabel(label ? label : "")
  , mStyle(style)
  {
  }

  void SetOnChange(std::function<void()> fn) { mOnChange = std::move(fn); }

  void Draw(IGraphics& g) override
  {
    int32_t val = sk_host_get_param(mFwId);
    int16_t mx = 127;
    int16_t mn = 0;
    struct sk_host_param_info info = {0};
    int hasInfo = sk_host_get_param_info(mFwId, &info);
    if (hasInfo)
    {
      mx = (int16_t)info.max;
      mn = (int16_t)info.min;
    }
    double norm = 0.0;
    if (mx > mn)
      norm = (double)(val - mn) / (double)(mx - mn);
    norm = std::clamp(norm, 0.0, 1.0);

    /* The desk is height-starved at the default editor size: on a short cell the label and
     * value strips shrink so the dial keeps a usable diameter instead of collapsing. */
    const float cellH = mRECT.H();
    const float labH = std::min(20.f, cellH * 0.30f);
    const float valH = std::min(18.f, cellH * 0.24f);
    const IRECT dial = mRECT.GetReducedFromTop(labH).GetReducedFromBottom(valH);
    const float side = std::min(dial.W(), dial.H()) * 0.92f;
    const IRECT ring = dial.GetCentredInside(side);

    g.FillEllipse(skmint::kWell, ring);
    g.DrawEllipse(skmint::kTheme, ring, 0, 1.5f);
    const float cx = ring.MW(), cy = ring.MH(), r = ring.W() * 0.34f;
    const float angle = -135.f + (float)norm * 270.f;
    g.DrawRadialLine(skmint::kAccent, cx, cy, angle, 0.f, r, nullptr, 2.f);

    IText labText = mStyle.labelText;
    IText valText = mStyle.valueText;
    labText.mSize = std::min(labText.mSize, std::max(9.f, labH * 0.92f));
    valText.mSize = std::min(valText.mSize, std::max(9.f, valH * 0.92f));

    const IRECT labRect = mRECT.GetFromTop(labH);
    g.DrawText(labText, mLabel.c_str(), labRect);

    char full[48] = "";
    if (!sk_host_format_param(mFwId, val, full, sizeof full))
    {
      snprintf(full, sizeof full, "%d", (int)val);
    }

    const IRECT valRect = mRECT.GetFromBottom(valH);
    g.DrawText(valText, full, valRect);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mDragY = 0.f;
    if (mPluginParam >= 0)
      if (IEditorDelegate* del = GetDelegate())
        del->BeginInformHostOfParamChangeFromUI(mPluginParam);
  }
  void OnMouseUp(float, float, const IMouseMod&) override
  {
    if (mPluginParam >= 0)
      if (IEditorDelegate* del = GetDelegate())
        del->EndInformHostOfParamChangeFromUI(mPluginParam);
  }
  void OnMouseDrag(float, float, float, float dY, const IMouseMod& mod) override
  {
    struct sk_host_param_info info = {0};
    int span = 127;
    if (sk_host_get_param_info(mFwId, &info) && info.max > info.min)
      span = info.max - info.min;

    const float scale = (span <= 16) ? 7.f : 3.f;
    const float thresh = mod.S ? (scale * 2.5f) : scale;
    mDragY += -dY;
    bool changed = false;
    /* A guard, not an expectation: each pass takes one step off mDragY, so this only fires if a
     * set is refused and a value cannot move (a stale firmware id): never spin the UI thread. */
    int guard = 5000;
    while (mDragY >= thresh && guard-- > 0)
    {
       int32_t val = sk_host_get_param(mFwId);
       sk_host_set_param(mFwId, val + 1);
       mDragY -= thresh;
       changed = true;
    }
    guard = 5000;
    while (mDragY <= -thresh && guard-- > 0)
    {
       int32_t val = sk_host_get_param(mFwId);
       sk_host_set_param(mFwId, val - 1);
       mDragY += thresh;
       changed = true;
    }
    if (changed)
    {
      sk_host_ui_pump();
      if (mPluginParam >= 0)
      {
        if (IEditorDelegate* del = GetDelegate())
        {
          int32_t val = sk_host_get_param(mFwId);
          del->SendParameterValueFromUI(mPluginParam, (double)val / 127.0);
        }
      }
      if (mOnChange)
        mOnChange();
      SetDirty(false);
    }
  }

  void OnMouseDblClick(float, float, const IMouseMod&) override
  {
    struct sk_host_param_info info = {0};
    if (sk_host_get_param_info(mFwId, &info))
    {
      sk_host_set_param(mFwId, info.def);
      sk_host_ui_pump();
      if (mPluginParam >= 0)
      {
        if (IEditorDelegate* del = GetDelegate())
          del->SendParameterValueFromUI(mPluginParam, (double)info.def / 127.0);
      }
      if (mOnChange)
        mOnChange();
      SetDirty(false);
    }
  }

  void OnMouseWheel(float, float, const IMouseMod& mod, float d) override
  {
    (void) mod;
    int32_t val = sk_host_get_param(mFwId);
    sk_host_set_param(mFwId, val + (d > 0 ? 1 : -1));
    sk_host_ui_pump();
    if (mPluginParam >= 0)
    {
      if (IEditorDelegate* del = GetDelegate())
      {
        int32_t nval = sk_host_get_param(mFwId);
        del->SendParameterValueFromUI(mPluginParam, (double)nval / 127.0);
      }
    }
    if (mOnChange)
      mOnChange();
    SetDirty(false);
  }

private:
  uint32_t mFwId;
  int mPluginParam = -1;
  std::string mLabel;
  IVStyle mStyle;
  float mDragY = 0.f;
  std::function<void()> mOnChange;
};

class SkParamMenu final : public IControl
{
public:
  SkParamMenu(const IRECT& bounds, uint32_t fwId, const char* label, const IVStyle& style)
  : IControl(bounds)
  , mFwId(fwId)
  , mLabel(label ? label : "")
  , mStyle(style)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.25f);

    const IRECT labRect = mRECT.GetFromTop(18.f).GetPadded(-2.f);
    g.DrawText(IText(14.f, skmint::kInk, skmint::kFont, EAlign::Near), mLabel.c_str(), labRect);

    int32_t val = sk_host_get_param(mFwId);
    char full[48] = "";
    if (!sk_host_format_param(mFwId, val, full, sizeof full))
    {
      snprintf(full, sizeof full, "%d", (int)val);
    }

    const IRECT valRect = mRECT.GetFromBottom(20.f).GetPadded(-2.f);
    g.DrawText(IText(15.f, skmint::kAccent, skmint::kFont, EAlign::Near), full, valRect);

    // Draw a small dropdown arrow
    const float cx = mRECT.R - 8.f;
    const float cy = valRect.MH();
    g.FillTriangle(skmint::kTheme, cx - 3.f, cy - 2.f, cx + 3.f, cy - 2.f, cx, cy + 3.f);
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    (void) x;
    (void) y;
    struct sk_host_param_info info = {0};
    if (!sk_host_get_param_info(mFwId, &info))
      return;

    mMenu.Clear();
    int16_t mn = (int16_t)info.min;
    int16_t mx = (int16_t)info.max;
    for (int i = mn; i <= mx; ++i)
    {
      if (info.names && info.names[i])
        mMenu.AddItem(info.names[i]);
      else {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", i);
        mMenu.AddItem(buf);
      }
    }
    int32_t val = sk_host_get_param(mFwId);
    mMenu.SetChosenItemIdx(val - mn);

    if (GetUI())
      GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    (void) valIdx;
    if (pSelectedMenu)
    {
      int idx = pSelectedMenu->GetChosenItemIdx();
      struct sk_host_param_info info = {0};
      sk_host_get_param_info(mFwId, &info);
      if (idx >= 0)
      {
        sk_host_set_param(mFwId, idx + info.min);
        SetDirty(false);
      }
    }
  }

  void OnMouseWheel(float, float, const IMouseMod&, float d) override
  {
    struct sk_host_param_info info = {0};
    if (sk_host_get_param_info(mFwId, &info))
    {
      int32_t val = sk_host_get_param(mFwId);
      int32_t next = std::clamp(val + (d > 0.f ? 1 : -1), (int32_t)info.min, (int32_t)info.max);
      sk_host_set_param(mFwId, next);
      SetDirty(false);
    }
  }

private:
  uint32_t mFwId;
  std::string mLabel;
  IVStyle mStyle;
  IPopupMenu mMenu;
};

/* Format a firmware page slot's value for the companion page inspector. Mirrors
 * the firmware's own knob readout closely enough for the desk (names / units). */
inline std::string SkFormatPageSlot(const struct sk_host_page_slot& s, int value)
{
  char buf[48] = "";
  if (s.names && value >= s.min && value <= s.max)
    snprintf(buf, sizeof buf, "%s", s.names[value]);
  else if (s.fmt == 8 /* F_ENUM */ && s.names) /* handled above */
    snprintf(buf, sizeof buf, "%d", value);
  else if (s.fmt == 1 /* F_PCT */)
    snprintf(buf, sizeof buf, s.max == 100 ? "%d%%" : "%d%%", s.max == 100 ? value : (value * 100 + 63) / 127);
  else if (s.fmt == 10 /* F_BPM */)
    snprintf(buf, sizeof buf, "%d BPM", value);
  else
    snprintf(buf, sizeof buf, "%d", value);
  return buf;
}

/* One knob of the firmware's *current page* (kCtrlTagPageKnob0..7). It reads
 * PAGES[ui.page] live so `<` / `>` and GLO / family navigation above always show
 * the same controls the virtual keyboard's OLED is on. Writes go back to the
 * firmware exactly like the desk's SkParamKnob. */
class SkPageKnob final : public IControl
{
public:
  SkPageKnob(const IRECT& bounds, uint32_t slot0_7)
  : IControl(bounds)
  , mSlot(slot0_7)
  {
  }

  bool RefreshFromHost()
  {
    const uint32_t page = sk_host_ui_page();
    struct sk_host_page_slot s {0};
    const bool ok = sk_host_page_slot(page, mSlot, &s) != 0;
    const int val = ok ? s.value : 0;
    const std::string lab = ok ? std::string(s.label) : std::string("—");
    const std::string full = ok ? SkFormatPageSlot(s, val) : std::string("");
    const bool changed = (mHas != ok || mLabel != lab || mValue != full || mId != s.id);
    mHas = ok;
    mLabel = lab;
    mValue = full;
    mId = ok ? s.id : 0;
    mKind = ok ? s.kind : 0;
    return changed;
  }

  void Draw(IGraphics& g) override
  {
    /* Re-read every frame: the firmware page can change under us at any time
     * (`<`/`>` select a track, GLO cycles MIXER→GLOBAL→SYSTEM), and this control
     * is only rebuilt on page-family changes. Reading here keeps the label, value
     * and needle in lock-step with the OLED without a dedicated poll. */
    RefreshFromHost();
    if (!mHas)
    {
      g.FillRoundRect(skmint::kWell, mRECT, 4.f);
      g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.f);
      g.DrawText(IText(13.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle), "—", mRECT);
      return;
    }
    g.FillRoundRect(skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.25f);
    g.DrawText(IText(13.f, skmint::kInk, skmint::kFont, EAlign::Near),
               mLabel.c_str(), mRECT.GetFromTop(18.f).GetPadded(-3.f));
    g.DrawText(IText(15.f, skmint::kAccent, skmint::kFont, EAlign::Center, EVAlign::Middle),
               mValue.c_str(), mRECT.GetFromBottom(26.f).GetPadded(-3.f));
    /* mini needle from the value's normalized position */
    struct sk_host_page_slot s {0};
    if (sk_host_page_slot(sk_host_ui_page(), mSlot, &s) && s.max > s.min)
    {
      const float n = std::clamp((float)(s.value - s.min) / (float)(s.max - s.min), 0.f, 1.f);
      const IRECT ring = mRECT.GetFromBottom(26.f).GetReducedFromRight(22.f).GetCentredInside(16.f);
      g.FillEllipse(skmint::kBg, ring);
      g.DrawEllipse(skmint::kTheme, ring, 0, 1.f);
      g.DrawRadialLine(skmint::kAccent, ring.MW(), ring.MH(),
                       -135.f + n * 270.f, 0.f, ring.W() * 0.34f, nullptr, 1.5f);
    }
  }

  void OnMouseDrag(float, float, float dX, float dY, const IMouseMod& mod) override
  {
    if (!mHas)
      return;
    const float delta = (std::abs(dX) > std::abs(dY) ? dX : -dY) * 0.02f;
    mAcc += delta * 40.f;
    const int steps = (int) mAcc;
    if (!steps)
      return;
    mAcc -= (float) steps;
    struct sk_host_page_slot s {0};
    if (!sk_host_page_slot(sk_host_ui_page(), mSlot, &s))
      return;
    int32_t v = std::clamp(s.value + steps, s.min, s.max);
    if (s.kind == 2)
      sk_host_set_gparam((uint32_t) s.id, v);
    else
      sk_host_set_param((uint32_t) s.id, v);
    sk_host_ui_pump();
    (void) mod;
    RefreshFromHost();
    SetDirty(false);
  }

  void OnMouseDblClick(float, float, const IMouseMod&) override
  {
    struct sk_host_page_slot s {0};
    if (!sk_host_page_slot(sk_host_ui_page(), mSlot, &s))
      return;
    if (s.kind == 2)
      sk_host_set_gparam((uint32_t) s.id, s.def);
    else
      sk_host_set_param((uint32_t) s.id, s.def);
    sk_host_ui_pump();
    RefreshFromHost();
    SetDirty(false);
  }

private:
  uint32_t mSlot = 0;
  bool mHas = false;
  int mId = 0;
  int mKind = 0;
  std::string mLabel;
  std::string mValue;
  float mAcc = 0.f;
};

class SkEncKnob final : public IControl
{
public:
  SkEncKnob(const IRECT& bounds, uint32_t physIdx, const char* fallbackLabel = "")
  : IControl(bounds)
  , mPhys(physIdx)
  , mFallbackLabel(fallbackLabel ? fallbackLabel : "")
  {
    RefreshFromHost();
  }

  bool RefreshFromHost()
  {
    std::string labStr;
    const char* lab = sk_host_knob_label(mPhys);
    if (lab && lab[0] && strcmp(lab, "—") != 0)
      labStr = lab;
    else if (!mFallbackLabel.empty())
      labStr = mFallbackLabel;
    else
    {
      static const char* const kDefaultLabels[8] = {
        "SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4", "MST"
      };
      labStr = (mPhys < 8) ? kDefaultLabels[mPhys] : "—";
    }

    const char* val = sk_host_knob_value(mPhys);
    std::string valStr = (val && val[0]) ? val : "—";
    bool finite = sk_host_knob_finite(mPhys) != 0;
    float n = 0.f;
    bool hasNorm = sk_host_knob_norm(mPhys, &n) != 0;

    bool changed = (mLabel != labStr || mValue != valStr || mFinite != finite || mHasNorm != hasNorm);
    if (hasNorm && std::fabs(mNorm - n) > 0.005f)
    {
      mNorm = n;
      changed = true;
    }
    mLabel = labStr;
    mValue = valStr;
    mFinite = finite;
    mHasNorm = hasNorm;
    return changed;
  }

  void Draw(IGraphics& g) override
  {
    const IRECT scribble = mRECT.GetFromTop(18.f);
    const IRECT valStrip = mRECT.GetFromBottom(16.f);
    const IRECT dial = mRECT.GetReducedFromTop(20.f).GetReducedFromBottom(18.f);

    g.FillRoundRect(skmint::kBg, scribble, 3.f);
    g.DrawRoundRect(skmint::kTheme, scribble, 3.f, 0, 1.f);
    g.DrawText(IText(12.f, skmint::InkOn(skmint::kBg), skmint::kFont, EAlign::Center, EVAlign::Middle),
               mLabel.c_str(), scribble.GetPadded(-1.f));

    const IRECT ring = dial.GetCentredInside(std::min(dial.W(), dial.H()) * 0.92f);
    g.FillEllipse(skmint::kWell, ring);
    g.DrawEllipse(skmint::kTheme, ring, 0, mFinite ? 2.f : 1.5f);

    {
      /* One needle convention for both branches: DrawRadialLine() subtracts 90 deg internally
       * (0 = up), so min (-135 deg) points to 7 o'clock and max (+135 deg) to 5 o'clock. The old
       * finite branch drew cos/sin of the raw angle, which was +90 deg off: min landed at 10 o'clock. */
      const float ang = -135.f + mNorm * 270.f;
      const float cx = ring.MW(), cy = ring.MH(), r = ring.W() * 0.34f;
      g.DrawRadialLine(skmint::kAccent, cx, cy, ang, 0.f, r, nullptr, 2.f);
    }

    g.FillRoundRect(skmint::kWell, valStrip, 2.f);
    g.DrawText(IText(12.f, skmint::kTheme, skmint::kFont, EAlign::Center, EVAlign::Middle),
               mValue.c_str(), valStrip.GetPadded(-1.f));
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) x; (void) y; (void) mod;
    /* Only count detents here. Do NOT set mNorm optimistically: the hardware
     * knob is endless, so its needle is driven by the PARAMETER the host applied,
     * not by the turn. Setting mNorm here made the scribble text (read straight
     * from the host) jump immediately while the needle lagged behind, then
     * snapped when RefreshFromHost() caught up. RefreshFromHost() below is the
     * single source of truth for both. */
    const float delta = (std::abs(dX) > std::abs(dY) ? dX : -dY) * 0.02f;
    mAcc += delta * 40.f;
    /* Endless parameter with no absolute host value (menu/PRESET): keep a local
     * needle so the turn still shows. A finite/paged parameter always follows the
     * host value via RefreshFromHost(), so it never lags. */
    if (!mHasNorm)
      mNorm = std::clamp(mNorm + delta, 0.f, 1.f);
    const int steps = (int) mAcc;
    if (steps != 0)
    {
      mAcc -= (float) steps;
      sk_host_enc_phys(mPhys, steps);
    }
    RefreshFromHost();
    SetDirty(false);
  }

private:
  uint32_t mPhys = 0;
  std::string mFallbackLabel;
  std::string mLabel;
  std::string mValue;
  float mNorm = 0.5f;
  float mAcc = 0.f;
  bool mFinite = false;
  bool mHasNorm = false;
};

class SkFaderControl final : public IControl
{
public:
  SkFaderControl(const IRECT& bounds, int visualIndex, const char* fallbackLabel = "")
  : IControl(bounds)
  , mVisual(visualIndex)
  , mHw(kFaderHwIndex[visualIndex & 3])
  , mFallbackLabel(fallbackLabel ? fallbackLabel : "")
  {
    RefreshFromHost();
  }

  bool RefreshFromHost()
  {
    std::string labStr;
    const char* lab = sk_host_fader_label((uint32_t) (mVisual & 3));
    if (lab && lab[0] && strcmp(lab, "—") != 0)
      labStr = lab;
    else if (!mFallbackLabel.empty())
      labStr = mFallbackLabel;
    else
    {
      char buf[8];
      snprintf(buf, sizeof buf, "F%d", mHw);
      labStr = buf;
    }
    const char* val = sk_host_fader_value((uint32_t) (mVisual & 3));
    std::string valStr = (val && val[0]) ? val : "—";

    bool changed = (mLabel != labStr || mValueStr != valStr);
    mLabel = labStr;
    mValueStr = valStr;
    return changed;
  }

  void Draw(IGraphics& g) override
  {
    const IRECT scribble = mRECT.GetFromTop(18.f);
    const IRECT valStrip = mRECT.GetFromBottom(16.f);
    const IRECT body = mRECT.GetReducedFromTop(20.f).GetReducedFromBottom(18.f);

    g.FillRoundRect(skmint::kBg, scribble, 3.f);
    g.DrawRoundRect(skmint::kTheme, scribble, 3.f, 0, 1.f);
    g.DrawText(IText(12.f, skmint::InkOn(skmint::kBg), skmint::kFont, EAlign::Center, EVAlign::Middle),
               mLabel.c_str(), scribble.GetPadded(-1.f));

    const IRECT rail = body.GetMidHPadded(3.f);
    g.FillRoundRect(skmint::kWell, rail, 2.f);
    g.DrawRoundRect(skmint::kTheme, rail, 2.f, 0, 1.f);
    const float y = body.B - body.H() * (float) mNorm;
    const IRECT cap(body.MW() - 12.f, y - 5.f, body.MW() + 12.f, y + 5.f);
    g.FillRoundRect(skmint::kAccent, cap, 2.f);
    g.DrawRoundRect(skmint::kTheme, cap, 2.f, 0, 1.f);

    g.FillRoundRect(skmint::kWell, valStrip, 2.f);
    g.DrawText(IText(12.f, skmint::kTheme, skmint::kFont, EAlign::Center, EVAlign::Middle),
               mValueStr.c_str(), valStrip.GetPadded(-1.f));
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) x; (void) dX; (void) dY; (void) mod;
    const IRECT body = mRECT.GetReducedFromTop(20.f).GetReducedFromBottom(18.f);
    mNorm = std::clamp((double) ((body.B - y) / body.H()), 0.0, 1.0);
    const int16_t adc = (int16_t) std::lround(mNorm * 1023.0);
    static const uint32_t kAn[5] = {0, 3, 0, 1, 2}; /* hwIndex 1..4 → smk_an */
    if (mHw >= 1 && mHw <= 4)
      sk_host_smk_fader(kAn[mHw], adc);
    /* No master here: on the hardware the four faders are track levels only
     * (main.c pins master_q12 full — there is no master pot). The master lives
     * on the top-bar SkMasterFader instead. */
    RefreshFromHost();
    SetDirty(false);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override { OnMouseDrag(x, y, 0, 0, mod); }

private:
  int mVisual = 0;
  int mHw = 1;
  double mNorm = 0.7;
  std::string mFallbackLabel;
  std::string mLabel;
  std::string mValueStr;
};

/* Top-bar master volume. A horizontal fader that drives the firmware's MASTER
 * (sk_host_master → song.master_q12, the same value the device's master pot
 * would set). It does not touch any track — the four panel faders are track
 * levels. Sits in the chrome bar next to the logo/title, with the VU meter. */
class SkMasterFader final : public IControl
{
public:
  explicit SkMasterFader(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IRECT lab = mRECT.GetFromTop(11.f);
    g.DrawText(IText(11.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle), "MASTER", lab);

    mRail = mRECT.GetReducedFromTop(12.f).GetMidVPadded(5.f);
    g.FillRoundRect(skmint::kWell, mRail, 3.f);
    g.DrawRoundRect(skmint::kTheme, mRail, 3.f, 0, 1.f);

    const float x = mRail.L + mRail.W() * (float) mNorm;
    g.FillRoundRect(skmint::kTheme, IRECT(mRail.L, mRail.MH() - 3.f, x, mRail.MH() + 3.f), 2.f);
    const IRECT cap(x - 3.f, mRail.MH() - 8.f, x + 3.f, mRail.MH() + 8.f);
    g.FillRoundRect(skmint::kAccent, cap, 2.f);
    g.DrawRoundRect(skmint::kTheme, cap, 2.f, 0, 1.f);

    char buf[8];
    snprintf(buf, sizeof buf, "%d%%", (int) std::lround(mNorm * 100.0));
    g.DrawText(IText(11.f, skmint::kTheme, skmint::kFont, EAlign::Far, EVAlign::Middle), buf, lab);
  }

  void OnMouseDown(float x, float, const IMouseMod& mod) override { SetFromX(x); (void) mod; }
  void OnMouseDrag(float x, float, float, float, const IMouseMod& mod) override { SetFromX(x); (void) mod; }

private:
  void SetFromX(float x)
  {
    if (mRail.W() <= 0.f)
      return;
    mNorm = std::clamp((double) ((x - mRail.L) / mRail.W()), 0.0, 1.0);
    sk_host_master((uint32_t) std::lround(mNorm * 1023.0));
    sk_host_ui_pump();
    SetDirty(false);
  }

  double mNorm = 0.684; /* 700/1023: the emulator's default master */
  IRECT mRail;
};
