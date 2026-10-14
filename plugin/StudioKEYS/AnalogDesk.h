#pragma once

#include "IControls.h"
#include "EngineDeskUI.h"
#include "SkMint.h"
#include "SkPanelControls.h"

#include <functional>

/** OsTIrus-inspired Easy Edit for ANALOG — layout reference only (no Virus TI BIOS). */
namespace skanalog {

using namespace iplug;
using namespace igraphics;

struct EasyParams
{
  uint32_t e0, e1, e2, e3, e4, e5, e6, e7;
  int atkParam = 7;
  int decParam = 8;
  int susParam = 9;
  int relParam = 10;
};

inline void AttachEasyEdit(const std::function<void(IControl*)>& attach,
                           const IRECT& body, const IVStyle& mintStyle, const EasyParams& p)
{
  const float w = body.W();

  // Column 1: OSCILLATOR (28% width)
  const IRECT oscCol = body.GetFromLeft(w * 0.28f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(oscCol, "OSCILLATOR"));
  {
    const IRECT kArea = oscCol.GetReducedFromTop(26.f).GetPadded(-4.f);
    // Row 0: WAVE, DETUNE
    const IRECT r0 = kArea.GetFromTop(kArea.H() * 0.50f);
    attach(new SkParamMenu(r0.GetGridCell(0, 1, 2).GetCentredInside(84.f, 42.f),
      p.e0, "WAVE", mintStyle));
    attach(new SkParamKnob(r0.GetGridCell(1, 1, 2).GetCentredInside(58.f),
      p.e1, "DETUNE", mintStyle));

    // Row 1: MIX, SUB
    const IRECT r1 = kArea.GetFromBottom(kArea.H() * 0.50f);
    attach(new SkParamKnob(r1.GetGridCell(0, 1, 2).GetCentredInside(58.f),
      p.e2, "MIX", mintStyle));
    attach(new SkParamKnob(r1.GetGridCell(1, 1, 2).GetCentredInside(58.f),
      p.e3, "SUB", mintStyle));
  }

  // Column 2: FILTER (30% width)
  const IRECT filtCol = body.GetReducedFromLeft(w * 0.28f).GetFromLeft(w * 0.30f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(filtCol, "FILTER"));
  {
    const IRECT kArea = filtCol.GetReducedFromTop(26.f).GetPadded(-4.f);
    // Row 0: CUTOFF & RESO (prominent hero knobs)
    const IRECT r0 = kArea.GetFromTop(kArea.H() * 0.53f);
    attach(new SkParamKnob(r0.GetGridCell(0, 1, 2).GetCentredInside(68.f),
      p.e4, "CUTOFF", mintStyle));
    attach(new SkParamKnob(r0.GetGridCell(1, 1, 2).GetCentredInside(68.f),
      p.e5, "RESO", mintStyle));

    // Row 1: DRIVE & KEYTRK
    const IRECT r1 = kArea.GetFromBottom(kArea.H() * 0.47f);
    attach(new SkParamKnob(r1.GetGridCell(0, 1, 2).GetCentredInside(58.f),
      p.e6, "DRIVE", mintStyle));
    attach(new SkParamKnob(r1.GetGridCell(1, 1, 2).GetCentredInside(58.f),
      p.e7, "KEYTRK", mintStyle));
  }

  // Column 3: ENVELOPE (42% width)
  const IRECT envCol = body.GetFromRight(w * 0.42f).GetPadded(-3.f);
  attach(new skengui::ModulePanel(envCol, "ENVELOPE"));
  {
    const IRECT eArea = envCol.GetReducedFromTop(26.f).GetPadded(-4.f);
    // Top ~54%: Wide interactive ADSR Graph
    const IRECT graphR = eArea.GetFromTop(eArea.H() * 0.54f).GetPadded(-2.f);
    attach(new skengui::TrackAdsrGraph(graphR, p.atkParam, p.decParam, p.susParam, p.relParam));

    // Bottom ~46%: 4 envelope knobs aligned right under the graph
    const IRECT knobsR = eArea.GetFromBottom(eArea.H() * 0.46f).GetPadded(-2.f);
    auto pulse = []() { skengui::PulseAdsrGraphs(); };

    auto kAtk = new SkParamKnob(knobsR.GetGridCell(0, 1, 4).GetCentredInside(54.f),
      (uint32_t)skdesk::kP_ATK, "ATK", mintStyle, p.atkParam);
    kAtk->SetOnChange(pulse);
    attach(kAtk);

    auto kDec = new SkParamKnob(knobsR.GetGridCell(1, 1, 4).GetCentredInside(54.f),
      (uint32_t)skdesk::kP_DEC, "DEC", mintStyle, p.decParam);
    kDec->SetOnChange(pulse);
    attach(kDec);

    auto kSus = new SkParamKnob(knobsR.GetGridCell(2, 1, 4).GetCentredInside(54.f),
      (uint32_t)skdesk::kP_SUS, "SUS", mintStyle, p.susParam);
    kSus->SetOnChange(pulse);
    attach(kSus);

    auto kRel = new SkParamKnob(knobsR.GetGridCell(3, 1, 4).GetCentredInside(54.f),
      (uint32_t)skdesk::kP_REL, "REL", mintStyle, p.relParam);
    kRel->SetOnChange(pulse);
    attach(kRel);
  }
}

} // namespace skanalog
