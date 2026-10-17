/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

/* Patch window — the single modal opened from the bottom PATCH strip. One window,
 * three columns, tagged like the CFW:
 *   PRESETS  P01..Pnn  every engine's INIT patch + factory presets (ENGINE_ORDER)
 *   USER     U01..U100 the NOR user slots (the keyboard's 100; unlimited VST-only rows live in the bay)
 *   IMPORTS  I-FM01..  DX7 32-voice bulks (FM6)
 *            I-V01..   Access Virus singles (ANALOG)
 * Drag a P… or I-… row onto a U… row to store it as a user preset (the CFW's
 * SAVE > USER). User rows are a target only — dragging out of them does nothing
 * ("not in reverse").
 *
 * The window draws itself and handles its own mouse (no child controls), so it
 * is created once in LayoutUI and toggled. The desk is hidden while it is open,
 * exactly like the About modal (SetDeskHiddenForModal). */

#include "IControl.h"
#include "SkMint.h"
#include "ImportBay.h"
#include "ProgramFile.h"
#include "SplitZone.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace skoverlay {

namespace demo {

/* Synthetic DX7 32-voice bulk (F0 43 00 09 20 00 … F7) with distinct names and
 * algorithms, so the bay lists 32 rows. Mirrors tools/check_bay_bulk.py. */
inline const std::vector<uint8_t>& Dx7Bulk()
{
  static std::vector<uint8_t> bulk = [] {
    std::vector<uint8_t> out(6u + 128u * 32u + 2u, 0);
    out[0] = 0xF0; out[1] = 0x43; out[2] = 0x00; out[3] = 0x09; out[4] = 0x20; out[5] = 0x00;
    static const uint8_t kInit[128] = {
      99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0,
      0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99,
      99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0,
      2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 99, 2, 0, 99, 99, 99, 99, 50, 50, 50, 50,
      0, 8, 35, 0, 0, 0, 49, 24, 73, 78, 73, 84, 32, 86, 79, 73, 67, 69};
    for (int v = 0; v < 32; ++v)
    {
      uint8_t* dst = out.data() + 6 + v * 128;
      std::memcpy(dst, kInit, 128);
      dst[110] = (uint8_t) (v % 32);               /* algorithm */
      dst[111] = (uint8_t) ((v % 8) | ((v / 8) & 8));
      char nm[10];
      std::snprintf(nm, sizeof nm, "DEMO %02d", v + 1);
      std::memcpy(dst + 118, nm, 10);
      for (int i = 0; i < 128; ++i)
        dst[i] &= 0x7F;
    }
    unsigned sum = 0;
    for (size_t i = 6; i < 6u + 128u * 32u; ++i)
      sum += out[i];
    out[6u + 128u * 32u] = (uint8_t) ((128u - (sum & 0x7Fu)) & 0x7Fu);
    out[6u + 128u * 32u + 1u] = 0xF7;
    return out;
  }();
  return bulk;
}

/* Synthetic Access Virus classic single (F0 00 20 33 … F7) with 256-byte pages.
 * Mirrors tools/check_virus_bay.py make_classic_virus_demo(). */
inline const std::vector<uint8_t>& VirusSingle()
{
  static std::vector<uint8_t> v = [] {
    std::vector<uint8_t> out(9u + 256u + 2u, 0);
    static const uint8_t hdr[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x00, 0x40};
    std::memcpy(out.data(), hdr, 9);
    uint8_t* page = out.data() + 9;
    page[5] = 32;      /* A5 portamento → glide */
    page[17] = 64;     /* A17 osc1 shape (saw) */
    page[40] = 100;    /* A40 cutoff */
    page[42] = 40;     /* A42 resonance */
    page[94] = 1;      /* A94 key mode mono */
    std::memcpy(page + 128 + 112, "VIRUS DEMO", 10);
    unsigned sum = (unsigned) out[5] + 0x10u + out[7] + out[8];
    for (int i = 0; i < 256; ++i)
      sum += page[i];
    out[9u + 256u] = (uint8_t) (sum & 0x7Fu);
    out[9u + 256u + 1u] = 0xF7;
    return out;
  }();
  return v;
}

inline void LoadDx7Demo(ImportBay& bay)
{
  const auto& b = Dx7Bulk();
  bay.LoadDx7Bulk(b.data(), b.size());
}

inline void LoadVirusDemo(ImportBay& bay)
{
  const auto& v = VirusSingle();
  bay.LoadVirusTi(v.data(), v.size());
}

} // namespace demo

/* Shared modal-window chrome: scrim, framed window, title, Close button. */
class SkWindow : public IControl
{
public:
  using CloseFn = std::function<void()>;

  SkWindow(const IRECT& bounds, const char* title, CloseFn onClose)
  : IControl(bounds)
  , mTitle(title ? title : "")
  , mOnClose(std::move(onClose))
  {
    Hide(true);
  }

  bool IsOpen() const { return !IsHidden(); }

  void Open() { Hide(false); OnOpened(); SetDirty(false); }

  void CloseWin()
  {
    Hide(true);
    if (mOnClose)
      mOnClose();
    SetDirty(false);
  }

  void Refresh()
  {
    OnOpened();
    SetDirty(false);
  }

