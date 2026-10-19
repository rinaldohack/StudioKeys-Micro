/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "IControls.h"
#include "EngineDeskUI.h"
#include "SkMint.h"
#include "SkPanelControls.h"
#include "sk_host.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace skwheel {

using namespace iplug;
using namespace igraphics;

/** Drawbar footages and classic Hammond colors */
struct DrawbarInfo
{
  const char* footage;
  IColor color; // authentic Brown, White, Black
};

static const DrawbarInfo kDrawbars[9] = {
  {"16'",   IColor(255, 150, 75, 20)},   // Brown (sub)
  {"5 1/3'", IColor(255, 150, 75, 20)},   // Brown (sub 5th)
  {"8'",    IColor(255, 235, 235, 235)}, // White (unison)
  {"4'",    IColor(255, 235, 235, 235)}, // White (octave)
  {"2 2/3'", IColor(255, 45, 45, 48)},    // Black (nazard)
  {"2'",    IColor(255, 235, 235, 235)}, // White (blockflote)
  {"1 3/5'", IColor(255, 45, 45, 48)},    // Black (tierce)
  {"1 1/3'", IColor(255, 45, 45, 48)},    // Black (larigot)
  {"1'",    IColor(255, 235, 235, 235)}  // White (sifflote)
};

static const char* const kRegNames[16] = {
  "FLUTE", "MELLO", "HOLLW", "SMOOT", "3BAR", "BLUES", "GOSPL", "ROCK",
  "TOPS", "CLARI", "REED", "STRNG", "CHAPL", "BRITE", "BASS", "FULL"
};

static const char kRegPresets[16][10] = {
  "008000000", "008400000", "800800000", "838000000",
  "888000000", "888800000", "888600006", "888000888",
  "800000888", "008080800", "008765432", "006876540",
  "004432000", "008888000", "858000000", "888888888"
};

/** Interactive 9-Drawbar console matching Hammond B3 mechanics.
 *
 * P5: the nine levels are the engine's own params P_E0..P_E8 (no more REG + SUB/BODY/TOP
 * indirection), so each bar is independent — and the same values the HOME knobs, the pad
 * pairs and the firmware's page read. */
class DrawbarsConsole final : public IControl
{
public:
  DrawbarsConsole(const IRECT& bounds, uint32_t pe0)
  : IControl(bounds)
  , mPe0(pe0)
  {
  }

  int GetBarLevel(int k) const
  {
    return std::clamp((int) sk_host_get_param(mPe0 + (uint32_t) k), 0, 8);
  }

  void SetBarLevel(int k, int newLevel)
  {
    sk_host_set_param(mPe0 + (uint32_t) k, std::clamp(newLevel, 0, 8));
    sk_host_ui_pump();
    SetDirty(false);
  }

  /** The registration the nine bars currently spell, or -1 when they match none. */
  int CurrentReg() const
  {
    for (int r = 0; r < 16; ++r)
    {
      bool same = true;
      for (int k = 0; k < 9 && same; ++k)
        same = (GetBarLevel(k) == kRegPresets[r][k] - '0');
      if (same)
        return r;
    }
    return -1;
  }

  void ApplyReg(int r)
  {
    r = std::clamp(r, 0, 15);
    for (int k = 0; k < 9; ++k)
      sk_host_set_param(mPe0 + (uint32_t) k, kRegPresets[r][k] - '0');
    sk_host_ui_pump();
    SetDirty(false);
  }

  void StepReg(int dir)
  {
    int r = CurrentReg();
    if (r < 0)
      r = dir > 0 ? 0 : 15;
    else
      r = (r + dir + 16) % 16;
    ApplyReg(r);
  }

