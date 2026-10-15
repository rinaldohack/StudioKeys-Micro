#pragma once

/* Patch window — the single modal opened from the bottom PATCH strip. One window,
 * three columns, tagged like the CFW:
 *   PRESETS  P01..Pnn  every engine's INIT patch + factory presets (ENGINE_ORDER)
 *   USER     U01..U32  the NOR user slots
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
    (void) mod;
    if (IsHidden())
      return;
    Layout();

    if (!mWin.Contains(x, y) || mClose.Contains(x, y))
    {
      CloseWin();
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
  : SkWindow(bounds, "PATCH · USER · IMPORTS", std::move(onClose))
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
  struct P { unsigned rank = 0, index = 0; bool init = false; std::string tag, name, engine; bool fav = false; };
  struct U { int slot = 0; bool used = false, fav = false; std::string tag, name, engine; };
  struct B { int bay = 0; bool isFm = true; std::string tag, name, engine; };

  void OnOpened() override { Rebuild(); }

  void Rebuild()
  {
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

    for (int slot = 0; slot < sk_host_user_slot_count() && slot < 32; ++slot)
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
  }

  /* --- geometry ---------------------------------------------------------- */
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

    DrawButtons(g, mBtnRow, {"Load to Synth", "Save to User", "Load DX7", "Load Virus TI"});
  }

  void DrawCol(IGraphics& g, int c, bool /*fav*/)
  {
    const int n = ColCount(c);
    const IRECT clip = mColList[c];
    detail::DrawList(g, clip, mScroll[c], n, kRowH, [&](int i, const IRECT& r) {
      const bool sel = (mSelCol == c && mSelRow == i);
      if (sel)
        g.FillRect(IColor(60, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), r);

      std::string tag, name, eng;
      bool fav2 = false, dim = false, cur = false;
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
      const std::string tagTxt = std::string(cur ? "*" : "") + tag;
      const std::string engTxt = std::string(fav2 ? "*" : "") + eng;
      g.DrawText(IText(11.f, cur ? skmint::kAccent : tc, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 tagTxt.c_str(), tagR);
      g.DrawText(IText(11.f, nc, skmint::kFont, EAlign::Near, EVAlign::Middle), name.c_str(),
                 inner.GetReducedFromLeft(50.f).GetReducedFromRight(48.f));
      g.DrawText(IText(10.f, sel ? skmint::kInk : skmint::kTheme, skmint::kFont, EAlign::Far, EVAlign::Middle),
                 engTxt.c_str(), engR);
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

  /* Toolbar: 0 Load to Synth, 1 Save to User, 2 Load DX7, 3 Load Virus TI.
   * Close is drawn by SkWindow. */
  void OnBodyButtons(float x, float y)
  {
    switch (HitButton(x, y))
    {
      case 0: LoadFocused(); break;
      case 1: SaveFocusedToUser(); break;
      case 2: ImportFromFile(true); break;   /* DX7 */
      case 3: ImportFromFile(false); break;  /* Virus TI */
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

  /* Load a .syx into the import bay. isDx7 picks the DX7 path first, else Virus. */
  void ImportFromFile(bool isDx7)
  {
    if (!mBay || !GetUI())
      return;
    WDL_String fileName, path;
    GetUI()->PromptForFile(fileName, path, EFileAction::Open, "syx");
    if (path.GetLength() == 0)
      return;
    std::ifstream f(path.Get(), std::ios::binary);
    if (!f)
      return;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty())
      return;
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
  std::vector<IRECT> mBtnHits;
};

} // namespace skoverlay
