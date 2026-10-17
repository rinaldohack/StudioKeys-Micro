/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#pragma once

/* P15: the user sample slots as drop targets. A WAV dropped on USR1..USR3 is decoded here and
 * handed to the host, which resamples to 22.05 kHz, IMA-encodes it and writes the slot record
 * exactly as a USB upload does (smp_user_scan rescans it, so SAMPLE and SLICE play it at once).
 * The bay sits under the SAMPLE and SLICE desks, where the SET / SRC knob points at these slots. */

#include "EngineDeskUI.h"
#include "SkWav.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace skslot {

using namespace iplug;
using namespace igraphics;

/** One slot pad: name + length when loaded, "DROP WAV" when empty; CLR erases it. */
class SlotDrop final : public IControl
{
public:
  SlotDrop(const IRECT& bounds, int slot)
  : IControl(bounds)
  , mSlot(slot)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool used = sk_host_sample_slot_used((uint32_t) mSlot) != 0;
    const IColor edge = mErr ? IColor(200, 232, 96, 96) : used ? skmint::kTheme : skmint::kWell;
    g.FillRoundRect(used ? skmint::kSurf : skmint::kBg, mRECT, 4.f);
    g.DrawRoundRect(edge, mRECT, 4.f, 0, mErr ? 1.6f : 1.f);

    const IRECT in = mRECT.GetPadded(-6.f);
    const IRECT title = in.GetFromTop(16.f);
    const IRECT body = in.GetReducedFromTop(16.f);

    char head[32];
    snprintf(head, sizeof head, "USR%d", mSlot + 1);
    g.DrawText(IText(12.f, used ? skmint::kTheme : skmint::kInk, skmint::kFont, EAlign::Near,
                     EVAlign::Middle),
               head, title);
    if (used)
    {
      const std::string name = sk_host_sample_slot_name((uint32_t) mSlot);
      g.DrawText(IText(12.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 name.c_str(), title.GetReducedFromLeft(38.f));
      mClear = title.GetFromRight(30.f).GetCentredInside(28.f, 13.f);
      g.FillRoundRect(skmint::kWell, mClear, 2.f);
      g.DrawRoundRect(IColor(120, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), mClear, 2.f,
                      0, 1.f);
      g.DrawText(IText(9.f, skmint::kText, skmint::kFont), "CLR", mClear);
    }
    else
      mClear = IRECT();

    if (mMsg.empty())
    {
      if (used)
      {
        char info[48];
        const uint32_t frames = sk_host_sample_slot_frames((uint32_t) mSlot);
        snprintf(info, sizeof info, "%.2f s · %u smp", frames ? (double) frames / 22050.0 : 0.0,
                 frames);
        g.DrawText(IText(11.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Top), info,
                   body);
        g.DrawText(IText(10.f, IColor(150, skmint::kText.R, skmint::kText.G, skmint::kText.B),
                         skmint::kFont, EAlign::Near, EVAlign::Bottom),
                    "DROP TO REPLACE", body);
      }
      else
      {
        g.DrawText(IText(12.f, skmint::kAccent, skmint::kFont, EAlign::Near, EVAlign::Top),
                   "DROP A WAV", body);
        g.DrawText(IText(10.f, IColor(150, skmint::kText.R, skmint::kText.G, skmint::kText.B),
                         skmint::kFont, EAlign::Near, EVAlign::Bottom),
                   "PCM / FLOAT · 3.2 s", body);
      }
    }
    else
    {
      g.DrawText(IText(10.f, mErr ? IColor(230, 232, 96, 96) : skmint::kTheme, skmint::kFont,
                       EAlign::Near, EVAlign::Top),
                 mMsg.c_str(), body);
      g.DrawText(IText(10.f, IColor(150, skmint::kText.R, skmint::kText.G, skmint::kText.B),
                       skmint::kFont, EAlign::Near, EVAlign::Bottom),
                 used ? "DROP TO REPLACE" : "DROP A WAV", body);
    }
  }

  void OnDrop(const char* path) override { Load(path); }

  void OnDropMultiple(const std::vector<const char*>& paths) override
  {
    /* One slot holds 1..16 zones: several files would need a root split each, so take the first
     * and say so rather than silently dropping the rest. */
    if (!paths.empty())
    {
      Load(paths[0]);
      if (paths.size() > 1)
        SetMsg("one WAV per slot", true);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    if (!mClear.Empty() && mClear.Contains(x, y))
    {
      const int rc = sk_host_sample_slot_clear((uint32_t) mSlot);
      if (rc == 0)
      {
        mMsg = "cleared";
        mErr = false;
      }
      else
        SetMsg(sk_host_sample_slot_error(rc), true);
      SetDirty(false);
    }
  }

private:
  void SetMsg(const std::string& s, bool err)
  {
    mMsg = s;
    mErr = err;
    SetDirty(false);
  }

  /** The file's stem, the slot's 8-char header name. */
  static std::string Stem(const char* path)
  {
    std::string s(path ? path : "");
    const size_t a = s.find_last_of("/\\");
    if (a != std::string::npos)
      s.erase(0, a + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos)
      s.erase(dot);
    if (s.empty())
      s = "SAMPLE";
    return s;
  }

  /** Point the SAMPLE / SLICE source knob at the slot just loaded (when the engine has one). */
  void SelectSet(int slot)
  {
    const uint32_t pe0 = sk_host_pe0_id();
    struct sk_host_param_info info = {0};
    if (!sk_host_get_param_info(pe0, &info) || !info.names)
      return;
    char want[8];
    snprintf(want, sizeof want, "USR%d", slot + 1);
    for (int i = info.min; i <= info.max; ++i)
      if (info.names[i] && !std::strcmp(info.names[i], want))
      {
        sk_host_set_param(pe0, (int32_t) i);
        sk_host_ui_pump();
        if (IGraphics* ui = GetUI())
          ui->SetAllControlsDirty();
        return;
      }
  }

  void Load(const char* path)
  {
    skwav::Audio a;
    std::string err;
    if (!skwav::Load(path, a, err))
    {
      SetMsg(err, true);
      return;
    }
    if (a.rate < 5512u || a.rate > 192000u)
    {
      SetMsg("rate 5.5k..192k only", true);
      return;
    }
    std::vector<int16_t> pcm(a.mono.size());
    for (size_t i = 0; i < a.mono.size(); ++i)
    {
      const float v = std::clamp(a.mono[i], -1.f, 1.f);
      pcm[i] = (int16_t) std::lround(v * 32767.f);
    }
    const std::string name = Stem(path);
    const int rc = sk_host_sample_slot_write((uint32_t) mSlot, pcm.data(), (uint32_t) pcm.size(),
                                             a.rate, name.c_str());
    if (rc)
    {
      SetMsg(sk_host_sample_slot_error(rc), true);
      return;
    }
    SelectSet(mSlot);
    mMsg.clear();
    mErr = false;
    SetDirty(false);
  }

  int mSlot = 0;
  IRECT mClear;
  std::string mMsg;
  bool mErr = false;
};

/** Three pads in a row (the bay under a desk's map / scope). */
inline void AttachUserSlotBay(const skengui::AttachFn& attach, const IRECT& bounds)
{
  for (int k = 0; k < 3; ++k)
    attach(new SlotDrop(bounds.GetGridCell(k, 1, 3).GetPadded(-3.f), k));
}

} // namespace skslot