  void Draw(IGraphics& g) override
  {
    // Metal chassis backdrop
    g.FillRoundRect(IColor(255, 18, 18, 20), mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.25f);

    const float colW = mRECT.W() / 9.f;
    const float travelT = mRECT.T + 26.f;
    const float travelB = mRECT.B - 28.f;
    const float travelH = travelB - travelT;

    for (int k = 0; k < 9; ++k)
    {
      const float colL = mRECT.L + k * colW;
      const float colR = colL + colW;
      const IRECT colRct(colL, mRECT.T, colR, mRECT.B);

      // Footage label on top
      const IRECT footRct(colL, mRECT.T + 4.f, colR, travelT);
      g.DrawText(IText(12.f, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 kDrawbars[k].footage, footRct);

      // Vertical guide track slot
      const float slotW = 5.f;
      const IRECT slot(colRct.MW() - slotW * 0.5f, travelT, colRct.MW() + slotW * 0.5f, travelB);
      g.FillRoundRect(IColor(255, 10, 10, 12), slot, 2.f);

      // Tick marks 0..8
      for (int step = 0; step <= 8; ++step)
      {
        const float ty = travelT + travelH * (float)step / 8.f;
        g.DrawLine(IColor(40, 255, 255, 255), colRct.MW() - 8.f, ty, colRct.MW() + 8.f, ty, 0, 1.f);
      }

      // Drawbar pull position: level 0 = at travelT, level 8 = at travelB
      const int level = GetBarLevel(k);
      const float barY = travelT + travelH * (float)level / 8.f;

      // Drawbar metal rod extending from top to handle
      const IRECT rod(colRct.MW() - 3.f, travelT, colRct.MW() + 3.f, barY);
      g.FillRect(IColor(255, 160, 160, 165), rod);

      // Drawbar authentic handle head
      const float headW = colW - 6.f;
      const float headH = 20.f;
      const IRECT head(colRct.MW() - headW * 0.5f, barY - headH * 0.5f,
                       colRct.MW() + headW * 0.5f, barY + headH * 0.5f);

      g.FillRoundRect(kDrawbars[k].color, head, 3.f);
      g.DrawRoundRect(IColor(200, 0, 0, 0), head, 3.f, 0, 1.5f);

      // Step number printed on handle head
      char numStr[4];
      snprintf(numStr, sizeof numStr, "%d", level);
      const IColor numCol = (k == 2 || k == 3 || k == 5 || k == 8)
                              ? IColor(255, 20, 20, 20)
                              : IColor(255, 240, 240, 240);
      g.DrawText(IText(12.f, numCol, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 numStr, head);

      // Value number at bottom
      const IRECT numRct(colL, travelB + 4.f, colR, mRECT.B - 4.f);
      g.DrawText(IText(13.f, level > 0 ? skmint::kAccent : skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 numStr, numRct);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    mActiveBar = HitBar(x);
    if (mActiveBar >= 0)
    {
      UpdateLevelFromY(mActiveBar, y);
    }
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod&) override
  {
    (void) x; (void) dX; (void) dY;
    if (mActiveBar >= 0)
    {
      UpdateLevelFromY(mActiveBar, y);
    }
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    mActiveBar = -1;
  }

  void OnMouseWheel(float x, float y, const IMouseMod&, float d) override
  {
    (void) y;
    int k = HitBar(x);
    if (k >= 0)
    {
      int cur = GetBarLevel(k);
      SetBarLevel(k, cur + (d > 0.f ? 1 : -1));
    }
  }

private:
  int HitBar(float x) const
  {
    const float colW = mRECT.W() / 9.f;
    int k = (int)((x - mRECT.L) / colW);
    return std::clamp(k, 0, 8);
  }

  void UpdateLevelFromY(int k, float y)
  {
    const float travelT = mRECT.T + 26.f;
    const float travelB = mRECT.B - 28.f;
    const float travelH = travelB - travelT;
    if (travelH <= 1.f) return;

    float t = std::clamp((y - travelT) / travelH, 0.f, 1.f);
    int level = (int)std::lround(t * 8.f);
    SetBarLevel(k, level);
  }

  uint32_t mPe0;
  int mActiveBar = -1;
};

/** Animated dual-rotor Leslie rotary speaker unit */
class LeslieRotaryControl final : public IControl
{
public:
  explicit LeslieRotaryControl(const IRECT& bounds, uint32_t peRotr)
  : IControl(bounds)
  , mPeRotr(peRotr)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kBg, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.25f);

    const int mode = std::clamp((int)sk_host_get_param(mPeRotr), 0, 2);

    // Title & status
    const IRECT hdr = mRECT.GetFromTop(20.f).GetPadded(-4.f);
    g.DrawText(IText(13.f, skmint::kAccent, skmint::kFont, EAlign::Near, EVAlign::Top),
               "LESLIE ROTARY", hdr);

    // Switch row: OFF / SLOW / FAST (a third of the panel each: easy to hit)
    const IRECT btnArea = mRECT.GetFromBottom(34.f).GetPadded(-4.f);
    static const char* const kModes[3] = {"OFF", "SLOW", "FAST"};
    for (int i = 0; i < 3; ++i)
    {
      const IRECT b = btnArea.GetGridCell(i, 1, 3).GetPadded(-2.f);
      const bool sel = (i == mode);
      g.FillRoundRect(sel ? skmint::kTheme : skmint::kWell, b, 3.f);
      g.DrawRoundRect(skmint::kTheme, b, 3.f, 0, 1.f);
      g.DrawText(IText(12.f, sel ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 kModes[i], b);
    }

    // Visual Dual Rotor Cabinet (Horn + Drum)
    const IRECT cab = mRECT.GetReducedFromTop(24.f).GetReducedFromBottom(38.f).GetPadded(-6.f);
    g.FillRoundRect(IColor(255, 18, 18, 20), cab, 4.f);

    // Top: High Horn rotor
    const IRECT hornBox = cab.GetFromTop(cab.H() * 0.48f).GetPadded(-2.f);
    const float hcx = hornBox.MW(), hcy = hornBox.MH(), hr = std::min(hornBox.W(), hornBox.H()) * 0.40f;
    g.DrawEllipse(IColor(60, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), hcx, hcy, hr, hr * 0.5f, 0, nullptr, 1.f);

    // Bottom: Low Drum rotor
    const IRECT drumBox = cab.GetFromBottom(cab.H() * 0.48f).GetPadded(-2.f);
    const float dcx = drumBox.MW(), dcy = drumBox.MH(), dr = std::min(drumBox.W(), drumBox.H()) * 0.40f;
    g.DrawEllipse(IColor(60, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B), dcx, dcy, dr, dr * 0.5f, 0, nullptr, 1.f);

    // Speed status text
    const char* status = mode == 0 ? "Stopped" : (mode == 1 ? "Chorale (Slow)" : "Tremolo (Fast)");
    const IRECT statusRct(cab.L, cab.MH() - 8.f, cab.R, cab.MH() + 8.f);
    g.DrawText(IText(11.f, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle), status, statusRct);
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    const IRECT btnArea = mRECT.GetFromBottom(34.f).GetPadded(-4.f);
    if (btnArea.GetPadded(6.f).Contains(x, y))
    {
      for (int i = 0; i < 3; ++i)
      {
        if (btnArea.GetGridCell(i, 1, 3).Contains(x, y))
        {
          sk_host_set_param(mPeRotr, i);
          sk_host_ui_pump();
          SetDirty(false);
          break;
        }
      }
    }
    else
    {
      int mode = (int)sk_host_get_param(mPeRotr);
      sk_host_set_param(mPeRotr, (mode + 1) % 3);
      sk_host_ui_pump();
      SetDirty(false);
    }
  }

private:
  uint32_t mPeRotr;
};

/** Hammond Percussion Unit with discrete rocker controls.
 *
 * P5: the rockers were three-way-tiny: each cell was a 2-row grid split in half, so on the
 * companion's narrow middle column the buttons ended up a few pixels tall. They are now one
 * big ON/OFF pair per cell that fills the cell (label above, buttons below), and the whole
 * cell is clickable — left half = option A, right half = option B. */
class PercussionUnit final : public IControl
{
public:
  explicit PercussionUnit(const IRECT& bounds, uint32_t pePerc)
  : IControl(bounds)
  , mPePerc(pePerc)
  {
  }

  /** the switch rects, in the same place Draw and the mouse use */
  void CellRects(int idx, IRECT& cell, IRECT& btnA, IRECT& btnB) const
  {
    const IRECT body = mRECT.GetReducedFromTop(std::min(22.f, mRECT.H() * 0.20f)).GetPadded(-4.f);
    cell = body.GetGridCell(idx, 2, 2).GetPadded(-4.f);
    /* The label sits on the cell's top line; the two buttons take the rest, always inside the
     * cell (a fixed minimum used to overflow it on a short window, and the part outside the cell
     * was neither drawn nor clickable because OnMouseDown gates on cell.Contains). */
    const IRECT sw = cell.GetReducedFromTop(std::min(16.f, cell.H() * 0.34f));
    btnA = sw.GetFromLeft(sw.W() * 0.5f).GetPadded(-2.f);
    btnB = sw.GetFromRight(sw.W() * 0.5f).GetPadded(-2.f);
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kBg, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.25f);

    // Title
    const IRECT hdr = mRECT.GetFromTop(20.f).GetPadded(-4.f);
    g.DrawText(IText(13.f, skmint::kAccent, skmint::kFont, EAlign::Near, EVAlign::Top),
               "PERCUSSION", hdr);

    const int v = std::clamp((int)sk_host_get_param(mPePerc), 0, 6);
    // {"OFF", "2ND", "3RD", "2SOFT", "3SOFT", "2SLOW", "3SLOW"}
    const bool on = (v != 0);
    const bool is3rd = (v == 2 || v == 4 || v == 6);
    const bool isSoft = (v == 3 || v == 4);
    const bool isSlow = (v == 5 || v == 6);

    // 4 rockers: [PERC ON/OFF], [HARMONIC 2nd/3rd], [VOLUME NORM/SOFT], [DECAY FAST/SLOW]
    auto drawSwitch = [&](int idx, const char* title, const char* optA, const char* optB, bool selB) {
      IRECT cell, btnA, btnB;
      CellRects(idx, cell, btnA, btnB);
      g.DrawText(IText(11.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Top), title,
                 cell.GetFromTop(std::min(16.f, cell.H() * 0.34f)));

      g.FillRoundRect(!selB ? skmint::kTheme : skmint::kWell, btnA, 3.f);
      g.DrawRoundRect(skmint::kTheme, btnA, 3.f, 0, 1.f);
      g.DrawText(IText(12.f, !selB ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont,
                       EAlign::Center, EVAlign::Middle),
                 optA, btnA);

      g.FillRoundRect(selB ? skmint::kTheme : skmint::kWell, btnB, 3.f);
      g.DrawRoundRect(skmint::kTheme, btnB, 3.f, 0, 1.f);
      g.DrawText(IText(12.f, selB ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont,
                       EAlign::Center, EVAlign::Middle),
                 optB, btnB);
    };

    drawSwitch(0, "STATUS", "OFF", "ON", on);
    drawSwitch(1, "HARMONIC", "2ND", "3RD", is3rd);
    drawSwitch(2, "VOLUME", "NORM", "SOFT", isSoft);
    drawSwitch(3, "DECAY", "FAST", "SLOW", isSlow);
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    const int cur = std::clamp((int)sk_host_get_param(mPePerc), 0, 6);
    bool on = (cur != 0);
    bool is3rd = (cur == 2 || cur == 4 || cur == 6);
    bool isSoft = (cur == 3 || cur == 4);
    bool isSlow = (cur == 5 || cur == 6);

    for (int idx = 0; idx < 4; ++idx)
    {
      IRECT cell, btnA, btnB;
      CellRects(idx, cell, btnA, btnB);
      if (!cell.Contains(x, y))
        continue;

      const bool clickB = (x >= cell.MW());
      /* P5g: the three sub-switches enable the unit when it is off. Before this, clicking
       * HARMONIC / VOLUME / DECAY while STATUS read OFF wrote 0 back (the "off" enum), so the
       * buttons looked dead — exactly the three the user reported in the screenshot. They now
       * behave like the WHEEL's PERC pads: pick the option and the unit comes on with it. */
      if (idx == 0) // ON/OFF
        on = clickB;
      else if (idx == 1) // 2ND/3RD
      {
        is3rd = clickB;
        on = true;
      }
      else if (idx == 2) // NORM/SOFT
      {
        isSoft = clickB;
        if (isSoft) isSlow = false;
        on = true;
      }
      else if (idx == 3) // FAST/SLOW
      {
        isSlow = clickB;
        if (isSlow) isSoft = false;
        on = true;
      }

      int next = 0;
      if (on)
      {
        if (isSlow)
          next = is3rd ? 6 : 5;
        else if (isSoft)
          next = is3rd ? 4 : 3;
        else
          next = is3rd ? 2 : 1;
      }
      sk_host_set_param(mPePerc, next);
      sk_host_ui_pump();
      SetDirty(false);
      break;
    }
  }

private:
  uint32_t mPePerc;
};

/** The registration picker. REG is no longer a stored param (the nine bars are): a click writes
 *  all nine at once, and the display follows the bars — the matched registration, or CUSTOM. */
class RegPicker final : public IControl
{
public:
  RegPicker(const IRECT& bounds, DrawbarsConsole* console)
  : IControl(bounds), mConsole(console)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kWell, mRECT, 3.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 3.f, 0, 1.f);

    const int r = mConsole ? mConsole->CurrentReg() : -1;
    char txt[32];
    snprintf(txt, sizeof txt, "REG  %s", r >= 0 ? kRegNames[r] : "CUSTOM");

    // ‹ / › affordances: the whole left and right thirds change the registration
    const float a = mRECT.W() * 0.12f;
    g.DrawText(IText(14.f, skmint::kTheme, skmint::kFont, EAlign::Center, EVAlign::Middle), "‹",
               IRECT(mRECT.L, mRECT.T, mRECT.L + a * 2.f, mRECT.B));
    g.DrawText(IText(14.f, skmint::kTheme, skmint::kFont, EAlign::Center, EVAlign::Middle), "›",
               IRECT(mRECT.R - a * 2.f, mRECT.T, mRECT.R, mRECT.B));
    g.DrawText(IText(12.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle), txt,
               mRECT.GetPadded(-a * 2.f));
  }

