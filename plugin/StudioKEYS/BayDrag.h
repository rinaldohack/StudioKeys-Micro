/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "IControls.h"
#include "ImportBay.h"
#include "SkMint.h"
#include <functional>

using namespace iplug;
using namespace igraphics;

/** Drag a bay row into the librarian band (plan phase 8). */
class BayDragControl final : public IControl
{
public:
  using StoreFn = std::function<void(size_t bayIndex)>;

  BayDragControl(const IRECT& bounds, ImportBay* bay, const IRECT& libDrop, StoreFn onStore)
  : IControl(bounds)
  , mBay(bay)
  , mLibDrop(libDrop)
  , mStore(std::move(onStore))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(skmint::kSurf, mRECT);
    if (!mBay || mBay->Rows().empty())
    {
      g.DrawText(IText(13.f, skmint::kInk, skmint::kFont), "Bay empty — load DX7/Virus", mRECT);
      return;
    }
    const size_t i = mBay->Selected();
    const auto& r = mBay->Rows()[i];
    WDL_String t;
    t.SetFormatted(96, "Drag → Lib: [%zu] %s (%s)", i, r.name.c_str(), r.engine.c_str());
    g.DrawText(IText(14.f, mDragging ? skmint::kAccent : skmint::kTheme, skmint::kFont), t.Get(), mRECT);
    if (mDragging)
      g.DrawRect(skmint::kAccent, mRECT, nullptr, 2.f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) mod;
    if (!mRECT.Contains(x, y) || !mBay || mBay->Rows().empty())
      return;
    mDragging = true;
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) dX;
    (void) dY;
    (void) mod;
    if (mDragging)
      SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    (void) mod;
    if (!mDragging)
      return;
    mDragging = false;
    if (mLibDrop.Contains(x, y) && mStore && mBay && !mBay->Rows().empty())
      mStore(mBay->Selected());
    SetDirty(false);
  }

private:
  ImportBay* mBay = nullptr;
  IRECT mLibDrop;
  StoreFn mStore;
  bool mDragging = false;
};
