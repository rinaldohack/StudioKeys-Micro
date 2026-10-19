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

// The four faders are T1..T4 LEVEL, left to right. What is permuted on the SMK37 is the analog
// mux address each physical fader sits on: FADER 1..4 (left to right) arrive on mux addresses
// 3, 0, 1, 2 (firmware ui_input.c SMK_FADER1..4, and analysis/elite_015/PROBE_MAP.md). The host
// smk_an[] array is indexed by mux address, so the companion must pass the address — and only the
// address. (Previously it first read kFaderHwIndex and then a second table, which permuted twice
// and made the leftmost virtual fader drive T2, the next T3, and so on.)
static constexpr int kFaderMuxAddr[4] = {3, 0, 1, 2};

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
 * velocity and streams channel aftertouch while held.
 *
 * P5: and they are the same pad *buttons* as on the device. The firmware's
 * page-aware map (ui_pads.c) decides what each one does and what colour the
 * WS2812 chain lights; sk_host_pad_rgb / _abbr / _name read that map, so the
 * plug-in's pads show the device's legend and LEDs, and a press goes into the
 * same edge queue (sk_host_pad) as well as out as a note. */
enum class SkPadEvent { Hit, Pressure, Release };
static constexpr int kPadFixedVelocity = 100;

/* P5f: trim a text line to the pad's width — a recall pad's patch name is up to 12 characters and
 * the pad is only ~50 px wide, so the second line ellipsizes instead of running into its neighbour. */