  void OnMouseDown(float x, float, const IMouseMod&) override
  {
    if (!mConsole)
      return;
    mConsole->StepReg(x < mRECT.MW() ? -1 : 1);
    SetDirty(false);
  }

private:
  DrawbarsConsole* mConsole = nullptr;
};

inline void AttachWheelDesk(const std::function<void(IControl*)>& attach,
                            const IRECT& body, const IVStyle& mintStyle,
                            int pAtk = 7, int pDec = 8, int pSus = 9, int pRel = 10)
{
  const uint32_t pe0 = sk_host_pe0_id();
  const float w = body.W();

  // Column 1 (Left 46%): 9 Hammond Drawbars + Registration selector
  const IRECT drawbarCol = body.GetFromLeft(w * 0.46f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(drawbarCol, "TONEWHEEL DRAWBARS (16' — 1')"));
  {
    const IRECT inner = drawbarCol.GetReducedFromTop(26.f).GetPadded(-4.f);
    // Top 30px: the registration shortcut (writes the nine bars)
    const IRECT regRct = inner.GetFromTop(30.f);
    auto* bars = new DrawbarsConsole(inner.GetReducedFromTop(34.f).GetPadded(-2.f), pe0);
    attach(new RegPicker(regRct.GetCentredInside(180.f, 28.f), bars));
    attach(bars);
  }

  // Column 2 (Middle 30%): Leslie Rotary & Percussion Unit
  const IRECT fxCol = body.GetReducedFromLeft(w * 0.46f).GetFromLeft(w * 0.30f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(fxCol, "ROTOR & PERCUSSION"));
  {
    const IRECT inner = fxCol.GetReducedFromTop(26.f).GetPadded(-4.f);
    // Top 42%: Leslie Rotary
    const IRECT rotRct = inner.GetFromTop(inner.H() * 0.42f).GetPadded(-2.f);
    attach(new LeslieRotaryControl(rotRct, pe0 + 11));   // P_E11 ROTR

    // The rest: the Percussion Unit (four big rockers)
    const IRECT percRct = inner.GetFromBottom(inner.H() * 0.55f).GetPadded(-2.f);
    attach(new PercussionUnit(percRct, pe0 + 9));        // P_E9 PERC
  }

  // Column 3 (Right 24%): Tube Overdrive, Key Click & Organ Envelope
  const IRECT toneCol = body.GetFromRight(w * 0.24f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(toneCol, "OVERDRIVE & ENVELOPE"));
  {
    const IRECT inner = toneCol.GetReducedFromTop(26.f).GetPadded(-4.f);
    // Top row: Click & Drive knobs (DRIVE is the track's DIST: the engine has no drive of its own)
    const IRECT topKnobs = inner.GetFromTop(inner.H() * 0.40f).GetPadded(-2.f);
    attach(new SkParamKnob(topKnobs.GetGridCell(0, 1, 2).GetCentredInside(58.f),
      pe0 + 10, "CLICK", mintStyle)); // P_E10 CLICK
    attach(new SkParamKnob(topKnobs.GetGridCell(1, 1, 2).GetCentredInside(58.f),
      (uint32_t) skdesk::kP_DIST, "DRIVE", mintStyle)); // P_DIST (fx.c)

    // Bottom area: ADSR Envelope
    const IRECT envArea = inner.GetFromBottom(inner.H() * 0.58f).GetPadded(-2.f);
    const IRECT graphR = envArea.GetFromTop(envArea.H() * 0.52f).GetPadded(-2.f);
    attach(new skengui::TrackAdsrGraph(graphR, pAtk, pDec, pSus, pRel));

    const IRECT knobsR = envArea.GetFromBottom(envArea.H() * 0.46f).GetPadded(-2.f);
    attach(new SkParamKnob(knobsR.GetGridCell(0, 1, 4).GetCentredInside(48.f),
      skdesk::kP_ATK, "A", mintStyle, pAtk));
    attach(new SkParamKnob(knobsR.GetGridCell(1, 1, 4).GetCentredInside(48.f),
      skdesk::kP_DEC, "D", mintStyle, pDec));
    attach(new SkParamKnob(knobsR.GetGridCell(2, 1, 4).GetCentredInside(48.f),
      skdesk::kP_SUS, "S", mintStyle, pSus));
    attach(new SkParamKnob(knobsR.GetGridCell(3, 1, 4).GetCentredInside(48.f),
      skdesk::kP_REL, "R", mintStyle, pRel));
  }
}

} // namespace skwheel