  void Draw(IGraphics& g) final
  {
    if (IsHidden())
      return;
    Layout();

    g.FillRect(IColor(210, 0, 0, 0), mRECT);                 /* scrim */
    g.FillRoundRect(skmint::kSurf, mWin, 10.f);
    g.DrawRoundRect(skmint::kTheme, mWin, 10.f, 0, 2.f);
    g.DrawText(IText(20.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Middle),
               mTitle.c_str(), mTitleRect);

    g.FillRoundRect(skmint::kWell, mClose, 5.f);
    g.DrawText(IText(14.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle), "Close", mClose);

    DrawBody(g);

    /* Drag ghost follows the cursor. */
    if (mDrag.active)
    {
      const IRECT g0(mDrag.x + 10.f, mDrag.y - 10.f, mDrag.x + 160.f, mDrag.y + 10.f);
      g.FillRoundRect(IColor(220, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), g0, 4.f);
      g.DrawText(IText(12.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 mDrag.label.c_str(), g0.GetPadded(-6.f));
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) final
  {
    if (IsHidden())
      return;
    Layout();

    if (!mWin.Contains(x, y) || mClose.Contains(x, y))
    {
      CloseWin();
      return;
    }
    /* P11: right-click opens a per-row context menu (export/import) in the windows that
     * support one; it must not close the window, and it must not arm a drag. */
    if (mod.R)
    {
      OnBodyRightDown(x, y);
      SetDirty(false);
      return;
    }
    OnBodyDown(x, y);
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) final
  {
    if (IsHidden() || !mDrag.armed)
      return;
    mDrag.x = x;
    mDrag.y = y;
    if (!mDrag.active && (std::abs(x - mDrag.ox) > 6.f || std::abs(y - mDrag.oy) > 6.f))
      mDrag.active = true;
    if (mDrag.active)
      SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod&) final
  {
    if (IsHidden())
      return;
    Layout();
    const bool wasDrag = mDrag.active;
    mDrag.active = false;
    mDrag.armed = false;
    OnBodyUp(x, y, wasDrag);
    SetDirty(false);
  }

  void OnMouseWheel(float x, float y, const IMouseMod&, float d) final
  {
    if (IsHidden())
      return;
    Layout();
    OnBodyWheel(x, y, d);
    SetDirty(false);
  }

protected:
  struct DragState
  {
    bool armed = false;   /* mouse down on a draggable row */
    bool active = false;  /* moved far enough to be a drag */
    int col = -1;
    int row = -1;
    float ox = 0.f, oy = 0.f, x = 0.f, y = 0.f;
    std::string label;
  };

  virtual float WinW() const { return 760.f; }
  virtual float WinH() const { return 560.f; }
  virtual void OnOpened() {}
  virtual void OnLayout(const IRECT& in) { mListClip = in; }
  virtual void DrawBody(IGraphics&) = 0;
  virtual void OnBodyDown(float, float) {}
  virtual void OnBodyRightDown(float, float) {}
  virtual void OnBodyUp(float, float, bool) {}
  virtual void OnBodyWheel(float, float, float) {}
  virtual int RowCount() const = 0;

  void Layout()
  {
    mWin = mRECT.GetCentredInside(std::min(WinW(), mRECT.W() - 60.f),
                                  std::min(WinH(), mRECT.H() - 60.f));
    const IRECT in = mWin.GetPadded(-16.f);
    mTitleRect = in.GetFromTop(30.f).GetReducedFromRight(90.f);
    mClose = in.GetFromTRHC(80.f, 26.f);
    mIn = in.GetReducedFromTop(38.f);
    OnLayout(mIn);
  }

  IRECT mWin, mTitleRect, mClose, mIn, mListClip;
  std::string mTitle;
  DragState mDrag;
  CloseFn mOnClose;
};

/* Shared list drawing used by the combined window and the librarian. */
namespace detail {

inline void DrawList(IGraphics& g, const IRECT& clip, float scroll, int count, float rowH,
                     const std::function<void(int, const IRECT&)>& drawRow)
{
  g.FillRoundRect(skmint::kWell, clip, 6.f);
  g.PathClipRegion(clip);
  for (int i = 0; i < count; ++i)
  {
    const float ry = clip.T + (float) i * rowH - scroll;
    if (ry + rowH < clip.T || ry > clip.B)
      continue;
    drawRow(i, IRECT(clip.L, ry, clip.R, ry + rowH));
  }
  g.PathClipRegion();
}

inline float ScrollFor(float scroll, float contentH, float viewH, float d)
{
  const float maxScroll = std::max(0.f, contentH - viewH);
  return std::max(0.f, std::min(maxScroll, scroll - d * 28.f));
}

} // namespace detail

/* ============================================================ COMBINED WINDOW
 * PATCH / BAY: three columns (PRESETS, USER, IMPORTS) with the CFW tags. Drag
 * a preset or import row onto a user row to store it; user rows never drag out. */
class PatchBayWindow final : public SkWindow
{
public:
  using LoadedFn = std::function<void()>;   /* a sound was loaded (desk swap) */
  using ChangedFn = std::function<void()>;  /* a bank mutation (list refresh) */

  PatchBayWindow(const IRECT& bounds, ImportBay* bay,
                 LoadedFn onLoaded, ChangedFn onChanged, CloseFn onClose)
  : SkWindow(bounds, "PROGRAM BAY · PRESETS · USER · IMPORTS", std::move(onClose))
  , mBay(bay)
  , mOnLoaded(std::move(onLoaded))
  , mOnChanged(std::move(onChanged))
  {
  }

protected:
  static constexpr float kRowH = 18.f;
  static constexpr float kHeaderH = 16.f;
  static constexpr float kColGap = 6.f;

  float WinW() const override { return 900.f; }
  float WinH() const override { return 600.f; }

  /* --- model ------------------------------------------------------------- */
  struct P { unsigned rank = 0, index = 0; bool init = false; bool sep = false; unsigned count = 0;
             std::string tag, name, engine; bool fav = false; };
  struct U { int slot = 0; bool used = false, fav = false; uint32_t chans = 0;
             std::string tag, name, engine; };
  struct B { int bay = 0; bool isFm = true; std::string tag, name, engine; };

  void OnOpened() override { Rebuild(); }

  void Rebuild()
  {
    /* P11: keep the focused row across a rebuild (the live sync rebuilds often). */
    const std::string keepTag = RowTag(mSelCol, mSelRow);
    const int keepCol = mSelCol;
    int keepRow = mSelRow;

    mPresets.clear();
    mUsers.clear();
    mBays.clear();

    unsigned pn = 0;
    for (unsigned rank = 0; rank < (unsigned) skdesk::kNumEngines; ++rank)
    {
      const char* en = sk_host_engine_name(rank);
      const std::string engName = en && en[0] ? en : "?";
      const unsigned n = sk_host_engine_factory_count(rank);
      if (n == 0)
        continue;
      /* P11: a per-engine separator so the list reads as engine families, not one flat run. */
      if (pn > 0)
      {
        P s;
        s.sep = true;
        s.rank = rank;
        s.count = n;
        s.engine = engName;
        s.name = engName;
        mPresets.push_back(std::move(s));
      }
      /* INIT row: the engine's defaults + first preset (TOOLS > INIT). */
      {
        P r;
        r.rank = rank;
        r.init = true;
        ++pn;
        char tag[8];
        std::snprintf(tag, sizeof tag, "P%02u", pn);
        r.tag = tag;
        r.name = "INIT";
        r.engine = engName;
        mPresets.push_back(std::move(r));
      }
      for (unsigned i = 0; i < n; ++i)
      {
        P r;
        r.rank = rank;
        r.index = i;
        ++pn;
        char tag[8];
        std::snprintf(tag, sizeof tag, "P%02u", pn);
        r.tag = tag;
        const char* nm = sk_host_engine_factory_name(rank, i);
        r.name = nm && nm[0] ? nm : "—";
        r.engine = engName;
        r.fav = sk_host_engine_factory_fav(rank, i) != 0;
        mPresets.push_back(std::move(r));
      }
    }

    /* P11: which mixer channels currently play each user slot (T1..T4 chips). The firmware's 100 slots
     * (core.h UP_SLOTS) are the ones the keyboard holds, so they are the ones this column mirrors. */
    const int nus = std::max(1, sk_host_user_slot_count());
    std::vector<uint32_t> chanOf((size_t) nus, 0u);
    for (uint32_t t = 0; t < 4; ++t)
    {
      const int s = sk_host_trk_user_slot(t);
      if (s >= 0 && s < nus)
        chanOf[(size_t) s] |= 1u << t;
    }

    for (int slot = 0; slot < nus; ++slot)
    {
      U r;
      r.slot = slot;
      r.used = sk_host_user_used((uint32_t) slot) != 0;
      char tag[8];
      std::snprintf(tag, sizeof tag, "U%02d", slot + 1);
      r.tag = tag;
      r.name = r.used ? sk_host_user_name((uint32_t) slot) : "— empty —";
      const char* en = r.used ? sk_host_user_engine_name((uint32_t) slot) : "";
      r.engine = en && en[0] ? en : "";
      r.fav = r.used && sk_host_user_fav((uint32_t) slot) != 0;
      r.chans = chanOf[(size_t) slot];
      mUsers.push_back(std::move(r));
    }

    if (mBay)
    {
      int fm = 0, va = 0;
      const auto& rows = mBay->Rows();
      for (size_t i = 0; i < rows.size(); ++i)
      {
        B r;
        r.bay = (int) i;
        r.isFm = rows[i].engine == "FM6";
        char tag[12];
        if (r.isFm)
          std::snprintf(tag, sizeof tag, "I-FM%02d", ++fm);
        else
          std::snprintf(tag, sizeof tag, "I-V%02d", ++va);
        r.tag = tag;
        r.name = rows[i].name;
        r.engine = rows[i].engine;
        mBays.push_back(std::move(r));
      }
    }

    mScroll[0] = mScroll[1] = mScroll[2] = 0.f;
    RestoreSelection(keepCol, keepTag, keepRow);
    mSig = LibSig();
  }

  /* P11: the USER column mirrors the CFW bank live. OnIdle calls this; a cheap hash of the used
   * slots decides whether a full rebuild is needed, so a save on the keyboard shows up here
   * without polling every row through the host every frame. */
public:
  void SyncFromHost()
  {
    if (IsHidden())
      return;
    if (LibSig() != mSig)
    {
      Rebuild();
      SetDirty(false);
    }
  }

protected:

  /* --- geometry ---------------------------------------------------------- */
  uint32_t LibSig() const
  {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    auto str = [&mix](const char* s) { for (const char* p = s; p && *p; ++p) mix((uint8_t) *p); };
    mix((uint32_t) (sk_host_favorites_filter() != 0));
    const int n = sk_host_user_slot_count();
    mix((uint32_t) n);
    for (int s = 0; s < n; ++s)
    {
      const int used = sk_host_user_used((uint32_t) s);
      mix((uint32_t) (used != 0));
      if (!used)
        continue;
      mix((uint32_t) sk_host_user_fav((uint32_t) s));
      str(sk_host_user_name((uint32_t) s));
      str(sk_host_user_engine_name((uint32_t) s));
    }
    for (uint32_t t = 0; t < 4; ++t)
      mix((uint32_t) (sk_host_trk_user_slot(t) + 1));
    return h;
  }

  void RestoreSelection(int col, const std::string& tag, int fallbackRow)
  {
    mSelCol = col;
    mSelRow = fallbackRow;
    if (!tag.empty())
    {
      const int n = ColCount(col);
      for (int i = 0; i < n; ++i)
        if (RowTag(col, i) == tag)
        {
          mSelRow = i;
          break;
        }
    }
    mSelRow = std::max(0, std::min(mSelRow, std::max(0, ColCount(mSelCol) - 1)));
  }

  int ColAt(float x) const
  {
    for (int c = 0; c < 3; ++c)
      if (mCol[c].Contains(x, mCol[c].T))
        return c;
    return -1;
  }

  int RowAt(int col, float y) const
  {
    if (col < 0 || col > 2)
      return -1;
    const int n = ColCount(col);
    const int row = (int) ((y - mColList[col].T + mScroll[col]) / kRowH);
    return (row >= 0 && row < n) ? row : -1;
  }

  int ColCount(int col) const
  {
    return col == 0 ? (int) mPresets.size() : col == 1 ? (int) mUsers.size() : (int) mBays.size();
  }

  void OnLayout(const IRECT& in) override
  {
    mTop = in.GetFromTop(20.f);
    mBtnRow = in.GetFromBottom(34.f);
    const IRECT cols = in.GetReducedFromTop(24.f).GetReducedFromBottom(40.f);
    const float w = (cols.W() - 2.f * kColGap) / 3.f;
    for (int c = 0; c < 3; ++c)
    {
      const float l = cols.L + (w + kColGap) * c;
      mCol[c] = IRECT(l, cols.T, l + w, cols.B);
      mColHdr[c] = mCol[c].GetFromTop(kHeaderH);
      mColList[c] = mCol[c].GetReducedFromTop(kHeaderH + 2.f);
    }
    mListClip = mColList[0];
  }

  int RowCount() const override { return std::max({ColCount(0), ColCount(1), ColCount(2)}); }

  /* --- draw -------------------------------------------------------------- */
  void DrawBody(IGraphics& g) override
  {
    const bool fav = sk_host_favorites_filter() != 0;
    mFavBtn = mTop.GetFromLeft(150.f).GetPadded(-1.f);
    g.FillRoundRect(skmint::kWell, mFavBtn, 4.f);
    g.DrawRoundRect(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), mFavBtn, 4.f, 0, 1.f);
    g.DrawText(IText(12.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle),
               fav ? "PRESETS: FAVORITES" : "PRESETS: ALL", mFavBtn);
    g.DrawText(IText(11.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle),
               "drag P⋯ / I-⋯ onto a U⋯ slot to save · double-click to load",
               mTop.GetReducedFromLeft(160.f));

    static const char* kHdr[3] = {"PRESETS", "USER", "IMPORTS"};
    static const char* kSub[3] = {"Pxx  INIT + factory", "Uxx  NOR slots", "I-FMxx / I-Vxx"};
    for (int c = 0; c < 3; ++c)
    {
      g.FillRoundRect(skmint::kTheme, mColHdr[c], 4.f);
      g.DrawText(IText(12.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 kHdr[c], mColHdr[c].GetPadded(-6.f));
      g.DrawText(IText(10.f, skmint::kInk, skmint::kFont, EAlign::Far, EVAlign::Middle),
                 kSub[c], mColHdr[c].GetPadded(-6.f));
    }

    if (fav)
      DrawCol(g, 0, true);
    else
      DrawCol(g, 0, false);
    DrawCol(g, 1, false);
    DrawCol(g, 2, false);

    DrawButtons(g, mBtnRow, {"Load to Synth", "Save to User", "Load DX7/Virus TI"});
    DrawCtx(g);
  }

  /* --- context menu (P11) ------------------------------------------------ */
  enum CtxAction
  {
    kCtxExportSkp = 0,
    kCtxExportDx7,
    kCtxExportVirus,
    kCtxExportBank,
    kCtxImport,
  };

  struct CtxItem
  {
    std::string label;
    bool enabled = false;
    int action = kCtxExportSkp;
  };

  void DrawCtx(IGraphics& g)
  {
    if (!mCtxOpen)
      return;
    const float h = (float) mCtxItems.size() * kCtxRowH;
    float l = mCtxX, t = mCtxY;
    if (l + kCtxW > mIn.R)
      l = mIn.R - kCtxW;
    if (t + h > mIn.B)
      t = mIn.B - h;
    l = std::max(l, mIn.L);
    t = std::max(t, mIn.T);
    mCtxBox = IRECT(l, t, l + kCtxW, t + h);
    g.FillRoundRect(IColor(245, skmint::kBg.R, skmint::kBg.G, skmint::kBg.B), mCtxBox, 5.f);
    g.DrawRoundRect(skmint::kTheme, mCtxBox, 5.f, 0, 1.f);
    mCtxHits.clear();
    for (size_t i = 0; i < mCtxItems.size(); ++i)
    {
      const IRECT r(mCtxBox.L, mCtxBox.T + (float) i * kCtxRowH, mCtxBox.R,
                    mCtxBox.T + (float) (i + 1) * kCtxRowH);
      mCtxHits.push_back(r);
      const CtxItem& it = mCtxItems[i];
      if (!it.enabled)
        continue;
      g.DrawText(IText(11.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 it.label.c_str(), r.GetPadded(-8.f));
    }
  }

  void OnBodyRightDown(float x, float y) override
  {
    const int c = ColAt(x);
    const int r = c >= 0 ? RowAt(c, y) : -1;
    if (c < 0)
    {
      mCtxOpen = false;
      return;
    }
    if (r >= 0)
    {
      if (c == 0 && mPresets[(size_t) r].sep)
      {
        mCtxOpen = false;
        return;
      }
      mSelCol = c;
      mSelRow = r;
    }
    mCtxX = x;
    mCtxY = y;
    mCtxOpen = true;
    BuildCtx();
  }

  void BuildCtx()
  {
    const bool row = RowValid(mSelCol, mSelRow);
    const bool bayFm = row && mSelCol == 2 && mBays[(size_t) mSelRow].isFm;
    const bool bayVa = row && mSelCol == 2 && !mBays[(size_t) mSelRow].isFm;
    const bool program = row && mSelCol != 2; /* bay rows carry a dump, not 91 params */

    mCtxItems.clear();
    mCtxItems.push_back({"Export program · StudioKEYS (.skp)", program, kCtxExportSkp});
    mCtxItems.push_back({"Export program · DX7 (.syx)", bayFm, kCtxExportDx7});
    mCtxItems.push_back({"Export program · Virus TI (.syx)", bayVa, kCtxExportVirus});
    mCtxItems.push_back({"Export USER bank · StudioKEYS (.skp)", AnyUserSlot(), kCtxExportBank});
    mCtxItems.push_back({"Import .skp / .syx…", mBay != nullptr, kCtxImport});
  }

  bool RowValid(int col, int row) const
  {
    if (row < 0)
      return false;
    if (col == 0)
      return row < (int) mPresets.size() && !mPresets[(size_t) row].sep;
    if (col == 1)
      return row < (int) mUsers.size();
    return row < (int) mBays.size();
  }

  bool AnyUserSlot() const
  {
    for (const auto& u : mUsers)
      if (u.used)
        return true;
    return false;
  }

  bool FocusedProgram(skpg::Program& out)
  {
    if (!RowValid(mSelCol, mSelRow))
      return false;
    out = skpg::Program {};
    if (mSelCol == 1)
    {
      const U& u = mUsers[(size_t) mSelRow];
      if (!u.used)
        return false;
      int16_t v[skpg::kMaxParams] {};
      const int n = sk_host_user_values((uint32_t) u.slot, v, (uint32_t) skpg::kMaxParams);
      if (n <= 0)
        return false;
      out.engine = u.engine;
      out.name = u.name;
      out.values.assign(v, v + n);
      return true;
    }
    /* Preset: apply it (the same thing LOAD does), then capture the track's parameters. The
     * selected track therefore ends up playing the exported sound. */
    const P& p = mPresets[(size_t) mSelRow];
    const bool ok = p.init ? sk_host_engine_init(p.rank) != 0
                           : sk_host_engine_factory_apply(p.rank, p.index) != 0;
    if (!ok)
      return false;
    if (mOnLoaded)
      mOnLoaded();
    const int n = sk_host_param_count();
    for (int i = 0; i < n; ++i)
      out.values.push_back((int16_t) sk_host_get_param((uint32_t) i));
    out.engine = p.engine;
    out.name = p.name;
    return !out.values.empty();
  }

  void ExecCtx(int action)
  {
    mCtxOpen = false;
    switch (action)
    {
      case kCtxExportSkp: ExportProgramSkp(); break;
      case kCtxExportDx7: ExportBayDump(true); break;
      case kCtxExportVirus: ExportBayDump(false); break;
      case kCtxExportBank: ExportBankSkp(); break;
      case kCtxImport: ImportFromFile(true); break;
      default: break;
    }
    SetDirty(false);
  }

  std::string SavePath(const std::string& suggested, const char* ext)
  {
    if (!GetUI())
      return {};
    WDL_String fileName, path;
    fileName.Set(suggested.c_str());
    GetUI()->PromptForFile(fileName, path, EFileAction::Save, ext);
    /* Save leaves the chosen path in `path` on macOS; some hosts fill `fileName`. */
    if (path.GetLength())
      return std::string(path.Get());
    return {};
  }

  void ExportProgramSkp()
  {
    skpg::Program p;
    if (!FocusedProgram(p))
      return;
    const std::string path = SavePath((p.name.empty() ? std::string("program") : p.name) + ".skp",
                                      "skp");
    if (path.empty())
      return;
    skpg::WriteFile(path, {p});
  }

  void ExportBankSkp()
  {
    std::vector<skpg::Program> list;
    for (const auto& u : mUsers)
    {
      if (!u.used || u.engine == "FM6")
        continue; /* FM6 slots leave as a DX7 bulk; their params alone are not the sound */
      int16_t v[skpg::kMaxParams] {};
      const int n = sk_host_user_values((uint32_t) u.slot, v, (uint32_t) skpg::kMaxParams);
      if (n <= 0)
        continue;
      skpg::Program p;
      p.engine = u.engine;
      p.name = u.name;
      p.values.assign(v, v + n);
      list.push_back(std::move(p));
    }
    if (list.empty())
      return;
    const std::string path = SavePath("studiokeys-user-bank.skp", "skp");
    if (path.empty())
      return;
    skpg::WriteFile(path, list);
  }

  void ExportBayDump(bool dx7)
  {
    if (!mBay || mSelCol != 2 || !RowValid(2, mSelRow))
      return;
    const int bayIdx = mBays[(size_t) mSelRow].bay;
    if (bayIdx < 0 || (size_t) bayIdx >= mBay->Rows().size())
      return;
    const BayRow& r = mBay->Rows()[(size_t) bayIdx];
    std::vector<uint8_t> data;
    if (dx7)
      data = mBay->ExportDx7Voice(mBay->StoreRow((size_t) bayIdx, LibKind::HardwareReady));
    else
      data = mBay->ExportVirus(mBay->StoreRow((size_t) bayIdx, LibKind::VstOnly));
    if (data.empty())
      return;
    const std::string path = SavePath((r.name.empty() ? std::string("program") : r.name) + ".syx",
                                      "syx");
    if (path.empty())
      return;
    std::ofstream f(path, std::ios::binary);
    if (f)
      f.write((const char*) data.data(), (std::streamsize) data.size());
  }


  void DrawCol(IGraphics& g, int c, bool /*fav*/)
  {
    const int n = ColCount(c);
    const IRECT clip = mColList[c];
    detail::DrawList(g, clip, mScroll[c], n, kRowH, [&](int i, const IRECT& r) {
      /* P11: engine separator band in the PRESETS column (no tag, not selectable). */
      if (c == 0 && mPresets[(size_t) i].sep)
      {
        const P& s = mPresets[(size_t) i];
        g.FillRect(skmint::kBg, r);
        g.FillRect(IColor(90, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                   IRECT(r.L, r.B - 1.f, r.R, r.B));
        char lbl[32];
        std::snprintf(lbl, sizeof lbl, "%s  (%u)", s.engine.c_str(), s.count);
        g.DrawText(IText(10.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Middle), lbl,
                   r.GetPadded(-4.f));
        return;
      }

      const bool sel = (mSelCol == c && mSelRow == i);
      /* P11: alternating row stripes so the list reads as a list; the selected row
       * still wins with the theme tint. */
      if (!sel)
        g.FillRect((i & 1) ? skmint::kWell : skmint::kBg, r);
      if (sel)
        g.FillRect(IColor(60, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), r);

      std::string tag, name, eng;
      bool fav2 = false, dim = false, cur = false;
      uint32_t chans = 0;
      if (c == 0)
      {
        const P& p = mPresets[(size_t) i];
        tag = p.tag; name = p.name; eng = p.engine; fav2 = p.fav;
        cur = !p.init && sk_host_sel_user_slot() < 0 && sk_host_sel_engine_rank() == p.rank
              && sk_host_sel_preset() == (int) sk_host_engine_factory_preset(p.rank, p.index);
      }
      else if (c == 1)
      {
        const U& u = mUsers[(size_t) i];
        tag = u.tag; name = u.name; eng = u.engine; fav2 = u.fav;
        dim = !u.used;
        cur = sk_host_sel_user_slot() == u.slot;
        chans = u.chans;
      }
      else
      {
        const B& b = mBays[(size_t) i];
        tag = b.tag; name = b.name; eng = b.engine;
      }

      const IColor tc = sel ? skmint::InkOn(skmint::kTheme) : (dim ? skmint::kWell : skmint::kTheme);
      const IColor nc = sel ? skmint::InkOn(skmint::kTheme) : (dim ? skmint::kWell : skmint::kText);
      const IRECT inner = r.GetPadded(-4.f);
      const IRECT tagR = inner.GetFromLeft(48.f);
      const IRECT engR = inner.GetFromRight(46.f);
      /* P11: mixer-channel chips (T1..T4) between the name and the engine column. */
      const IRECT chipR = IRECT(engR.L - 34.f, inner.T, engR.L - 2.f, inner.B);
      const std::string tagTxt = std::string(cur ? "*" : "") + tag;
      const std::string engTxt = std::string(fav2 ? "*" : "") + eng;
      g.DrawText(IText(11.f, cur ? skmint::kAccent : tc, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 tagTxt.c_str(), tagR);
      g.DrawText(IText(11.f, nc, skmint::kFont, EAlign::Near, EVAlign::Middle), name.c_str(),
                 inner.GetReducedFromLeft(50.f).GetReducedFromRight(84.f));
      g.DrawText(IText(10.f, sel ? skmint::kInk : skmint::kTheme, skmint::kFont, EAlign::Far, EVAlign::Middle),
                 engTxt.c_str(), engR);
      if (chans)
      {
        const float cy = chipR.MH();
        for (uint32_t t = 0; t < 4; ++t)
        {
          if (!(chans & (1u << t)))
            continue;
          const float cx = chipR.L + 8.f * (float) t;
          g.FillRoundRect(skmint::kTrack[t], IRECT(cx, cy - 3.f, cx + 6.f, cy + 3.f), 1.5f);
        }
      }
    });

    if (n == 0)
      g.DrawText(IText(11.f, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 c == 0 ? "No presets" : c == 1 ? "No slots" : "Bay empty", clip);
  }

  void DrawButtons(IGraphics& g, const IRECT& row, const std::vector<std::string>& labels)
  {
    mBtnHits.clear();
    const int nb = (int) labels.size();
    const float bw = row.W() / (float) nb;
    for (int i = 0; i < nb; ++i)
    {
      const IRECT p = IRECT(row.L + bw * i, row.T, row.L + bw * (i + 1), row.B).GetPadded(-2.f);
      mBtnHits.push_back(p);
      g.FillRoundRect(skmint::kWell, p, 4.f);
      g.DrawRoundRect(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), p, 4.f, 0, 1.f);
      g.DrawText(IText(11.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 labels[(size_t) i].c_str(), p);
    }
  }

  int HitButton(float x, float y) const
  {
    for (size_t i = 0; i < mBtnHits.size(); ++i)
      if (mBtnHits[i].Contains(x, y))
        return (int) i;
    return -1;
  }

  /* --- interaction ------------------------------------------------------- */
  void OnBodyDown(float x, float y) override
  {
    /* P11: an open context menu swallows the next click (item, or dismiss). */
    if (mCtxOpen)
    {
      for (size_t i = 0; i < mCtxHits.size(); ++i)
        if (mCtxHits[i].Contains(x, y))
        {
          const int action = i < mCtxItems.size() ? mCtxItems[i].action : -1;
          if (action >= 0 && mCtxItems[i].enabled)
            ExecCtx(action);
          mCtxOpen = false;
          SetDirty(false);
          return;
        }
      mCtxOpen = false;
      SetDirty(false);
      return;
    }

    if (mFavBtn.Contains(x, y))
    {
      sk_host_set_favorites_filter(sk_host_favorites_filter() ? 0 : 1);
      Rebuild();
      mSelCol = 0;
      mSelRow = 0;
      SetDirty(false);
      return;
    }

    if (HitButton(x, y) >= 0)
    {
      OnBodyButtons(x, y);
      return;
    }

    const int c = ColAt(x);
    const int r = RowAt(c, y);
    if (c >= 0 && r >= 0)
    {
      /* P11: engine separator bands are labels, not rows. */
      if (c == 0 && mPresets[(size_t) r].sep)
        return;
      mSelCol = c;
      mSelRow = r;
      /* Only PRESETS and IMPORTS rows can be dragged (user rows are targets). */
      if (c == 0 || c == 2)
      {
        mDrag.armed = true;
        mDrag.active = false;
        mDrag.col = c;
        mDrag.row = r;
        mDrag.ox = x;
        mDrag.oy = y;
        mDrag.x = x;
        mDrag.y = y;
        mDrag.label = RowTag(c, r);
      }
      SetDirty(false);
    }
  }

  void OnBodyUp(float x, float y, bool wasDrag) override
  {
    if (wasDrag)
    {
      /* Drop target is the USER column only; dropping a U… row is a no-op. */
      const int c = ColAt(x);
      if (c == 1)
      {
        const int target = RowAt(1, y);
        if (target >= 0 && target < (int) mUsers.size())
          CopyToUserSlot(mDrag.col, mDrag.row, target);
      }
      SetDirty(false);
      return;
    }

    /* Plain click: double-click loads the focused row. */
    const uint32_t now = sk_host_now_ms();
    if (mSelCol == mLastCol && mSelRow == mLastRow && now - mLastClickMs < 350u)
    {
      LoadFocused();
      mLastRow = -1;
    }
    else
    {
      mLastCol = mSelCol;
      mLastRow = mSelRow;
      mLastClickMs = now;
    }
  }

  void OnBodyWheel(float x, float /*y*/, float d) override
  {
    const int c = ColAt(x);
    if (c < 0)
      return;
    mScroll[c] = detail::ScrollFor(mScroll[c], (float) ColCount(c) * kRowH, mColList[c].H(), d);
  }

  std::string RowTag(int col, int row) const
  {
    if (col == 0 && row < (int) mPresets.size())
      return mPresets[(size_t) row].tag;
    if (col == 1 && row < (int) mUsers.size())
      return mUsers[(size_t) row].tag;
    if (col == 2 && row < (int) mBays.size())
      return mBays[(size_t) row].tag;
    return "";
  }

  void LoadFocused()
  {
    bool ok = false;
    if (mSelCol == 0 && mSelRow < (int) mPresets.size())
    {
      const P& p = mPresets[(size_t) mSelRow];
      if (!p.sep)
        ok = p.init ? sk_host_engine_init(p.rank) != 0
                    : sk_host_engine_factory_apply(p.rank, p.index) != 0;
    }
    else if (mSelCol == 1 && mSelRow < (int) mUsers.size())
    {
      const U& u = mUsers[(size_t) mSelRow];
      ok = u.used && sk_host_user_apply((uint32_t) u.slot) != 0;
    }
    else if (mSelCol == 2 && mSelRow < (int) mBays.size())
    {
      ok = LoadBayIntoHost(mBays[(size_t) mSelRow].bay);
    }
    if (ok && mOnLoaded)
      mOnLoaded();
  }

  bool LoadBayIntoHost(int bayIdx)
  {
    if (!mBay || bayIdx < 0 || (size_t) bayIdx >= mBay->Rows().size())
      return false;
    const BayRow& r = mBay->Rows()[(size_t) bayIdx];
    if (r.engine == "FM6")
      return sk_host_load_fm6_packed(r.converted.data(), (uint32_t) r.converted.size()) != 0;
    int16_t an[12] = {};
    if (!AnalogBlobUnpack(r.converted, an))
      return false;
    return sk_host_load_analog_values(an, 12) != 0;
  }

  /* Drag P…/I-… onto a U… row: write that sound into the user slot. */
  void CopyToUserSlot(int srcCol, int srcRow, int userRow)
  {
    if (userRow < 0 || userRow >= (int) mUsers.size())
      return;
    const int slot = mUsers[(size_t) userRow].slot;
    bool ok = false;
    if (srcCol == 0 && srcRow < (int) mPresets.size())
    {
      const P& p = mPresets[(size_t) srcRow];
      if (p.sep)
        return;
      if (p.init ? sk_host_engine_init(p.rank) : sk_host_engine_factory_apply(p.rank, p.index))
      {
        ok = sk_host_user_save((uint32_t) slot) != 0;
        if (mOnLoaded)
          mOnLoaded(); /* the source sound is now on the track: swap the desk */
      }
    }
    else if (srcCol == 2 && srcRow < (int) mBays.size())
    {
      if (!mBay)
        return;
      const int bayIdx = mBays[(size_t) srcRow].bay;
      if (bayIdx < 0 || (size_t) bayIdx >= mBay->Rows().size())
        return;
      const BayRow& r = mBay->Rows()[(size_t) bayIdx];
      if (r.engine == "FM6")
      {
        ok = sk_host_user_save_sound((uint32_t) slot, 1, r.converted.data(), nullptr,
                                     (uint32_t) r.converted.size(), r.name.c_str()) != 0;
      }
      else
      {
        int16_t an[12] = {};
        AnalogBlobUnpack(r.converted, an);
        ok = sk_host_user_save_sound((uint32_t) slot, 2, nullptr, an, 12, r.name.c_str()) != 0;
      }
      if (ok && mOnLoaded)
        mOnLoaded();
    }

    if (ok)
    {
      Rebuild();
      if (mOnChanged)
        mOnChanged();
      mSelCol = 1;
      mSelRow = userRow;
      SetDirty(false);
    }
  }

  /* Toolbar: 0 Load to Synth, 1 Save to User, 2 Load DX7/Virus TI (auto-detect).
   * Close is drawn by SkWindow. */
  void OnBodyButtons(float x, float y)
  {
    switch (HitButton(x, y))
    {
      case 0: LoadFocused(); break;
      case 1: SaveFocusedToUser(); break;
      case 2: ImportFromFile(true); break;   /* auto-detect: DX7 first, then Virus */
      default: break;
    }
    SetDirty(false);
  }

  int FirstFreeUserSlot() const
  {
    for (const auto& u : mUsers)
      if (!u.used)
        return u.slot;
    return 0;
  }

  void SaveFocusedToUser()
  {
    int slot = -1;
    if (mSelCol == 1 && mSelRow < (int) mUsers.size() && !mUsers[(size_t) mSelRow].used)
      slot = mUsers[(size_t) mSelRow].slot;
    else
      slot = FirstFreeUserSlot();

    bool ok = false;
    if (mSelCol == 0 && mSelRow < (int) mPresets.size())
    {
      const P& p = mPresets[(size_t) mSelRow];
      if (p.sep)
        return;
      if (p.init ? sk_host_engine_init(p.rank) : sk_host_engine_factory_apply(p.rank, p.index))
      {
        ok = sk_host_user_save((uint32_t) slot) != 0;
        if (mOnLoaded)
          mOnLoaded();
      }
    }
    else if (mSelCol == 2 && mSelRow < (int) mBays.size() && mBay)
    {
      const int bayIdx = mBays[(size_t) mSelRow].bay;
      if (bayIdx >= 0 && (size_t) bayIdx < mBay->Rows().size())
      {
        const BayRow& r = mBay->Rows()[(size_t) bayIdx];
        if (r.engine == "FM6")
          ok = sk_host_user_save_sound((uint32_t) slot, 1, r.converted.data(), nullptr,
                                       (uint32_t) r.converted.size(), r.name.c_str()) != 0;
        else
        {
          int16_t an[12] = {};
          AnalogBlobUnpack(r.converted, an);
          ok = sk_host_user_save_sound((uint32_t) slot, 2, nullptr, an, 12, r.name.c_str()) != 0;
        }
      }
    }
    if (ok)
    {
      Rebuild();
      if (mOnChanged)
        mOnChanged();
    }
  }

  /* Load a program file: .skp (StudioKEYS, any engine) or .syx (DX7 bulk / Virus single).
   * isDx7 picks which SysEx parser to try first. */
  void ImportFromFile(bool isDx7)
  {
    if (!mBay || !GetUI())
      return;
    WDL_String fileName, path;
    GetUI()->PromptForFile(fileName, path, EFileAction::Open, "syx skp");
    if (path.GetLength() == 0)
      return;
    std::vector<uint8_t> data;
    if (!skpg::ReadBytes(path.Get(), data))
      return;
    if (skpg::LooksLikeSkpg(data))
    {
      ImportSkpg(std::string((const char*) data.data(), data.size()));
      return;
    }
    bool ok = false;
    if (isDx7)
      ok = mBay->LoadDx7Bulk(data.data(), data.size()) || mBay->LoadVirusTi(data.data(), data.size());
    else
      ok = mBay->LoadVirusTi(data.data(), data.size()) || mBay->LoadDx7Bulk(data.data(), data.size());
    if (!ok)
      return;
    Rebuild();
    mSelCol = 2;
    mSelRow = (int) mBays.size() - 1;
    SetDirty(false);
  }

  /* P11: .skp -> the first free user slots, in file order. FM6 programs are skipped: their
   * voice lives beside the record and arrives as a DX7 .syx instead. */
  void ImportSkpg(const std::string& text)
  {
    std::vector<skpg::Program> list;
    if (!skpg::Parse(text, list) || list.empty())
      return;
    int slot = FirstFreeUserSlot();
    const int nus = std::max(1, sk_host_user_slot_count());
    int first = -1;
    int written = 0;
    for (const auto& p : list)
    {
      while (slot < nus && sk_host_user_used((uint32_t) slot))
        ++slot;
      if (slot >= nus)
        break;
      if (p.engine == "FM6" || p.values.empty())
        continue;
      if (!sk_host_user_save_values((uint32_t) slot, p.engine.c_str(), p.values.data(),
                                    (uint32_t) p.values.size(), p.name.c_str()))
        continue;
      if (first < 0)
        first = slot;
      ++written;
      ++slot;
    }
    if (!written)
      return;
    Rebuild();
    if (first >= 0 && first < (int) mUsers.size())
    {
      mSelCol = 1;
      mSelRow = first;
    }
    if (mOnChanged)
      mOnChanged();
    SetDirty(false);
  }

  /* Button clicks are dispatched from OnBodyDown. */

private:
  ImportBay* mBay = nullptr;
  LoadedFn mOnLoaded;
  ChangedFn mOnChanged;

  std::vector<P> mPresets;
  std::vector<U> mUsers;
  std::vector<B> mBays;

  IRECT mTop, mBtnRow, mFavBtn;
  IRECT mCol[3], mColHdr[3], mColList[3];
  float mScroll[3] {0.f, 0.f, 0.f};
  int mSelCol = 0, mSelRow = 0;
  int mLastCol = -1, mLastRow = -1;
  uint32_t mLastClickMs = 0;
  uint32_t mSig = 0;   /* P11: hash of the user bank the lists were built from */
  std::vector<IRECT> mBtnHits;

  /* P11 context menu (right-click export/import). */
  static constexpr float kCtxW = 268.f;
  static constexpr float kCtxRowH = 22.f;
  bool mCtxOpen = false;
  float mCtxX = 0.f, mCtxY = 0.f;
  IRECT mCtxBox;
  std::vector<CtxItem> mCtxItems;
  std::vector<IRECT> mCtxHits;
};

/** P12 — the LINK window: pick the MIDI port the companion uses to reach the CFW, see whether
 *  the link is up, and choose which side wins the first sync. One self-drawing window (no child
 *  controls), so it is always drawn above the desk and cannot be buried by a desk rebuild. */
class LinkWindow final : public SkWindow
{
public:
  struct Hooks
  {
    std::function<std::vector<std::string>()> ports;
    std::function<std::string()> current;
    std::function<void(const std::string&)> connect; /* "" = name-based auto-detect */
    std::function<void()> disconnect;
    std::function<void()> rescan;
    std::function<void()> pluginToHw;
    std::function<void()> hwToPlugin;
    std::function<std::string()> status;
    std::function<bool()> linked;
  };

  LinkWindow(const IRECT& bounds, Hooks hooks, CloseFn onClose)
  : SkWindow(bounds, "LINK · MIDI PORT & SYNC", std::move(onClose))
  , mH(std::move(hooks))
  {
  }

  /** Re-read the endpoints (New device plugged in, or the window just opened). */
  void Reload()
  {
    mPorts = mH.ports ? mH.ports() : std::vector<std::string> {};
    mIndex = 0;
    const std::string cur = mH.current ? mH.current() : std::string();
    for (size_t i = 0; i < mPorts.size(); ++i)
      if (mPorts[i] == cur)
      {
        mIndex = (int) i;
        break;
      }
    SetDirty(false);
  }

protected:
  float WinW() const override { return 600.f; }
  float WinH() const override { return 320.f; }
  int RowCount() const override { return 0; }

  void OnOpened() override { Reload(); }

  void OnLayout(const IRECT& in) override
  {
    mPortRow = in.GetFromTop(30.f);
    mPrev = mPortRow.GetFromLeft(34.f).GetCentredInside(30.f);
    mNext = mPortRow.GetFromRight(34.f).GetCentredInside(30.f);
    mRescan = mPortRow.GetFromRight(104.f).GetFromLeft(82.f).GetCentredInside(78.f, 26.f);
    mPortName = IRECT(mPrev.R + 6.f, mPortRow.T, mRescan.L - 6.f, mPortRow.B);
    mStatus = in.GetReducedFromTop(38.f).GetFromTop(46.f);
    const IRECT row = in.GetFromBottom(38.f);
    mBtn[0] = row.GetGridCell(0, 1, 3).GetPadded(-5.f);
    mBtn[1] = row.GetGridCell(1, 1, 3).GetPadded(-5.f);
    mBtn[2] = row.GetGridCell(2, 1, 3).GetPadded(-5.f);
  }

  void DrawBody(IGraphics& g) override
  {
    const bool up = mH.linked && mH.linked();

    g.DrawText(IText(12.f, IColor(170, skmint::kText.R, skmint::kText.G, skmint::kText.B),
                     skmint::kFont, EAlign::Near, EVAlign::Middle),
               "MIDI PORT", IRECT(mPortRow.L, mPortRow.T, mPortRow.L + 76.f, mPortRow.B));

    auto drawBtn = [&](const IRECT& r, const char* label, bool hot, bool enabled = true) {
      g.FillRoundRect(!enabled ? skmint::kBg : hot ? skmint::kTheme : skmint::kWell, r, 4.f);
      g.DrawRoundRect(skmint::kTheme, r, 4.f, 0, 1.f);
      g.DrawText(IText(13.f, !enabled ? IColor(120, skmint::kText.R, skmint::kText.G, skmint::kText.B)
                                     : hot ? skmint::InkOn(skmint::kTheme) : skmint::kInk,
                       skmint::kFont, EAlign::Center, EVAlign::Middle),
                 label, r);
    };

    /* Port selector: <  name  >  RESCAN */
    drawBtn(mPrev, "<", false, mPorts.size() > 1);
    drawBtn(mNext, ">", false, mPorts.size() > 1);
    const std::string name = mPorts.empty()
                               ? (up ? mH.current() : std::string("(no MIDI ports)"))
                               : mPorts[(size_t) std::clamp(mIndex, 0, (int) mPorts.size() - 1)];
    g.FillRoundRect(skmint::kWell, mPortName, 4.f);
    g.DrawRoundRect(skmint::kTheme, mPortName, 4.f, 0, 1.f);
    g.DrawText(IText(13.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle), name.c_str(),
               mPortName.GetPadded(-6.f));
    drawBtn(mRescan, "RESCAN", false);

    /* Status: connected endpoint, the engine's error, or "not linked yet". */
    g.FillRoundRect(skmint::kBg, mStatus, 4.f);
    g.DrawRoundRect(up ? skmint::kTheme : skmint::kWell, mStatus, 4.f, 0, 1.f);
    char line[256];
    snprintf(line, sizeof line, "%s", mH.status ? mH.status().c_str() : "");
    const IText st(13.f, up ? skmint::kTheme : skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle);
    g.DrawText(st, "STATUS", mStatus.GetPadded(-8.f).GetFromTop(14.f));
    g.DrawText(st, line, mStatus.GetPadded(-8.f).GetReducedFromTop(14.f));

    /* The three actions. The arrows need a live link; the first button connects it. */
    drawBtn(mBtn[0], up ? "Disconnect" : "Connect", !up);
    drawBtn(mBtn[1], "Plugin → Keyboard", false, up);
    drawBtn(mBtn[2], "Keyboard → Plugin", false, up);
  }

  void OnBodyDown(float x, float y) override
  {
    Layout();
    const bool up = mH.linked && mH.linked();
    if (mPrev.Contains(x, y) && !mPorts.empty())
    {
      mIndex = (mIndex + (int) mPorts.size() - 1) % (int) mPorts.size();
      if (mH.connect)
        mH.connect(mPorts[(size_t) mIndex]);
      SetDirty(false);
      return;
    }
    if (mNext.Contains(x, y) && !mPorts.empty())
    {
      mIndex = (mIndex + 1) % (int) mPorts.size();
      if (mH.connect)
        mH.connect(mPorts[(size_t) mIndex]);
      SetDirty(false);
      return;
    }
    if (mRescan.Contains(x, y))
    {
      if (mH.rescan)
        mH.rescan();
      Reload();
      return;
    }
    if (mBtn[0].Contains(x, y))
    {
      if (up)
      {
        if (mH.disconnect)
          mH.disconnect();
      }
      else
      {
        const std::string pick = mPorts.empty() ? std::string()
                              : mPorts[(size_t) std::clamp(mIndex, 0, (int) mPorts.size() - 1)];
        if (mH.connect)
          mH.connect(pick);
      }
      SetDirty(false);
      return;
    }
    if (!up)
      return;
    if (mBtn[1].Contains(x, y))
    {
      if (mH.pluginToHw)
        mH.pluginToHw();
      CloseWin();
      return;
    }
    if (mBtn[2].Contains(x, y))
    {
      if (mH.hwToPlugin)
        mH.hwToPlugin();
      CloseWin();
    }
  }

private:
  Hooks mH;
  std::vector<std::string> mPorts;
  int mIndex = 0;
  IRECT mPortRow, mPrev, mNext, mRescan, mPortName, mStatus;
  IRECT mBtn[3];
};

/* P13: the key split editor (the companion half of the CFW's live-key routing).
 * Live keys below the split note play the LOW track, every key at/above it the HIGH track. The
 * setting lives in the firmware settings record, so it survives the power cycle; this window only
 * mirrors and edits it. The virtual keybed overlay (sksplit::KeybedOverlay) shows the result. */
class SplitWindow final : public SkWindow
{
public:
  SplitWindow(const IRECT& bounds, CloseFn onClose)
  : SkWindow(bounds, "KEY SPLIT · LOWER / UPPER ZONE", std::move(onClose))
  {
  }

protected:
  float WinW() const override { return 620.f; }
  float WinH() const override { return 350.f; }
  int RowCount() const override { return 0; }

  void OnLayout(const IRECT& in) override
  {
    const sksplit::State now = sksplit::Read();
    if (now.note)
      mLastNote = now.note;
    else if (mLastNote < 1 || mLastNote > 127)
      mLastNote = 60;
    mOn = IRECT(in.L, in.T, in.L + 190.f, in.T + 48.f);
    mNoteRow = in.GetReducedFromTop(58.f).GetFromTop(54.f);
    mNoteDown = mNoteRow.GetFromLeft(40.f).GetCentredInside(34.f);
    mNoteUp = mNoteRow.GetFromRight(40.f).GetCentredInside(34.f);
    mNoteVal = IRECT(mNoteDown.R + 8.f, mNoteRow.T, mNoteUp.L - 8.f, mNoteRow.B);
    const IRECT lowRow = in.GetReducedFromTop(122.f).GetFromTop(40.f);
    const IRECT highRow = in.GetReducedFromTop(196.f).GetFromTop(40.f);
    mLowTag = lowRow.GetFromLeft(110.f);
    mHiTag = highRow.GetFromLeft(110.f);
    for (int i = 0; i < 4; ++i)
    {
      mChiplo[i] = lowRow.GetReducedFromLeft(118.f).GetGridCell(i, 1, 4).GetPadded(-4.f);
      mChihi[i] = highRow.GetReducedFromLeft(118.f).GetGridCell(i, 1, 4).GetPadded(-4.f);
    }
    mHint = in.GetFromBottom(34.f);
  }

  void DrawBody(IGraphics& g) override
  {
    const sksplit::State s = sksplit::Read();
    const bool on = s.note != 0;

    g.FillRoundRect(on ? skmint::kTheme : skmint::kWell, mOn, 6.f);
    g.DrawRoundRect(skmint::kTheme, mOn, 6.f, 0, 1.25f);
    g.DrawText(IText(17.f, on ? skmint::InkOn(skmint::kTheme) : skmint::kInk, skmint::kFont, EAlign::Center,
                     EVAlign::Middle),
               on ? "SPLIT ON" : "SPLIT OFF", mOn);
    g.DrawText(IText(12.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle),
               "Click to arm / disarm the split.", IRECT(mOn.R + 12.f, mOn.T, mOn.R + 300.f, mOn.B));

    auto drawBtn = [&](const IRECT& r, const char* label, bool enabled) {
      g.FillRoundRect(enabled ? skmint::kWell : skmint::kBg, r, 4.f);
      g.DrawRoundRect(skmint::kTheme, r, 4.f, 0, 1.f);
      g.DrawText(IText(16.f, enabled ? skmint::kInk : IColor(110, skmint::kText.R, skmint::kText.G, skmint::kText.B),
                       skmint::kFont, EAlign::Center, EVAlign::Middle),
                 label, r);
    };

    /* note row: -  C4  + */
    g.FillRoundRect(skmint::kWell, mNoteVal, 5.f);
    g.DrawRoundRect(on ? skmint::kTheme : skmint::kSurf, mNoteVal, 5.f, 0, 1.f);
    g.DrawText(IText(20.f, skmint::kAccent, skmint::kFont, EAlign::Center, EVAlign::Middle),
               on ? sksplit::NoteName(s.note).c_str() : "--", mNoteVal);
    drawBtn(mNoteDown, "-", on);
    drawBtn(mNoteUp, "+", on);
    g.DrawText(IText(12.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle), "SPLIT NOTE",
               IRECT(mNoteRow.L + 46.f, mNoteRow.T, mNoteRow.L + 300.f, mNoteRow.B));

    g.DrawText(IText(13.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle), "LOW (below)", mLowTag);
    g.DrawText(IText(13.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Middle), "HIGH (from)", mHiTag);
    for (int i = 0; i < 4; ++i)
    {
      auto chip = [&](const IRECT& r, int tr, bool hot) {
        g.FillRoundRect(hot ? skmint::kTrack[tr] : skmint::kWell, r, 5.f);
        g.DrawRoundRect(skmint::kTrack[tr], r, 5.f, 0, hot ? 2.f : 1.f);
        char b[8];
        std::snprintf(b, sizeof b, "T%d", tr + 1);
        g.DrawText(IText(14.f, hot ? IColor(255, 16, 16, 18) : skmint::kInk, skmint::kFont, EAlign::Center,
                         EVAlign::Middle),
                   b, r);
      };
      chip(mChiplo[i], i, s.lo == i);
      chip(mChihi[i], i, s.hi == i);
    }

    g.DrawText(IText(12.f, IColor(185, skmint::kText.R, skmint::kText.G, skmint::kText.B), skmint::kFont,
                     EAlign::Near, EVAlign::Middle),
               "Live keys and MIDI IN follow the split; sequencer and ARP playback keep their own track.", mHint);
  }

  void OnBodyDown(float x, float y) override
  {
    Layout();
    sksplit::State s = sksplit::Read();
    bool changed = false;
    if (mOn.Contains(x, y))
    {
      sksplit::Write(s.note ? 0 : mLastNote, s.lo, s.hi);
      changed = true;
    }
    else if (mNoteDown.Contains(x, y) && s.note)
    {
      mLastNote = std::max(1, s.note - 1);
      sksplit::Write(mLastNote, s.lo, s.hi);
      changed = true;
    }
    else if (mNoteUp.Contains(x, y) && s.note)
    {
      mLastNote = std::min(127, s.note + 1);
      sksplit::Write(mLastNote, s.lo, s.hi);
      changed = true;
    }
    else
    {
      for (int i = 0; i < 4 && !changed; ++i)
        if (mChiplo[i].Contains(x, y))
        {
          sksplit::Write(s.note ? s.note : mLastNote, i, s.hi == i ? (i + 1) % 4 : s.hi);
          changed = true;
        }
        else if (mChihi[i].Contains(x, y))
        {
          sksplit::Write(s.note ? s.note : mLastNote, s.lo == i ? (i + 1) % 4 : s.lo, i);
          changed = true;
        }
    }
    if (changed)
    {
      SetDirty(false);
      if (IGraphics* g = GetUI())
        g->SetAllControlsDirty();
    }
  }

private:
  IRECT mOn, mNoteRow, mNoteDown, mNoteUp, mNoteVal, mLowTag, mHiTag, mHint;
  IRECT mChiplo[4], mChihi[4];
  int mLastNote = 60; /* what ON restores after the split was switched off */
};

} // namespace skoverlay