static inline std::string SkPadFit(IGraphics& g, const std::string& s, float size, float width)
{
  const IColor probe(255, 255, 255, 255);
  IRECT b(0.f, 0.f, 0.f, 0.f);
  if (g.MeasureText(IText(size, probe, skmint::kFont), s.c_str(), b) <= width)
    return s;
  std::string out = s;
  while (out.size() > 1)
  {
    out.pop_back();
    const std::string t = out + "..";
    if (g.MeasureText(IText(size, probe, skmint::kFont), t.c_str(), b) <= width)
      return t;
  }
  return out;
}


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
    RefreshFromHost();
  }

  bool RefreshFromHost()
  {
    uint32_t rgb = sk_host_pad_rgb((uint32_t) mPad);
    const char* abbr = sk_host_pad_abbr((uint32_t) mPad);
    const char* name = sk_host_pad_name((uint32_t) mPad);
    const char* sub = sk_host_pad_sub((uint32_t) mPad);
    const std::string a = abbr ? abbr : "";
    const std::string n = name ? name : "";
    const std::string s = sub ? sub : "";
    const int lvl = sk_host_pad_level((uint32_t) mPad);
    bool changed = (rgb != mRgb || a != mAbbr || n != mName || s != mSub || lvl != mLevel);
    mRgb = rgb;
    mAbbr = a;
    mName = n;
    mSub = s;
    mLevel = lvl;
    return changed;
  }

  void Draw(IGraphics& g) override
  {
    /* The LED colour is drawn as the pad's fill (the chain is inside the pad on the device); a
     * dark LED is the embossed pad surface, as before. */
    const int r = (int) ((mRgb >> 16) & 0xFF), gr = (int) ((mRgb >> 8) & 0xFF), b = (int) (mRgb & 0xFF);
    const bool lit = (r + gr + b) > 24;
    IColor led = lit ? IColor(255, r, gr, b) : skmint::kSurf;
    if (mDown)
      led = lit ? led : skmint::kAccent;

    g.FillRoundRect(led, mRECT, 5.f);
    g.DrawRoundRect(mDown ? skmint::kAccent : skmint::kTheme, mRECT, 5.f, 0, 1.25f);

    /* lit pads put their legend on the LED; a dark one keeps the theme's ink */
    int lum = (r * 299 + gr * 587 + b * 114) / 1000;
    const IColor ink = lit && lum > 130 ? IColor(255, 20, 20, 22) : skmint::kText;

    if (!mAbbr.empty())
    {
      /* P5f: a recall pad puts its slot label above and the patch the slot holds under it, so the
       * PERFORM page reads as a live set. Every other pad centres its one-line legend. */
      if (!mSub.empty())
      {
        g.DrawText(IText(12.f, ink, skmint::kFont, EAlign::Center, EVAlign::Middle), mAbbr.c_str(),
                   mRECT.GetMidVPadded(mRECT.H() * 0.10f).GetReducedFromBottom(mRECT.H() * 0.16f));
        g.DrawText(IText(8.5f, ink, skmint::kFont, EAlign::Center, EVAlign::Middle),
                   SkPadFit(g, mSub, 8.5f, mRECT.W() - 6.f).c_str(),
                   mRECT.GetMidVPadded(mRECT.H() * 0.10f).GetReducedFromTop(mRECT.H() * 0.30f));
      }
      else
      {
        g.DrawText(IText(13.f, ink, skmint::kFont, EAlign::Center, EVAlign::Middle), mAbbr.c_str(),
                   mRECT.GetMidVPadded(mRECT.H() * 0.16f));
      }
    }

    /* P5e: a drawbar pad draws its bar down the right edge, so the four pairs read as four
     * drawbars and a drag shows the level climbing. 9 steps, as the OLED's graph_wheel. */
    if (mLevel >= 0)
    {
      const IRECT rail = mRECT.GetFromRight(10.f).GetPadded(-3.f);
      g.FillRoundRect(IColor(120, 0, 0, 0), rail, 2.f);
      const float t = (float) mLevel / 8.f;
      if (t > 0.f)
      {
        const IRECT fill(rail.L, rail.B - rail.H() * t, rail.R, rail.B);
        g.FillRoundRect(IColor(255, 255, 214, 64), fill, 2.f);
      }
      g.DrawRoundRect(IColor(80, 0, 0, 0), rail, 2.f, 0, 1.f);
    }

    char num[8];
    snprintf(num, sizeof num, "%d", mPad + 1);
    g.DrawText(IText(9.f, lit ? ink : skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Top), num,
               mRECT.GetPadded(-4.f).GetFromTop(12.f));

    if (mDown)
    {
      /* aftertouch bar along the bottom: the live pressure the pad streams */
      const IRECT bar = mRECT.GetReducedFromBottom(12.f).GetMidVPadded(3.f).GetPadded(-3.f);
      g.FillRect(skmint::kBg, bar);
      const IRECT fill(bar.L, bar.T, bar.L + bar.W() * ((float) mPressure / 127.f), bar.B);
      g.FillRect(skmint::kTheme, fill);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) x;
    (void) mod;
    mDown = true;
    mPressure = 0;
    mHoldTicks = 0;
    /* P5e: a drawbar pad is a virtual drawbar. The press steps it once (the device's own action)
     * and becomes the base a drag rides from; the drag itself takes over in OnMouseDrag below. */
    mDragY = y;
    mDragSteps = 0;
    mDragging = false;
    sk_host_pad((uint32_t) mPad, 1);        /* the device's pad action (page-aware, ui_pads.c) */
    mDragLevel = sk_host_pad_level((uint32_t) mPad);   /* -1: not a drawbar, the drag is a no-op */
    if (mOnPad)
      mOnPad(mPad, SkPadEvent::Hit, kPadFixedVelocity);
    SetDirty(false);
  }

  /* P5e: press and drag a drawbar pad to ride its bar — one step per 6 px, up = louder, exactly
   * the "grab the bar and pull it" a Nord's LED drawbar gives. A plain click stays a single step,
   * and on the device (no mouse) a held pad ramps in ui_pads.c's smk_pads_run(). */
  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) x;
    (void) dX;
    (void) mod;
    if (!mDown || mDragLevel < 0)
      return;
    if (!mDragging)
    {
      if (std::fabs(y - mDragY) < 3.f)
        return;
      mDragging = true;
      sk_host_pad((uint32_t) mPad, 0);      /* let go of the hold: the drag owns the bar now */
    }
    const int steps = (int) std::lround((mDragY - y) / 6.f);
    if (steps == mDragSteps)
      return;
    mDragSteps = steps;
    sk_host_pad_level_set((uint32_t) mPad, mDragLevel + steps);
    if (mOnPad)
      mOnPad(mPad, SkPadEvent::Pressure, 0);
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
    if (!mDragging)                         /* the drag already released the device's hold */
      sk_host_pad((uint32_t) mPad, 0);
    mDragging = false;
    mDragLevel = -1;
    if (mOnPad)
      mOnPad(mPad, SkPadEvent::Release, 0);
    SetDirty(false);
  }

  /* P5f: driven from StudioKEYS::OnIdle() (see RefreshPanelScribbles). iPlug2 only dispatches
   * IControl::OnGUIIdle under USE_IDLE_CALLS — which this build never defines — so the pads used to
   * keep the colour/legend they had when the editor opened: a chorus or Leslie toggle, an engine
   * change, even a page change never reached them. */
  void Tick()
  {
    if (RefreshFromHost())
      SetDirty(false); /* the page / engine / FX state changed the LED or legend */
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

  void OnGUIIdle() override { Tick(); }

private:
  int mPad = 0;
  PadFn mOnPad;
  uint32_t mRgb = 0;
  std::string mAbbr;
  std::string mName;
  std::string mSub;        /* P5f: a recall pad's patch name ("" otherwise) */
  bool mDown = false;
  int mPressure = 0;
  int mHoldTicks = 0;
  int mLevel = -1;         /* P5e: the pad's drawbar level 0..8, -1 = not a drawbar */
  float mDragY = 0.f;      /* the press's y, the drag's reference */
  int mDragSteps = 0;      /* steps applied by the drag so far */
  int mDragLevel = -1;     /* the level the drag rides from (after the press's single step) */
  bool mDragging = false;
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
    mValues.clear();
    /* One row per *name*, not per raw value: an F_INT list can be far shorter than its range
     * (LOFI WAV# has 16 names over 0..127), and reading info.names[i] for i up to 127 walked
     * off the array into other globals. sk_host_param_name_value maps row -> value the same way
     * the firmware's formatter does, so the row that says SQ50 selects SQ50. */
    const int k = sk_host_param_name_count(mFwId, 0);
    if (k > 0)
    {
      for (int i = 0; i < k; ++i)
      {
        const int v = sk_host_param_name_value(mFwId, 0, i);
        char buf[48] = "";
        if (v < 0 || !sk_host_format_param(mFwId, v, buf, sizeof buf))
          continue;
        mValues.push_back(v);
        mMenu.AddItem(buf);
      }
    }
    else
    {
      for (int i = info.min; i <= info.max; ++i)
      {
        char buf[48] = "";
        if (!sk_host_format_param(mFwId, i, buf, sizeof buf))
          snprintf(buf, sizeof buf, "%d", i);
        mValues.push_back(i);
        mMenu.AddItem(buf);
      }
    }
    int32_t val = sk_host_get_param(mFwId);
    int row = 0;
    for (size_t i = 0; i < mValues.size(); ++i)
      if (mValues[i] <= val)
        row = (int) i;
    mMenu.SetChosenItemIdx(row);

    if (GetUI())
      GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    (void) valIdx;
    if (pSelectedMenu)
    {
      const int idx = pSelectedMenu->GetChosenItemIdx();
      if (idx >= 0 && idx < (int) mValues.size())
      {
        sk_host_set_param(mFwId, mValues[(size_t) idx]);
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
  std::vector<int> mValues;   /* row -> the value that row selects */
};

/* One page slot's value text. This used to re-implement the firmware's rules here and indexed
 * `names[value]` for any value the descriptor's min..max allowed. The engine desc hooks return
 * F_INT descriptors whose names list is much shorter than its range (LOFI WAV#: 16 names over
 * 0..127, DCY: 32 over 0..127): names[16] is the list's 0, names[17..] is past it, and
 * snprintf("%s", NULL/garbage) segfaults inside strlen. Live 12 crashed there on 2026-10-19.
 * Ask the firmware instead: it counts the list and splits the range (params.c param_format), so
 * the companion also prints exactly what the OLED shows. */
inline std::string SkFormatPageSlot(const struct sk_host_page_slot& s, int value)
{
  char buf[48] = "";
  const bool ok = s.kind == 2 ? sk_host_format_gparam((uint32_t) s.id, value, buf, sizeof buf) != 0
                              : sk_host_format_param((uint32_t) s.id, value, buf, sizeof buf) != 0;
  if (!ok)
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

    /* Stacked, never overlapping: label on top, value under it, a thin level bar along the
     * bottom edge. The old cell put a 16 px needle circle in the bottom-right corner of a
     * ~27 px cell, so it drew through the label and the value (and the value ran off the
     * edge). The grid is 4 x 2, so there is no room for a radial knob: 11 + 14 + 2 px. */
    const IRECT in = mRECT.GetPadded(-4.f);
    g.DrawText(IText(11.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Top),
               mLabel.c_str(), in.GetFromTop(11.f));

    /* the value, one size down only when it would not fit (dB, ms, 1/16, 120 BPM).
     * Measured once per (string, width): MeasureText lays out the whole string, and this draws
     * 8 cells every frame. */
    const IRECT valBox(in.L, in.T + 11.f, in.R, in.B - 2.f);
    if (mFittedFor != mValue || mFittedW != valBox.W())
    {
      mFittedSize = 14.f;
      mFittedFor = mValue;
      mFittedW = valBox.W();
      IText mt(mFittedSize, skmint::kAccent, skmint::kFont, EAlign::Near, EVAlign::Middle);
      IRECT tb(0.f, 0.f, 0.f, 0.f);
      while (mFittedSize > 9.f && g.MeasureText(mt, mValue.c_str(), tb) > valBox.W())
      {
        mFittedSize -= 0.5f;
        mt.mSize = mFittedSize;
      }
    }
    g.DrawText(IText(mFittedSize, skmint::kAccent, skmint::kFont, EAlign::Near, EVAlign::Middle),
               mValue.c_str(), valBox);

    /* level bar: the normalized position the needle used to show */
    struct sk_host_page_slot s {0};
    if (sk_host_page_slot(sk_host_ui_page(), mSlot, &s) && s.max > s.min)
    {
      const float n = std::clamp((float) (s.value - s.min) / (float) (s.max - s.min), 0.f, 1.f);
      const IRECT bar(in.L, in.B - 2.f, in.R, in.B);
      g.FillRoundRect(skmint::kBg, bar, 1.f);
      g.FillRoundRect(skmint::kAccent, IRECT(bar.L, bar.T, bar.L + bar.W() * n, bar.B), 1.f);
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
  /* the fitted value size: recomputed when the text or the cell width changes */
  std::string mFittedFor;
  float mFittedW = -1.f;
  float mFittedSize = 14.f;
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
  , mHw(kFaderMuxAddr[visualIndex & 3])
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
      snprintf(buf, sizeof buf, "F%d", (mVisual & 3) + 1);   /* the printed panel number */
      labStr = buf;
    }
    const char* val = sk_host_fader_value((uint32_t) (mVisual & 3));
    std::string valStr = (val && val[0]) ? val : "—";

    bool changed = (mLabel != labStr || mValueStr != valStr);
    mLabel = labStr;
    mValueStr = valStr;
    /* The cap follows P_LEVEL like the value text does: a preset load, a pad, the link or the
     * firmware's own fader scan moved the level and the cap used to stay where the mouse left
     * it. Not while THIS fader is being dragged: sk_host_smk_fader() reaches P_LEVEL through
     * the firmware's UI pass, so an immediate read would snap the cap back for a block. */
    float n = 0.f;
    if (!mDragging && sk_host_fader_norm((uint32_t) (mVisual & 3), &n) &&
        std::fabs(mNorm - (double) n) > 0.002)
    {
      mNorm = (double) n;
      changed = true;
    }
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
    if (!mDragging)
      mDragging = true;   /* RefreshFromHost() reads the cap from P_LEVEL, which the firmware
                           * applies a block later: don't fight the mouse while it is held */
    const IRECT body = mRECT.GetReducedFromTop(20.f).GetReducedFromBottom(18.f);
    mNorm = std::clamp((double) ((body.B - y) / body.H()), 0.0, 1.0);
    const int16_t adc = (int16_t) std::lround(mNorm * 1023.0);
    /* mHw is already the mux address smk_an[] is indexed by — no second permutation. */
    sk_host_smk_fader((uint32_t) mHw, adc);
    /* No master here: on the hardware the four faders are track levels only
     * (main.c pins master_q12 full — there is no master pot). The master lives
     * on the top-bar SkMasterFader instead. */
    RefreshFromHost();
    SetDirty(false);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override { OnMouseDrag(x, y, 0, 0, mod); }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    mDragging = false;
    if (RefreshFromHost())   /* the firmware's quantised level, now that the mouse let go */
      SetDirty(false);
  }

private:
  int mVisual = 0;
  int mHw = 1;
  double mNorm = 0.7;
  bool mDragging = false;
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

  /* The pot's ADC is the firmware's master (web_master_adc -> song.master_q12): follow it when
   * something else moved it (a linked panel, a preset) instead of keeping the mouse's last x. */
  bool RefreshFromHost()
  {
    const double n = (double) sk_host_master_get() / 1023.0;
    if (std::fabs(mNorm - n) < 0.002)
      return false;
    mNorm = std::clamp(n, 0.0, 1.0);
    return true;
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
