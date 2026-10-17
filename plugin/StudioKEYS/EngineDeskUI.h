/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "IControls.h"
#include "DeskParams.h"
#include "SkMint.h"
#include "SkPanelControls.h"
#include "sk_host.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

using namespace iplug;
using namespace igraphics;

/** Shared engine desk language — modular panels + draggable ADSR, FM6-adjacent. */
namespace skengui {

/** FL-style header waveform (always live). */
class HeaderWave final : public IControl
{
public:
  explicit HeaderWave(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kBg, mRECT, 3.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 3.f, 0, 1.f);
    float buf[128];
    sk_host_waveform_peek(buf, 128);
    const float mid = mRECT.MH();
    g.DrawLine(IColor(60, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
               mRECT.L + 2.f, mid, mRECT.R - 2.f, mid, 0, 1.f);
    for (int i = 1; i < 128; ++i)
    {
      const float x0 = mRECT.L + 2.f + (mRECT.W() - 4.f) * (float)(i - 1) / 127.f;
      const float x1 = mRECT.L + 2.f + (mRECT.W() - 4.f) * (float)i / 127.f;
      const float y0 = mid - buf[i - 1] * mRECT.H() * 0.42f;
      const float y1 = mid - buf[i] * mRECT.H() * 0.42f;
      g.DrawLine(skmint::kTheme, x0, y0, x1, y1, 0, 1.25f);
    }
  }
};

class ModulePanel final : public IControl
{
public:
  ModulePanel(const IRECT& bounds, const char* title)
  : IControl(bounds)
  , mTitle(title ? title : "")
  {
  }

  void Draw(IGraphics& g) override
  {
    // Sleek dark panel with subtle border
    g.FillRoundRect(skmint::kSurf, mRECT, 6.f);
    const IColor borderCol(90, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B);
    g.DrawRoundRect(borderCol, mRECT, 6.f, 0, 1.25f);
    if (!mTitle.empty())
    {
      const IRECT hdr = mRECT.GetPadded(-8.f).GetFromTop(18.f);
      g.DrawText(IText(14.f, skmint::kAccent, skmint::kFont, EAlign::Near, EVAlign::Top),
                 mTitle.c_str(), hdr);
    }
  }

  void OnMouseDown(float, float, const IMouseMod&) override {}

private:
  std::string mTitle;
};

class TrackAdsrGraph;
inline std::vector<TrackAdsrGraph*>& LiveAdsrGraphs()
{
  static std::vector<TrackAdsrGraph*> s;
  return s;
}

inline void PulseAdsrGraphs();

/** Draggable, responsive track ADSR graph.
 * Reacts instantly to Attack/Decay/Sustain/Release changes,
 * provides live manipulation of nodes and curves with bidirectional knob updating. */
class TrackAdsrGraph final : public IControl
{
public:
  TrackAdsrGraph(const IRECT& bounds, int atkParam, int decParam, int susParam, int relParam)
  : IControl(bounds)
  , mAtk(atkParam)
  , mDec(decParam)
  , mSus(susParam)
  , mRel(relParam)
  {
    LiveAdsrGraphs().push_back(this);
  }

  ~TrackAdsrGraph() override
  {
    auto& v = LiveAdsrGraphs();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }

