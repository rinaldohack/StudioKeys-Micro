#pragma once

/* P16 — the companion's AUTOMATION editor.
 *
 * The CFW captures knob motion into the sequencer's sparse step pool (AUTO REC / transport
 * REC); this window shows the same pool for the selected track and edits it: one row per
 * captured parameter, 16 steps per bank (4 banks), the value in every cell.
 *   click an empty cell      add the event at the parameter's current value
 *   click a filled cell      remove it (right-click does the same)
 *   wheel over a cell        the event's value +/- 1
 *   PLAY / AUTO REC / CLEAR  the track's motion playback, the arm, the whole pool
 * No new storage: every edit goes through the firmware's motion.c, so the CFW sees it
 * immediately (the same pool the project saves). */

#include "sk_host.h"
#include "SkMint.h"
#include "PatchOverlays.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace skmotion {

inline constexpr uint32_t kStepsPerBank = 16u;

/* The chrome's AUTOMATION chip: lit when the track has motion or AUTO REC is armed. */
class MotionButton final : public IControl
{
public:
  MotionButton(const IRECT& bounds, std::function<void()> onClick)
  : IControl(bounds)
  , mOnClick(std::move(onClick))
  {
  }

  void Draw(IGraphics& g) override
  {
    const uint32_t tr = sk_host_selected_track();
    const bool on = sk_host_motion_armed(tr) || sk_host_motion_count(tr) != 0u;
    g.FillRoundRect(on ? skmint::kTheme : skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(on ? skmint::kTheme : skmint::kSurf, mRECT, 4.f, 0, 1.25f);
    g.DrawText(IText(12.f, on ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont, EAlign::Center,
                     EVAlign::Middle),
               "AUTO", mRECT);
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

class MotionWindow final : public skoverlay::SkWindow
{
public:
  explicit MotionWindow(const IRECT& bounds, CloseFn onClose)
  : SkWindow(bounds, "AUTOMATION · STEP MOTION", std::move(onClose))
  {
  }

protected:
  float WinW() const override { return 700.f; }
  float WinH() const override { return 470.f; }
  int RowCount() const override { return 0; }

  void OnLayout(const IRECT& in) override
  {
    mTrack = sk_host_selected_track();
    Collect();

    const IRECT top = in.GetFromTop(38.f);
    const IRECT tabs = top.GetFromLeft(230.f);
    for (uint32_t i = 0; i < 4u; ++i)
      mTab[i] = tabs.GetGridCell((int) i, 1, 4).GetPadded(-3.f);

    const IRECT verbs = top.GetFromRight(330.f);
    mPlay = verbs.GetGridCell(0, 1, 3).GetPadded(-3.f);
    mAuto = verbs.GetGridCell(1, 1, 3).GetPadded(-3.f);
    mClear = verbs.GetGridCell(2, 1, 3).GetPadded(-3.f);

    const IRECT bank = in.GetReducedFromTop(46.f).GetFromTop(30.f);
    mPrev = bank.GetFromLeft(34.f).GetPadded(-2.f);
    mNext = bank.GetFromRight(34.f).GetPadded(-2.f);
    mBankText = IRECT(mPrev.R + 6.f, bank.T, mNext.L - 6.f, bank.B);

    mHint = in.GetFromBottom(30.f);
    mGrid = in.GetReducedFromTop(84.f).GetReducedFromBottom(36.f);
    mNames = mGrid.GetFromLeft(88.f);
    mCells = mGrid.GetReducedFromLeft(92.f);

    const float rowH = mRows ? std::min(26.f, mCells.H() / (float) mRows) : 26.f;
    mRowH = rowH;
    for (uint32_t r = 0; r < mRows && r < kMaxRows; ++r)
    {
      mRowR[r] = IRECT(mNames.L, mCells.T + rowH * (float) r, mNames.R, mCells.T + rowH * (float) (r + 1));
      for (uint32_t c = 0; c < kStepsPerBank; ++c)
      {
        const float x0 = mCells.L + mCells.W() * (float) c / (float) kStepsPerBank;
        const float x1 = mCells.L + mCells.W() * (float) (c + 1u) / (float) kStepsPerBank;
        mCell[r][c] = IRECT(x0, mRowR[r].T, x1, mRowR[r].B).GetPadded(-1.f);
      }
    }
  }

  void DrawBody(IGraphics& g) override
  {
    const uint32_t on = sk_host_motion_on(mTrack) ? 1u : 0u;
    const uint32_t armed = sk_host_motion_armed(mTrack) ? 1u : 0u;

    for (uint32_t i = 0; i < 4u; ++i)
    {
      const std::string t = "T" + std::to_string(i + 1u);
      Chip(g, mTab[i], t.c_str(), i == mTrack, skmint::kTrack[i], true);
    }

    Chip(g, mPlay, on ? "PLAY ON" : "PLAY OFF", on != 0u, skmint::kTheme, true);
    Chip(g, mAuto, armed ? "AUTO REC" : "AUTO OFF", armed != 0u, skmint::kAccent, true);
    Chip(g, mClear, mClearArm ? "SURE?" : "CLEAR", mClearArm, skmint::kAccent, mRows != 0u);

    char bank[48];
    std::snprintf(bank, sizeof bank, "STEPS %u-%u  (%u/4)", (unsigned) (mBank * kStepsPerBank + 1u),
                  (unsigned) ((mBank + 1u) * kStepsPerBank), (unsigned) (mBank + 1u));
    g.DrawText(IText(13.f, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle), bank, mBankText);

    g.FillRoundRect(skmint::kWell, mGrid, 6.f);
    if (!mRows)
    {
      g.DrawText(IText(14.f, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 "NO AUTOMATION · arm AUTO REC on the CFW (or turn a knob while REC plays)",
                 mGrid.GetPadded(-20.f));
      g.DrawText(IText(11.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 "Every knob move while AUTO REC or REC is armed becomes a step event on the selected track.",
                 mHint);
      return;
    }

    for (uint32_t r = 0; r < mRows && r < kMaxRows; ++r)
    {
      char label[16];
      sk_host_motion_label(mTrack, mParam[r], label, sizeof label);
      g.DrawText(IText(12.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle), label,
                 mRowR[r].GetReducedFromRight(2.f));
      for (uint32_t c = 0; c < kStepsPerBank; ++c)
      {
        int32_t v = 0;
        if (Find(mParam[r], mBank * kStepsPerBank + c, &v))
        {
          g.FillRoundRect(skmint::kTheme, mCell[r][c], 3.f);
          char b[8];
          std::snprintf(b, sizeof b, "%d", (int) v);
          g.DrawText(IText(11.f, skmint::InkOn(skmint::kTheme), skmint::kFont, EAlign::Center, EVAlign::Middle), b,
                     mCell[r][c]);
        }
        else
          g.FillRoundRect(skmint::kBg, mCell[r][c], 3.f);
      }
    }
    if (mRows > kMaxRows)
      g.DrawText(IText(11.f, skmint::kAccent, skmint::kFont, EAlign::Far, EVAlign::Middle),
                 (std::to_string(mRows - kMaxRows) + " MORE PARAMETERS").c_str(),
                 IRECT(mGrid.L, mGrid.B + 2.f, mGrid.R, mGrid.B + 28.f));

    g.DrawText(IText(11.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle),
               "Click a cell to add / remove the event. Wheel over a cell: its value +/-1.", mHint);
  }

  void OnBodyDown(float x, float y) override
  {
    Layout();
    mClearArm = false;
    for (uint32_t i = 0; i < 4u; ++i)
      if (mTab[i].Contains(x, y))
      {
        sk_host_select_track(i);
        mTrack = i;
        Redraw();
        return;
      }
    if (mPrev.Contains(x, y) || mNext.Contains(x, y))
    {
      mBank = mPrev.Contains(x, y) ? (mBank + 3u) % 4u : (mBank + 1u) % 4u;
      Redraw();
      return;
    }
    if (mPlay.Contains(x, y))
    {
      sk_host_motion_set_on(mTrack, sk_host_motion_on(mTrack) ? 0 : 1);
      Redraw();
      return;
    }
    if (mAuto.Contains(x, y))
    {
      sk_host_motion_set_armed(mTrack, sk_host_motion_armed(mTrack) ? 0 : 1);
      Redraw();
      return;
    }
    if (mClear.Contains(x, y) && mRows)
    {
      if (!mClearArm)
        mClearArm = true;
      else
      {
        sk_host_motion_clear(mTrack);
        mClearArm = false;
      }
      Redraw();
      return;
    }
    int row = 0, col = 0;
    if (CellAt(x, y, &row, &col))
    {
      const uint32_t step = mBank * kStepsPerBank + (uint32_t) col;
      int32_t v = 0;
      if (Find(mParam[row], step, &v))
        sk_host_motion_remove_event(mTrack, step, mParam[row]);
      else
        sk_host_motion_set_event(mTrack, step, mParam[row], sk_host_trk_param(mTrack, mParam[row]));
      Redraw();
    }
  }

  void OnBodyRightDown(float x, float y) override
  {
    Layout();
    int row = 0, col = 0;
    if (CellAt(x, y, &row, &col))
    {
      const uint32_t step = mBank * kStepsPerBank + (uint32_t) col;
      if (sk_host_motion_remove_event(mTrack, step, mParam[row]) == 0)
        Redraw();
    }
  }

  void OnBodyWheel(float x, float y, float d) override
  {
    Layout();
    int row = 0, col = 0;
    if (!d || !CellAt(x, y, &row, &col))
      return;
    const uint32_t step = mBank * kStepsPerBank + (uint32_t) col;
    int32_t v = 0;
    if (!Find(mParam[row], step, &v))
      return;
    int lo = 0, hi = 127;
    sk_host_motion_range(mTrack, mParam[row], &lo, &hi);
    int32_t nv = v + (d > 0.f ? 1 : -1);
    nv = std::max<int32_t>(lo, std::min<int32_t>(hi, nv));
    if (nv != v && sk_host_motion_set_event(mTrack, step, mParam[row], nv) == 0)
      Redraw();
  }

private:
  static constexpr uint32_t kMaxRows = 10u;

  void Chip(IGraphics& g, const IRECT& r, const char* label, bool on, const IColor& col, bool enabled)
  {
    g.FillRoundRect(on ? col : skmint::kWell, r, 5.f);
    g.DrawRoundRect(enabled ? col : skmint::kSurf, r, 5.f, 0, on ? 2.f : 1.f);
    const IColor ink = on ? skmint::InkOn(col) : (enabled ? skmint::kInk : skmint::kText);
    g.DrawText(IText(12.f, ink, skmint::kFont, EAlign::Center, EVAlign::Middle), label, r);
  }

  void Collect()
  {
    mRows = 0;
    const uint32_t n = sk_host_motion_count(mTrack);
    for (uint32_t i = 0; i < n; ++i)
    {
      uint32_t step = 0, param = 0;
      int32_t v = 0;
      if (!sk_host_motion_event(mTrack, i, &step, &param, &v))
        continue;
      bool have = false;
      for (uint32_t r = 0; r < mRows; ++r)
        have = have || mParam[r] == param;
      if (!have && mRows < 64u)
        mParam[mRows++] = param;
    }
  }

  bool Find(uint32_t param, uint32_t step, int32_t* out) const
  {
    const uint32_t n = sk_host_motion_count(mTrack);
    for (uint32_t i = 0; i < n; ++i)
    {
      uint32_t s = 0, p = 0;
      int32_t v = 0;
      if (!sk_host_motion_event(mTrack, i, &s, &p, &v))
        continue;
      if (s == step && p == param)
      {
        if (out)
          *out = v;
        return true;
      }
    }
    return false;
  }

  bool CellAt(float x, float y, int* row, int* col) const
  {
    for (uint32_t r = 0; r < mRows && r < kMaxRows; ++r)
      for (uint32_t c = 0; c < kStepsPerBank; ++c)
        if (mCell[r][c].Contains(x, y))
        {
          *row = (int) r;
          *col = (int) c;
          return true;
        }
    return false;
  }

  void Redraw()
  {
    SetDirty(false);
    if (IGraphics* g = GetUI())
      g->SetAllControlsDirty();
  }

  uint32_t mTrack = 0, mBank = 0;
  uint32_t mParam[64] = {};
  uint32_t mRows = 0;
  bool mClearArm = false;
  float mRowH = 26.f;
  IRECT mTab[4], mPlay, mAuto, mClear, mPrev, mNext, mBankText, mGrid, mNames, mCells, mHint;
  IRECT mRowR[kMaxRows], mCell[kMaxRows][kStepsPerBank];
};

} // namespace skmotion
