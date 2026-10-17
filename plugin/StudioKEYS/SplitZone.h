/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

/* P13 companion mirror of the CFW key split.
 *
 * The firmware routes live keys below SPLIT NOTE to the LOW track and every key at or above it to
 * the HIGH track (host API: sk_host_split_get / sk_host_split_set). This header draws that setting
 * on the companion's virtual keybed — a tinted lower/upper zone, the split line and the two track
 * tags — and provides the chrome button that opens skoverlay::SplitWindow, where the tracks, the
 * split note and the on/off state are edited. Drawing only: the keybed overlay never eats a mouse
 * event, so every key keeps playing. */

#include "SkMint.h"
#include "sk_host.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

using namespace iplug;
using namespace igraphics;

namespace sksplit {

struct State
{
  int note = 0; /* 0 = split off */
  int lo = 0;
  int hi = 1;
};

inline State Read()
{
  State s;
  int note = 0, lo = 0, hi = 0;
  sk_host_split_get(&note, &lo, &hi);
  s.note = std::clamp(note, 0, 127);
  s.lo = std::clamp(lo, 0, 3);
  s.hi = std::clamp(hi, 0, 3);
  return s;
}

inline void Write(int note, int lo, int hi)
{
  sk_host_split_set(std::clamp(note, 0, 127), std::clamp(lo, 0, 3), std::clamp(hi, 0, 3));
}

/* "C4" for 60 (the CFW's octave numbering: MIDI 60 = C4). */
inline std::string NoteName(int note)
{
  static const char* const kName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  char b[12];
  const int n = std::clamp(note, 0, 127);
  std::snprintf(b, sizeof b, "%s%d", kName[n % 12], n / 12 - 1);
  return b;
}

/* IVKeyboardControl's own layout rule (white keys equal, black keys 0.6 of one, offsets by pitch
 * class) so the zone boundary lands exactly on the key that changes track. */
inline float ShiftForPitch(int pitch)
{
  switch (pitch % 12)
  {
    case 1: return 7.f / 12.f;
    case 3: return 5.f / 12.f;
    case 6: return 2.f / 3.f;
    case 8: return 0.5f;
    case 10: return 1.f / 3.f;
    default: return 0.f;
  }
}

inline bool IsBlackKey(int note)
{
  switch (note % 12)
  {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
  }
}

class KeybedOverlay final : public IControl
{
public:
  KeybedOverlay(const IRECT& bounds, int loNote, int hiNote)
  : IControl(bounds)
  , mLo(loNote)
  , mHi(hiNote)
  {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override
  {
    const State s = Read();
    if (!s.note)
      return;

    float keyW = 0.f, blackW = 0.f;
    const float splitX = XOfNote(s.note, keyW, blackW);
    const IRECT r = mRECT;
    const float x = std::clamp(splitX, r.L + 1.f, r.R - 1.f);
    const IColor low = skmint::kTrack[s.lo % 4], high = skmint::kTrack[s.hi % 4];

    if (x > r.L + 1.f)
      g.FillRect(IColor(30, low.R, low.G, low.B), IRECT(r.L, r.T, x, r.B));
    if (x < r.R - 1.f)
      g.FillRect(IColor(30, high.R, high.G, high.B), IRECT(x, r.T, r.R, r.B));

    /* the split line itself, with the note's name on a small tab so the exact key is readable */
    g.FillRect(skmint::kAccent, IRECT(x - 1.f, r.T, x + 1.f, r.B));
    char label[24];
    std::snprintf(label, sizeof label, "%s", NoteName(s.note).c_str());
    const IRECT tab(x - 17.f, r.T + 2.f, x + 17.f, r.T + 16.f);
    g.FillRoundRect(skmint::kAccent, tab, 3.f);
    g.DrawText(IText(11.f, skmint::InkOn(skmint::kAccent), skmint::kFont, EAlign::Center, EVAlign::Middle),
               label, tab);

    /* zone tags: which mixer channel each side plays */
    char tag[8];
    auto drawTag = [&](const IRECT& t, int tr) {
      g.FillRoundRect(IColor(215, skmint::kTrack[tr % 4].R, skmint::kTrack[tr % 4].G, skmint::kTrack[tr % 4].B),
                      t, 3.f);
      std::snprintf(tag, sizeof tag, "T%d", tr + 1);
      g.DrawText(IText(11.f, IColor(255, 16, 16, 18), skmint::kFont, EAlign::Center, EVAlign::Middle), tag, t);
    };
    const float tw = 26.f;
    if (x - r.L > tw + 8.f)
      drawTag(IRECT(r.L + 5.f, r.B - 18.f, r.L + 5.f + tw, r.B - 4.f), s.lo);
    if (r.R - x > tw + 8.f)
      drawTag(IRECT(r.R - 5.f - tw, r.B - 18.f, r.R - 5.f, r.B - 4.f), s.hi);
  }

private:
  float XOfNote(int note, float& whiteW, float& blackW) const
  {
    const int lo = mLo, hi = mHi;
    int whites = 0;
    for (int n = lo; n <= hi; ++n)
      if (!IsBlackKey(n))
        ++whites;
    const float padStart = ShiftForPitch(lo);
    const float padEnd = IsBlackKey(hi) ? 1.f - ShiftForPitch(hi) : 0.f;
    whiteW = whites > 0 ? mRECT.W() / ((float) whites + 0.6f * (padStart + padEnd)) : mRECT.W();
    blackW = 0.6f * whiteW;

    float prev = mRECT.L;
    float left = mRECT.L;
    for (int n = lo; n <= hi; ++n)
    {
      if (IsBlackKey(n))
      {
        left = prev - ShiftForPitch(n) * blackW;
        if (n == note)
          return left + blackW * 0.5f;
      }
      else
      {
        left = prev;
        prev += whiteW;
        if (n == note)
          return left;
      }
    }
    return left;
  }

  int mLo, mHi;
};

/* Chrome chip: SPLIT lights up while a split is armed and opens the editor. */
class SplitButton final : public IControl
{
public:
  SplitButton(const IRECT& bounds, std::function<void()> onClick)
  : IControl(bounds)
  , mOnClick(std::move(onClick))
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool on = Read().note != 0;
    g.FillRoundRect(on ? skmint::kTheme : skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(on ? skmint::kTheme : skmint::kSurf, mRECT, 4.f, 0, 1.25f);
    g.DrawText(IText(12.f, on ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont, EAlign::Center,
                     EVAlign::Middle),
               "SPLIT", mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (mOnClick)
      mOnClick();
    SetDirty(false);
  }

private:
  std::function<void()> mOnClick;
};

} // namespace sksplit