  void Draw(IGraphics& g) override
  {
    Layout();

    // Dark well with subtle border
    g.FillRoundRect(skmint::kWell, mRECT, 6.f);
    const IColor borderCol(80, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B);
    g.DrawRoundRect(borderCol, mRECT, 6.f, 0, 1.f);

    const float baseY = mPlot.B - 8.f;
    const float topY = mPlot.T + 10.f;
    const float h = std::max(1.f, baseY - topY);

    // Subtle horizontal grid guides (25%, 50%, 75%)
    const IColor gridCol(25, skmint::kText.R, skmint::kText.G, skmint::kText.B);
    for (float frac : {0.25f, 0.5f, 0.75f})
    {
      const float gy = baseY - h * frac;
      g.DrawLine(gridCol, mPlot.L + 4.f, gy, mPlot.R - 4.f, gy, 0, 1.f);
    }

    // Sustain level reference guide line
    const IColor susGuide(45, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B);
    g.DrawLine(susGuide, mX[2], mY[2], mPlot.R - 4.f, mY[2], 0, 1.f);

    // Glowing translucent fill under the ADSR envelope curve
    g.PathMoveTo(mX[0], baseY);
    g.PathLineTo(mX[1], mY[1]);
    g.PathLineTo(mX[2], mY[2]);
    g.PathLineTo(mX[3], mY[3]);
    g.PathLineTo(mX[4], baseY);
    g.PathClose();
    const IColor fillGlow(40, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B);
    g.PathFill(fillGlow);

    // Envelope outline stroke
    for (int i = 0; i < 4; ++i)
      g.DrawLine(skmint::kTheme, mX[i], mY[i], mX[i + 1], mY[i + 1], 0, 2.5f);

    // Stage letters (A, D, S, R) along the bottom
    const IColor stageTextCol(110, skmint::kText.R, skmint::kText.G, skmint::kText.B);
    const float bottomTextY = baseY + 1.f;
    g.DrawText(IText(12.f, stageTextCol, skmint::kFont), "A",
               IRECT(mX[0], bottomTextY, mX[1], mPlot.B));
    g.DrawText(IText(12.f, stageTextCol, skmint::kFont), "D",
               IRECT(mX[1], bottomTextY, mX[2], mPlot.B));
    g.DrawText(IText(12.f, stageTextCol, skmint::kFont), "S",
               IRECT(mX[2], bottomTextY, mX[3], mPlot.B));
    g.DrawText(IText(12.f, stageTextCol, skmint::kFont), "R",
               IRECT(mX[3], bottomTextY, mX[4], mPlot.B));

    // Interactive handles (Nodes 1, 2, 3, 4)
    for (int i = 1; i <= 4; ++i)
    {
      const bool isHot = (mDrag == i || mHover == i);
      const float rHalo = isHot ? 8.f : 6.f;
      const float rCore = isHot ? 4.5f : 3.5f;

      if (isHot)
      {
        const IColor haloCol(80, skmint::kAccent.R, skmint::kAccent.G, skmint::kAccent.B);
        g.FillCircle(haloCol, mX[i], mY[i], rHalo);
      }

      g.FillCircle(isHot ? skmint::kAccent : skmint::kTheme, mX[i], mY[i], rCore);
      g.DrawCircle(skmint::kBg, mX[i], mY[i], rCore, 0, 1.25f);
    }

    // Active tooltip / value badge while dragging or hovering
    int activeNode = (mDrag >= 1 && mDrag <= 4) ? mDrag : mHover;
    if (activeNode >= 1 && activeNode <= 4)
    {
      char valBuf[32];
      if (activeNode == 1)
        snprintf(valBuf, sizeof valBuf, "A: %d", (int)std::lround(mCurrentAtk));
      else if (activeNode == 2)
        snprintf(valBuf, sizeof valBuf, "D: %d  S: %d%%", (int)std::lround(mCurrentDec), (int)std::lround(mCurrentSus * 100.f / 127.f));
      else if (activeNode == 3)
        snprintf(valBuf, sizeof valBuf, "S: %d%%", (int)std::lround(mCurrentSus * 100.f / 127.f));
      else
        snprintf(valBuf, sizeof valBuf, "R: %d", (int)std::lround(mCurrentRel));

      const float bx = std::clamp(mX[activeNode], mPlot.L + 35.f, mPlot.R - 35.f);
      const float by = std::max(mPlot.T + 4.f, mY[activeNode] - 16.f);
      const IRECT badgeR(bx - 32.f, by - 8.f, bx + 32.f, by + 8.f);
      g.FillRoundRect(skmint::kBg, badgeR, 3.f);
      g.DrawRoundRect(skmint::kAccent, badgeR, 3.f, 0, 1.f);
      g.DrawText(IText(12.f, skmint::kAccent, skmint::kFont), valBuf, badgeR);
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod&) override
  {
    Layout();
    int h = HitNode(x, y);
    if (h < 0)
      h = HitSegment(x, y);
    if (h != mHover)
    {
      mHover = h;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    if (mHover >= 0)
    {
      mHover = -1;
      SetDirty(false);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    Layout();
    int h = HitNode(x, y);
    if (h < 0)
      h = HitSegment(x, y);
    mDrag = h;
    SetDirty(false);
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    mDrag = -1;
    PulseAdsrGraphs();
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override
  {
    if (mDrag < 1 || mDrag > 4)
      return;
    Layout();

    const float baseY = mPlot.B - 8.f;
    const float topY = mPlot.T + 10.f;
    const float h = std::max(1.f, baseY - topY);
    const float plotW = mPlot.W();

    auto pushParam = [&](int paramIdx, int fwId, float v) {
      int32_t iv = std::clamp((int32_t)std::lround(v), 0, 127);
      sk_host_set_param((uint32_t)fwId, iv);
      sk_host_ui_pump();
      if (IEditorDelegate* del = GetDelegate())
        del->SendParameterValueFromUI(paramIdx, (double)iv / 127.0);
      if (IGraphics* ui = GetUI())
      {
        ui->ForControlWithParam(paramIdx, [iv](IControl* c) {
          c->SetValue((double)iv / 127.0);
          c->SetDirty(false);
        });
      }
    };

    if (mDrag == 1) // Attack
    {
      const float minW = plotW * 0.04f;
      const float maxW = plotW * 0.32f;
      const float curW = std::clamp(x - mX[0], minW, maxW);
      const float norm = (curW - minW) / (maxW - minW);
      pushParam(mAtk, (int)skdesk::kP_ATK, norm * 127.f);
    }
    else if (mDrag == 2) // Decay + Sustain
    {
      const float minW = plotW * 0.04f;
      const float maxW = plotW * 0.30f;
      const float curW = std::clamp(x - mX[1], minW, maxW);
      const float normT = (curW - minW) / (maxW - minW);
      pushParam(mDec, (int)skdesk::kP_DEC, normT * 127.f);

      const float normL = std::clamp((baseY - y) / h, 0.f, 1.f);
      pushParam(mSus, (int)skdesk::kP_SUS, normL * 127.f);
    }
    else if (mDrag == 3) // Sustain
    {
      const float normL = std::clamp((baseY - y) / h, 0.f, 1.f);
      pushParam(mSus, (int)skdesk::kP_SUS, normL * 127.f);
    }
    else if (mDrag == 4) // Release
    {
      const float minW = plotW * 0.04f;
      const float maxW = plotW * 0.32f;
      const float curW = std::clamp(x - mX[3], minW, maxW);
      const float norm = (curW - minW) / (maxW - minW);
      pushParam(mRel, (int)skdesk::kP_REL, norm * 127.f);
    }

    PulseAdsrGraphs();
    SetDirty(false);
  }

private:
  void Layout()
  {
    mPlot = mRECT.GetPadded(-6.f);

    float atk = 10.f, dec = 70.f, sus = 90.f, rel = 60.f;
    if (IEditorDelegate* del = GetDelegate())
    {
      if (del->GetParam(mAtk)) atk = (float)del->GetParam(mAtk)->Value();
      if (del->GetParam(mDec)) dec = (float)del->GetParam(mDec)->Value();
      if (del->GetParam(mSus)) sus = (float)del->GetParam(mSus)->Value();
      if (del->GetParam(mRel)) rel = (float)del->GetParam(mRel)->Value();
    }
    else
    {
      atk = (float)std::clamp(sk_host_get_param(skdesk::kP_ATK), 0, 127);
      dec = (float)std::clamp(sk_host_get_param(skdesk::kP_DEC), 0, 127);
      sus = (float)std::clamp(sk_host_get_param(skdesk::kP_SUS), 0, 127);
      rel = (float)std::clamp(sk_host_get_param(skdesk::kP_REL), 0, 127);
    }

    mCurrentAtk = atk;
    mCurrentDec = dec;
    mCurrentSus = sus;
    mCurrentRel = rel;

    const float baseY = mPlot.B - 8.f;
    const float topY = mPlot.T + 10.f;
    const float h = std::max(1.f, baseY - topY);
    const float plotW = mPlot.W();

    const float minW = plotW * 0.04f;
    const float wAtk = minW + (plotW * 0.28f) * (atk / 127.f);
    const float wDec = minW + (plotW * 0.26f) * (dec / 127.f);
    const float susY = baseY - h * (sus / 127.f);
    const float wSus = plotW * 0.16f;
    const float wRel = minW + (plotW * 0.28f) * (rel / 127.f);

    mX[0] = mPlot.L + 4.f;
    mY[0] = baseY;

    mX[1] = mX[0] + wAtk;
    mY[1] = topY;

    mX[2] = mX[1] + wDec;
    mY[2] = susY;

    mX[3] = mX[2] + wSus;
    mY[3] = susY;

    mX[4] = std::min(mPlot.R - 4.f, mX[3] + wRel);
    mY[4] = baseY;
  }

  int HitNode(float x, float y) const
  {
    float bestD = 22.f * 22.f;
    int best = -1;
    for (int i = 1; i <= 4; ++i)
    {
      const float dx = x - mX[i], dy = y - mY[i];
      const float d = dx * dx + dy * dy;
      if (d < bestD) { bestD = d; best = i; }
    }
    return best;
  }

  int HitSegment(float x, float y) const
  {
    (void) y;
    if (x <= mX[1]) return 1;
    if (x <= (mX[2] + mX[3]) * 0.5f) return 2;
    if (x <= mX[3] + 8.f) return 3;
    return 4;
  }

  IRECT mPlot;
  float mX[5] {}, mY[5] {};
  int mDrag = -1;
  int mHover = -1;
  int mAtk = 0, mDec = 0, mSus = 0, mRel = 0;
  float mCurrentAtk = 10.f, mCurrentDec = 70.f, mCurrentSus = 90.f, mCurrentRel = 60.f;
};

inline void PulseAdsrGraphs()
{
  for (TrackAdsrGraph* g : LiveAdsrGraphs())
  {
    if (g)
      g->SetDirty(false);
  }
}

struct GroupDef
{
  const char* title;
  int start; /* E0 index */
  int count;
};

inline const GroupDef* GroupsFor(const char* eng, int& nOut)
{
  static const GroupDef kAnalog[] = {{"OSC", 0, 4}, {"FILTER", 4, 4}};
  static const GroupDef kPhase[] = {{"PHASE", 0, 4}, {"LINE", 4, 4}};
  static const GroupDef kLofi[] = {{"CHIP", 0, 4}, {"MOTION", 4, 4}};
  static const GroupDef kSample[] = {{"SAMPLE", 0, 4}, {"TONE", 4, 4}};
  static const GroupDef kVoice[] = {{"VOWEL", 0, 4}, {"TONE", 4, 4}};
  static const GroupDef kTrio[] = {{"OSC", 0, 4}, {"TONE", 4, 4}};
  static const GroupDef kWheel[] = {{"BARS", 0, 4}, {"TONE", 4, 4}};
  static const GroupDef kGrain[] = {{"GRAIN", 0, 4}, {"SPRAY", 4, 4}};
  static const GroupDef kPhys[] = {{"BODY", 0, 4}, {"EXCITE", 4, 4}};
  static const GroupDef kNoise[] = {{"SOURCE", 0, 4}, {"MOTION", 4, 4}};
  static const GroupDef kSlice[] = {{"SLICE", 0, 4}, {"PLAY", 4, 4}};
  static const GroupDef kDrum[] = {{"KIT", 0, 4}, {"HIT", 4, 4}};
  static const GroupDef kDefault[] = {{"EDIT 1", 0, 4}, {"EDIT 2", 4, 4}};

  auto pick = [&](const GroupDef* g, int n) { nOut = n; return g; };
  if (!eng) return pick(kDefault, 2);
  if (!std::strcmp(eng, "ANALOG")) return pick(kAnalog, 2);
  if (!std::strcmp(eng, "PHASE")) return pick(kPhase, 2);
  if (!std::strcmp(eng, "LOFI")) return pick(kLofi, 2);
  if (!std::strcmp(eng, "SAMPLE")) return pick(kSample, 2);
  if (!std::strcmp(eng, "VOICE")) return pick(kVoice, 2);
  if (!std::strcmp(eng, "TRIO")) return pick(kTrio, 2);
  if (!std::strcmp(eng, "WHEEL")) return pick(kWheel, 2);
  if (!std::strcmp(eng, "GRAIN")) return pick(kGrain, 2);
  if (!std::strcmp(eng, "PHYS")) return pick(kPhys, 2);
  if (!std::strcmp(eng, "NOISE")) return pick(kNoise, 2);
  if (!std::strcmp(eng, "SLICE")) return pick(kSlice, 2);
  if (!std::strcmp(eng, "DRUM")) return pick(kDrum, 2);
  return pick(kDefault, 2);
}

/* --------------------------------------------------------------------------------------------
 * P7 desk kit — the pieces every dedicated engine desk shares, so ANALOG/FM6/WHEEL and the
 * nine new desks (LOFI, SAMPLE, VOICE, TRIO, GRAIN, PHYS, NOISE, SLICE, DRUM) read as one
 * instrument instead of one finished UI plus a fallback grid.
 * ------------------------------------------------------------------------------------------ */

using AttachFn = std::function<void(IControl*)>;
/** Attach one engine edit parameter (E0..E7) in a cell, as a knob or a menu by its firmware
 *  descriptor — the plugin-side label override stays in one place (StudioKEYS.cpp). */
using AttachParamFn = std::function<void(const IRECT&, uint32_t)>;

/** The strip's left part: a panel title with a dim caption, e.g. "CHIP" / "4BIT · PLS". */
inline void DrawPanelCaption(IGraphics& g, const IRECT& outer, const char* caption)
{
  const IRECT r = outer.GetReducedFromTop(18.f);
  g.DrawText(IText(11.f, IColor(140, skmint::kText.R, skmint::kText.G, skmint::kText.B),
                   skmint::kFont, EAlign::Far, EVAlign::Top),
             caption ? caption : "", r);
}

/** Shared ADSR strip: the draggable graph on the left, ATK/DEC/SUS/REL on the right.
 *  Every desk calls this, so the envelope always sits in the same place at the same size.
 *  A short strip (the engine desks get ~77 px) drops the panel's title row so the graph and the
 *  knobs keep their size; the frame still groups them. */
inline void AttachAdsrStrip(const AttachFn& attach, const IRECT& strip, const IVStyle& style,
                            int pAtk, int pDec, int pSus, int pRel)
{
  const bool compact = strip.H() < 92.f;
  attach(new ModulePanel(strip, compact ? "" : "ENVELOPE"));
  const IRECT inner = strip.GetReducedFromTop(compact ? 6.f : 22.f).GetPadded(compact ? -4.f : -6.f);
  auto pulse = []() { PulseAdsrGraphs(); };

  const IRECT graphR = inner.GetFromLeft(inner.W() * 0.44f).GetPadded(-2.f);
  attach(new TrackAdsrGraph(graphR, pAtk, pDec, pSus, pRel));

  const IRECT knobs = inner.GetReducedFromLeft(inner.W() * 0.46f).GetPadded(-2.f);
  struct KnobDef
  {
    uint32_t id;
    const char* label;
    int param;
  };
  const KnobDef defs[4] = {
    {(uint32_t) skdesk::kP_ATK, "ATK", pAtk},
    {(uint32_t) skdesk::kP_DEC, "DEC", pDec},
    {(uint32_t) skdesk::kP_SUS, "SUS", pSus},
    {(uint32_t) skdesk::kP_REL, "REL", pRel},
  };
  for (int i = 0; i < 4; ++i)
  {
    auto* k = new SkParamKnob(knobs.GetGridCell(i, 1, 4).GetCentredInside(54.f),
                              defs[i].id, defs[i].label, style, defs[i].param);
    k->SetOnChange(pulse);
    attach(k);
  }
}

/** A grouped 2x2 parameter block: module panel plus the four E params starting at `first`. */
inline void AttachParamBlock(const AttachFn& attach, const AttachParamFn& param,
                             const IRECT& col, const char* title, int first, int count = 4)
{
  attach(new ModulePanel(col, title));
  const IRECT inner = col.GetReducedFromTop(22.f).GetPadded(-3.f);
  for (int i = 0; i < count; ++i)
    param(inner.GetGridCell(i, 2, 2).GetPadded(-3.f), (uint32_t)(first + i));
}

/** Live audio scope frame: dark well, grid, border. Returns the plot area inside it. */
inline IRECT ScopeWell(IGraphics& g, const IRECT& r, bool grid = true)
{
  g.FillRoundRect(skmint::kBg, r, 4.f);
  g.DrawRoundRect(IColor(90, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), r, 4.f, 0, 1.25f);
  const IRECT plot = r.GetPadded(-6.f);
  if (grid)
  {
    const IColor gridCol(26, skmint::kText.R, skmint::kText.G, skmint::kText.B);
    for (int i = 1; i < 4; ++i)
    {
      const float y = plot.T + plot.H() * (float)i / 4.f;
      g.DrawLine(gridCol, plot.L, y, plot.R, y, 0, 1.f);
    }
    const float mid = plot.MH();
    g.DrawLine(IColor(50, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
               plot.L, mid, plot.R, mid, 0, 1.f);
  }
  return plot;
}

/** Normalise a firmware param (respecting its own min..max) to 0..1. */
inline float ParamNorm(uint32_t fwId)
{
  struct sk_host_param_info info = {0};
  if (!sk_host_get_param_info(fwId, &info) || info.max <= info.min)
    return std::clamp((float) sk_host_get_param(fwId) / 127.f, 0.f, 1.f);
  return std::clamp((float)(sk_host_get_param(fwId) - info.min) / (float)(info.max - info.min), 0.f, 1.f);
}

/** Small "NAME value" chip drawn in a scope corner. */
inline void DrawChip(IGraphics& g, const IRECT& r, const char* label, const char* value, bool hot = false)
{
  g.FillRoundRect(hot ? skmint::kTheme : skmint::kWell, r, 2.f);
  g.DrawRoundRect(skmint::kTheme, r, 2.f, 0, 1.f);
  char buf[64];
  snprintf(buf, sizeof buf, "%s %s", label ? label : "", value ? value : "");
  g.DrawText(IText(11.f, hot ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont),
             buf, r);
}

/** Read a firmware param formatted (for a chip / caption). */
inline void ParamText(uint32_t fwId, char* out, int n)
{
  const int32_t v = sk_host_get_param(fwId);
  if (!n)
    return;
  if (!sk_host_format_param(fwId, v, out, n))
    snprintf(out, (size_t) n, "%d", (int) v);
}

} // namespace skengui
