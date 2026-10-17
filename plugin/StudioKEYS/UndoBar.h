/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* P18: the UNDO / REDO chips on the companion's chrome. Two small pills that mirror the CFW's
 * SAVE-held undo: a step is the selected track's sound (engine, preset, parameters, FM6 patch,
 * motion), and the chips light up only while their stack has something to swap. The chip shows
 * the depth so a long session reads as "UNDO 5" rather than a dead button. Cmd/Ctrl+Z and
 * Shift+Cmd/Ctrl+Z (Ctrl+Y) drive the same calls from StudioKEYS::LayoutUI's key handler. */
#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "sk_host.h"
#include "SkMint.h"

#include <cstdio>
#include <functional>

namespace skundo {

enum class Mode
{
  Undo,
  Redo
};

class UndoButton final : public IControl
{
public:
  UndoButton(const IRECT& bounds, Mode mode, std::function<void()> onClick)
  : IControl(bounds)
  , mMode(mode)
  , mOnClick(std::move(onClick))
  {
  }

  void Draw(IGraphics& g) override
  {
    const uint32_t depth = Depth();
    const bool on = depth != 0u;
    g.FillRoundRect(on ? skmint::kSurf : skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(on ? skmint::kAccent : skmint::kWell, mRECT, 4.f, 0, 1.25f);
    char label[16];
    if (on && depth > 1u)
      snprintf(label, sizeof label, "%s %u", mMode == Mode::Undo ? "UNDO" : "REDO", (unsigned) depth);
    else
      snprintf(label, sizeof label, "%s", mMode == Mode::Undo ? "UNDO" : "REDO");
    g.DrawText(IText(11.f, on ? skmint::kInk : skmint::kText, skmint::kFont, EAlign::Center,
                     EVAlign::Middle),
               label, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (Depth() != 0u && mOnClick)
      mOnClick();
    SetDirty(false);
  }

private:
  uint32_t Depth() const { return mMode == Mode::Undo ? sk_host_undo_depth() : sk_host_redo_depth(); }

  Mode mMode;
  std::function<void()> mOnClick;
};

} // namespace skundo
