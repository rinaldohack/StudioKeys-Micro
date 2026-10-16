#pragma once

#include "IControls.h"
#include "SkMint.h"
#include "sk_host.h"
#include "../../firmware/src/lc_logo.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

/** Flat SETTINGS desk + modal overlays (About / UBOOT roast). */
namespace sksettings {

using namespace iplug;
using namespace igraphics;

enum {
  kMiColor = 0, kMiStyle, kMiLarge, kMiAnim, kMiLeds, kMiLang,
  kMiHold, kMiAccel, kMiLatch, kMiBpmlock, kMiMaster,
  kMiLowcut, kMiUsb, kMiRmaud, kMiSerial,
  kMiPanel, kMiAbout, kMiUpdate,
  kMiExtClock = 99,
  kMiUnleashed = 100   /* VST-only: not a CFW MENU row, drawn on the card by the plugin */
};

/* ─── Flat settings: 2 columns, scrollable when content overflows ─── */

class SettingsPanel final : public IControl
{
public:
  using ActionFn = std::function<void(uint32_t row)>;

  SettingsPanel(const IRECT& bounds, ActionFn onAction)
  : IControl(bounds)
  , mOnAction(std::move(onAction))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kSurf, mRECT, 8.f);
    g.DrawRoundRect(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), mRECT, 8.f, 0, 1.f);

    RebuildLayout();
    ClampScroll();

    const IRECT clip = mRECT.GetPadded(-6.f);
    g.PathClipRegion(clip);

    /* Draw distinct card backgrounds for each settings group */
    for (const auto& c : mCards)
    {
      IRECT cr = c.r;
      cr.T -= mScrollY;
      cr.B -= mScrollY;
      if (cr.B < clip.T || cr.T > clip.B)
        continue;
      g.FillRoundRect(skmint::kWell, cr, 6.f);
      g.DrawRoundRect(IColor(65, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), cr, 6.f, 0, 1.2f);
    }

    const uint32_t nVal = sk_host_menu_values();
    for (const auto& h : mHits)
    {
      IRECT r = h.r;
      r.T -= mScrollY;
      r.B -= mScrollY;
      if (r.B < clip.T || r.T > clip.B)
        continue;

      if (h.isHeader)
      {
        g.FillRoundRect(skmint::kSurf, r, 4.f);
        g.DrawRoundRect(IColor(50, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), r, 4.f, 0, 1.f);
        g.DrawText(IText(13.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Middle),
                   sk_host_menu_tab_name(h.tab), r.GetPadded(-6.f));
        continue;
      }

      if (h.row == kMiUnleashed)
      {
        /* VST-only: Unleashed / Limited voice headroom. Not a CFW MENU row — it
         * lives on the card beside the device settings. */
        g.FillRoundRect(IColor(22, 255, 255, 255), r, 4.f);
        g.DrawText(IText(14.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                   "VST POLYPHONY",
                   r.GetPadded(-8.f).GetReducedFromRight(r.W() * 0.42f));
        const IRECT chip = r.GetFromRight(std::min(150.f, r.W() * 0.40f)).GetPadded(-3.f);
        const bool on = (sk_host_unleashed() != 0);
        g.FillRoundRect(on ? skmint::kTheme : skmint::kSurf, chip, 4.f);
        if (!on)
          g.DrawRoundRect(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), chip, 4.f, 0, 1.f);
        g.DrawText(IText(13.f, on ? skmint::InkOn(skmint::kTheme) : skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                   on ? "UNLEASHED" : "LIMITED", chip);
        continue;
      }

      if (h.row == kMiMaster)
      {
        /* The MASTER volume fader (was the top-bar fader — too ugly there). Linked
         * to the CFW MENU > AUDIO > MASTER row: the same 0..1023 master level,
         * sk_host_master() -> song.master_q12. */
        g.FillRoundRect(IColor(22, 255, 255, 255), r, 4.f);
        const uint32_t n = sk_host_menu_n(h.row);
        const uint32_t val = sk_host_menu_get(h.row);
        const char* vn = sk_host_menu_vname(h.row, val);
        g.DrawText(IText(14.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                   "MASTER VOLUME",
                   r.GetPadded(-8.f).GetReducedFromRight(std::min(150.f, r.W() * 0.40f) + 44.f));
        const IRECT rail = r.GetFromRight(std::min(150.f, r.W() * 0.40f)).GetPadded(-6.f).GetMidVPadded(6.f);
        g.FillRoundRect(skmint::kWell, rail, 3.f);
        g.DrawRoundRect(skmint::kTheme, rail, 3.f, 0, 1.f);
        const float frac = n > 1u ? (float) val / (float) (n - 1u) : 0.f;
        const float x = rail.L + rail.W() * frac;
        g.FillRoundRect(skmint::kTheme, IRECT(rail.L, rail.MH() - 2.f, x, rail.MH() + 2.f), 1.f);
        const IRECT cap(x - 3.f, rail.MH() - 7.f, x + 3.f, rail.MH() + 7.f);
        g.FillRoundRect(skmint::kAccent, cap, 2.f);
        g.DrawRoundRect(skmint::kTheme, cap, 2.f, 0, 1.f);
        const IRECT valR = r.GetFromRight(std::min(150.f, r.W() * 0.40f)).GetFromTop(14.f);
        g.DrawText(IText(12.f, skmint::kAccent, skmint::kFont, EAlign::Far, EVAlign::Middle),
                   vn && vn[0] ? vn : "—", valR);
        continue;
      }

      if (h.row == kMiExtClock)
      {
        g.FillRoundRect(IColor(22, 255, 255, 255), r, 4.f);
        g.DrawText(IText(14.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                   "Use external MIDI clock",
                   r.GetPadded(-8.f).GetReducedFromRight(r.W() * 0.42f));
        const IRECT chip = r.GetFromRight(std::min(150.f, r.W() * 0.40f)).GetPadded(-3.f);
        const bool on = (sk_host_get_gparam(2 /* G_CLOCK */) != 0);
        g.FillRoundRect(on ? skmint::kTheme : skmint::kSurf, chip, 4.f);
        if (!on)
          g.DrawRoundRect(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), chip, 4.f, 0, 1.f);
        g.DrawText(IText(13.f, on ? skmint::InkOn(skmint::kTheme) : skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                   on ? "ON" : "OFF", chip);
        continue;
      }

      g.FillRoundRect(IColor(22, 255, 255, 255), r, 4.f);
      const char* lab = sk_host_menu_row_label(h.row);
      g.DrawText(IText(14.f, skmint::kInk, skmint::kFont, EAlign::Near, EVAlign::Middle),
                 lab && lab[0] ? lab : "?",
                 r.GetPadded(-8.f).GetReducedFromRight(r.W() * 0.42f));

      const IRECT chip = r.GetFromRight(std::min(150.f, r.W() * 0.40f)).GetPadded(-3.f);
      if (h.row < nVal)
      {
        const uint32_t val = sk_host_menu_get(h.row);
        const char* vn = sk_host_menu_vname(h.row, val);
        const uint32_t n = sk_host_menu_n(h.row);

        if (h.row == kMiColor)
        {
          g.FillRoundRect(skmint::kSurf, chip, 4.f);
          g.DrawRoundRect(skmint::kTheme, chip, 4.f, 0, 1.2f);
          const IRECT dot = chip.GetFromLeft(chip.H()).GetCentredInside(12.f, 12.f);
          g.FillCircle(skmint::kTheme, dot.MW(), dot.MH(), 5.f);
          g.DrawText(IText(13.f, skmint::kAccent, skmint::kFont, EAlign::Center, EVAlign::Middle),
                     vn && vn[0] ? vn : "—", chip.GetReducedFromLeft(chip.H()));
        }
        else if (n == 2u)
        {
          const bool on = (val != 0);
          g.FillRoundRect(on ? skmint::kTheme : skmint::kSurf, chip, 4.f);
          if (!on)
            g.DrawRoundRect(IColor(70, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B), chip, 4.f, 0, 1.f);
          g.DrawText(IText(13.f, on ? skmint::InkOn(skmint::kTheme) : skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                     vn && vn[0] ? vn : (on ? "ON" : "OFF"), chip);
        }
        else
        {
          g.FillRoundRect(skmint::kSurf, chip, 4.f);
          g.DrawRoundRect(skmint::kTheme, chip, 4.f, 0, 1.f);
          g.DrawText(IText(13.f, skmint::kAccent, skmint::kFont, EAlign::Center, EVAlign::Middle),
                     vn && vn[0] ? vn : "—", chip);
        }
      }
      else
      {
        g.FillRoundRect(skmint::kTheme, chip, 4.f);
        g.DrawText(IText(13.f, skmint::InkOn(skmint::kTheme), skmint::kFont, EAlign::Center, EVAlign::Middle),
                   "OPEN", chip);
      }
    }

    g.PathClipRegion(); /* clear clip */

    if (mContentH > clip.H() + 1.f)
    {
      const float trackH = clip.H();
      const float thumbH = std::max(24.f, trackH * trackH / mContentH);
      const float maxScroll = mContentH - trackH;
      const float thumbY = clip.T + (maxScroll > 0.f ? (mScrollY / maxScroll) * (trackH - thumbH) : 0.f);
      const IRECT track = clip.GetFromRight(6.f).GetPadded(-1.f);
      g.FillRoundRect(skmint::kWell, track, 3.f);
      g.FillRoundRect(skmint::kTheme, IRECT(track.L, thumbY, track.R, thumbY + thumbH), 3.f);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) mod;
    RebuildLayout();
    ClampScroll();
    for (const auto& h : mHits)
    {
      IRECT r = h.r;
      r.T -= mScrollY;
      r.B -= mScrollY;
      if (h.isHeader || !r.Contains(x, y))
        continue;
      const uint32_t nVal = sk_host_menu_values();
      if (h.row >= nVal && h.row != kMiUnleashed && h.row != kMiMaster)
      {
        if (mOnAction)
          mOnAction(h.row);
        return;
      }
      if (h.row == kMiUnleashed)
      {
        sk_host_set_unleashed(sk_host_unleashed() ? 0 : 1);
        SetDirty(false);
        return;
      }
      if (h.row == kMiMaster)
      {
        mDragRow = kMiMaster;
        SetMasterFromX(r, x);
        return;
      }
      const uint32_t n = sk_host_menu_n(h.row);
      if (n <= 1u)
        return;
      if (n == 2u)
      {
        sk_host_menu_put(h.row, sk_host_menu_get(h.row) ? 0u : 1u);
        if (h.row == kMiColor)
        {
          skmint::SyncFromHost();
          if (GetUI())
          {
            GetUI()->AttachPanelBackground(skmint::kBg);
            GetUI()->SetAllControlsDirty();
          }
        }
        SetDirty(false);
        return;
      }
      OpenChoice(h.row, r.GetFromRight(std::min(140.f, r.W() * 0.38f)).GetPadded(-5.f));
      return;
    }
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    (void) x; (void) y; (void) mod;
    mDragRow = ~0u;
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    (void) x;
    (void) y;
    (void) mod;
    RebuildLayout();
    mScrollY -= d * 28.f;
    ClampScroll();
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) dX;
    (void) mod;
    if (mDragRow == kMiMaster)
    {
      RebuildLayout();
      ClampScroll();
      for (const auto& h : mHits)
      {
        if (h.row != kMiMaster)
          continue;
        IRECT r = h.r;
        r.T -= mScrollY;
        r.B -= mScrollY;
        SetMasterFromX(r, x);
        return;
      }
      SetDirty(false);
      return;
    }
    (void) x;
    (void) y;
    RebuildLayout();
    mScrollY -= dY;
    ClampScroll();
    SetDirty(false);
  }

  void OnPopupMenuSelection(IPopupMenu* pMenu, int valIdx) override
  {
    (void) valIdx;
    if (!pMenu || mPopupRow >= sk_host_menu_values())
      return;
    const int idx = pMenu->GetChosenItemIdx();
    if (idx < 0)
      return;
    sk_host_menu_put(mPopupRow, (uint32_t) idx);
    if (mPopupRow == kMiColor)
    {
      skmint::SyncFromHost();
      if (GetUI())
      {
        GetUI()->AttachPanelBackground(skmint::kBg);
        GetUI()->SetAllControlsDirty();
      }
    }
    SetDirty(false);
  }

private:
  struct Hit
  {
    IRECT r; /* content-space (unscrolled) */
    uint32_t row = 0;
    uint32_t tab = 0;
    bool isHeader = false;
  };

  struct CardHit
  {
    IRECT r;
    uint32_t tab = 0;
  };

  void ClampScroll()
  {
    const float viewH = mRECT.H() - 12.f;
    const float maxScroll = std::max(0.f, mContentH - viewH);
    if (mScrollY < 0.f)
      mScrollY = 0.f;
    if (mScrollY > maxScroll)
      mScrollY = maxScroll;
  }

  void RebuildLayout()
  {
    mHits.clear();
    mCards.clear();
    const IRECT pad = mRECT.GetPadded(-10.f).GetReducedFromRight(10.f); /* room for scrollbar */
    const uint32_t nTab = sk_host_menu_tab_count();
    const uint32_t nRows = sk_host_menu_count();
    const float colW = pad.W() / 2.f;
    const float headerH = 26.f;
    const float rowH = 34.f;

    float y[2] = {pad.T, pad.T};
    int col = 0;

    for (uint32_t t = 0; t < nTab; ++t)
    {
      std::vector<uint32_t> rows;
      for (uint32_t row = 0; row < nRows; ++row)
        if (sk_host_menu_tab_of(row) == t)
          rows.push_back(row);
      if (rows.empty())
        continue;

      col = (y[0] <= y[1]) ? 0 : 1;
      const float x0 = pad.L + col * colW;
      const float cardTop = y[col];

      const IRECT head(x0 + 6.f, y[col] + 6.f, x0 + colW - 6.f, y[col] + 6.f + headerH);
      mHits.push_back({head, 0, t, true});
      y[col] += headerH + 10.f;

      for (uint32_t row : rows)
      {
        const IRECT cell(x0 + 8.f, y[col], x0 + colW - 8.f, y[col] + rowH - 4.f);
        mHits.push_back({cell, row, t, false});
        y[col] += rowH;
      }
      if (t == 1 /* MTAB_CONTROL */)
      {
        const IRECT cell(x0 + 8.f, y[col], x0 + colW - 8.f, y[col] + rowH - 4.f);
        mHits.push_back({cell, kMiExtClock, t, false});
        y[col] += rowH;
        const IRECT vc(x0 + 8.f, y[col], x0 + colW - 8.f, y[col] + rowH - 4.f);
        mHits.push_back({vc, kMiUnleashed, t, false});
        y[col] += rowH;
      }
      y[col] += 6.f;
      const IRECT cardRect(x0 + 3.f, cardTop, x0 + colW - 3.f, y[col]);
      mCards.push_back({cardRect, t});
      y[col] += 12.f;
    }

    mContentH = std::max(y[0], y[1]) - pad.T + 8.f;
  }

  void OpenChoice(uint32_t row, const IRECT& anchor)
  {
    IGraphics* g = GetUI();
    if (!g)
      return;
    const uint32_t n = sk_host_menu_n(row);
    if (n < 2u)
      return;
    mPopupRow = row;
    mMenu.Clear(true);
    mMenu.SetRootTitle(sk_host_menu_row_label(row));
    for (uint32_t i = 0; i < n; ++i)
    {
      const char* vn = sk_host_menu_vname(row, i);
      mMenu.AddItem(vn && vn[0] ? vn : "?");
    }
    mMenu.SetChosenItemIdx((int) sk_host_menu_get(row));
    g->CreatePopupMenu(*this, mMenu, anchor);
  }

  /* MASTER volume: map a click/drag x over the row to the CFW MENU > AUDIO > MASTER
   * row's value (0..n-1), then push it through the same menu path the OLED uses. */
  void SetMasterFromX(const IRECT& r, float x)
  {
    const uint32_t n = sk_host_menu_n(kMiMaster);
    if (n < 2u)
      return;
    const IRECT rail = r.GetFromRight(std::min(150.f, r.W() * 0.40f)).GetPadded(-6.f).GetMidVPadded(6.f);
    if (rail.W() <= 0.f)
      return;
    float f = (x - rail.L) / rail.W();
    f = std::clamp(f, 0.f, 1.f);
    uint32_t v = (uint32_t) std::lround(f * (float) (n - 1u));
    sk_host_menu_put(kMiMaster, v);
    sk_host_ui_pump();
    SetDirty(false);
  }

  float mScrollY = 0.f;
  float mContentH = 0.f;
  uint32_t mPopupRow = 0;
  uint32_t mDragRow = ~0u;
  IPopupMenu mMenu;
  ActionFn mOnAction;
  std::vector<Hit> mHits;
  std::vector<CardHit> mCards;
};

/* ─── Self-drawing modal (topmost via desk-hide while open) ─── */

class ModalOverlay final : public IControl
{
public:
  using CloseFn = std::function<void()>;

  ModalOverlay(const IRECT& bounds, CloseFn onClose)
  : IControl(bounds)
  , mOnClose(std::move(onClose))
  {
    Hide(true);
  }

  void SetAbout()
  {
    mKind = Kind::About;
    mAutoCloseMs = 0;
    mOpenedAt = 0;
    mScrollY = 0.f;
    SetDirty(false);
  }

  void SetUbootRoast(bool hwMode)
  {
    mKind = hwMode ? Kind::UbootHw : Kind::UbootRoast;
    mAutoCloseMs = 5000;
    mOpenedAt = sk_host_now_ms();
    mScrollY = 0.f;
    SetDirty(false);
  }

  void Open()
  {
    Hide(false);
    if (mKind == Kind::About)
      sk_host_about_open();
    SetDirty(false);
  }

  void Close()
  {
    Hide(true);
    if (mKind == Kind::About)
      sk_host_about_close();
    if (mOnClose)
      mOnClose();
    SetDirty(false);
  }

  bool IsOpen() const { return !IsHidden(); }
  bool IsAbout() const { return mKind == Kind::About; }
  bool IsUboot() const { return mKind == Kind::UbootRoast || mKind == Kind::UbootHw; }
  bool IsUbootHw() const { return mKind == Kind::UbootHw; }
  uint32_t AutoCloseMs() const { return mAutoCloseMs; }
  uint32_t OpenedAt() const { return mOpenedAt; }

  void Restore(bool about, bool ubootHw, uint32_t autoCloseMs, uint32_t openedAt, bool open)
  {
    if (about)
      SetAbout();
    else
      SetUbootRoast(ubootHw);
    mAutoCloseMs = autoCloseMs;
    mOpenedAt = openedAt;
    if (open)
      Open();
    else
      Hide(true);
  }

  void OnGUIIdle() override
  {
    if (IsHidden() || !mAutoCloseMs)
      return;
    const uint32_t now = sk_host_now_ms();
    if (mOpenedAt && (int32_t) (now - mOpenedAt) >= (int32_t) mAutoCloseMs)
      Close();
    else
      SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    if (IsHidden())
      return;

    g.FillRect(IColor(200, 0, 0, 0), mRECT);

    const IRECT win = mRECT.GetCentredInside(560.f, 380.f);
    g.FillRoundRect(skmint::kSurf, win, 10.f);
    g.DrawRoundRect(skmint::kTheme, win, 10.f, 0, 2.f);

    const IRECT titleR = win.GetFromTop(36.f).GetPadded(-16.f).GetReducedFromRight(80.f);
    const IRECT closeR = win.GetFromTRHC(72.f, 28.f).GetTranslated(-12.f, 10.f);
    mCloseHit = closeR;

    const char* title = "ABOUT / CREDITS";
    if (mKind == Kind::UbootRoast)
      title = "UBOOT REJECTED";
    else if (mKind == Kind::UbootHw)
      title = "DEVICE DISCONNECTED";

    g.DrawText(IText(20.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Middle),
               title, titleR);

    g.FillRoundRect(skmint::kTheme, closeR, 5.f);
    g.DrawText(IText(14.f, skmint::InkOn(skmint::kTheme), skmint::kFont, EAlign::Center, EVAlign::Middle),
               mAutoCloseMs ? "…" : "Close", closeR);

    const IRECT body = win.GetReducedFromTop(48.f).GetReducedFromBottom(mAutoCloseMs ? 36.f : 12.f).GetPadded(-16.f);
    DrawBody(g, body);

    if (mAutoCloseMs && mOpenedAt)
    {
      const uint32_t now = sk_host_now_ms();
      int left = (int) ((mAutoCloseMs - (now - mOpenedAt) + 999u) / 1000u);
      if (left < 0)
        left = 0;
      if (left > 5)
        left = 5;
      char foot[24];
      snprintf(foot, sizeof foot, "%ds -> MENU", left);
      g.DrawText(IText(15.f, skmint::kAccent, skmint::kFont, EAlign::Center, EVAlign::Middle),
                 foot, win.GetFromBottom(32.f));
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    (void) mod;
    if (IsHidden())
      return;
    if (mCloseHit.Contains(x, y))
    {
      Close();
      return;
    }
    const IRECT win = mRECT.GetCentredInside(560.f, 380.f);
    if (!win.Contains(x, y))
      Close();
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    (void) x;
    (void) y;
    (void) mod;
    if (IsHidden() || mKind != Kind::About)
      return;
    mScrollY -= d * 24.f;
    if (mScrollY < 0.f)
      mScrollY = 0.f;
    if (mScrollY > mScrollMax)
      mScrollY = mScrollMax;
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    (void) x;
    (void) y;
    (void) dX;
    (void) mod;
    if (IsHidden() || mKind != Kind::About)
      return;
    mScrollY -= dY;
    if (mScrollY < 0.f)
      mScrollY = 0.f;
    if (mScrollY > mScrollMax)
      mScrollY = mScrollMax;
    SetDirty(false);
  }

private:
  enum class Kind { About, UbootRoast, UbootHw };

  /* LangitCinta! mark — same bitmap as OLED menu_logo() / lc_logo.h. */
  static constexpr float kLogoDrawH = 72.f;

  static void DrawLcLogo(IGraphics& g, const IRECT& slot)
  {
    const float cellW = slot.W() / (float) LC_LOGO_W;
    const float cellH = slot.H() / (float) LC_LOGO_H;
    const float scale = std::min(cellW, cellH);
    const float ox = slot.MW() - 0.5f * LC_LOGO_W * scale;
    const float oy = slot.MH() - 0.5f * LC_LOGO_H * scale;
    /* Tight nibble pack (cv_alpha): odd width does NOT pad to a byte per row. */
    for (uint32_t ly = 0; ly < LC_LOGO_H; ++ly)
    {
      for (uint32_t lx = 0; lx < LC_LOGO_W; ++lx)
      {
        const uint32_t k = ly * LC_LOGO_W + lx;
        const uint8_t packed = LC_LOGO[k >> 1];
        const uint8_t nib = (k & 1u) ? (packed & 0x0Fu) : (packed >> 4);
        if (!nib)
          continue;
        const int a = 255 * (int) nib / 15;
        g.FillRect(IColor(a, skmint::kTheme.R, skmint::kTheme.G, skmint::kTheme.B),
                   IRECT(ox + lx * scale, oy + ly * scale,
                         ox + (lx + 1) * scale, oy + (ly + 1) * scale));
      }
    }
  }

  void DrawBody(IGraphics& g, const IRECT& body)
  {
    auto line = [&](float y, const char* s, const IColor& c, float sz = 13.f) {
      g.DrawText(IText(sz, c, skmint::kFont, EAlign::Near, EVAlign::Top), s,
                 IRECT(body.L, body.T + y, body.R, body.T + y + sz + 4.f));
    };

    if (mKind == Kind::About)
    {
      /* Same document as OLED menu_document(): header → CREDITS → logo → credits → footer. */
      g.PathClipRegion(body);
      const uint32_t n = sk_host_about_line_count();
      const float lineH = 18.f;
      const float logoBlock = kLogoDrawH + 12.f; /* after CREDITS (index 5), like menu_logo() */
      float contentH = logoBlock;
      for (uint32_t i = 0; i < n; ++i)
      {
        uint8_t kind = 0;
        sk_host_about_line(i, &kind);
        contentH += (kind == 3u) ? 10.f : lineH;
      }
      mScrollMax = std::max(0.f, contentH - body.H() + 8.f);
      if (mScrollY > mScrollMax)
        mScrollY = mScrollMax;

      float y = -mScrollY;
      for (uint32_t i = 0; i < n; ++i)
      {
        uint8_t kind = 0;
        const char* text = sk_host_about_line(i, &kind);
        if (kind == 3u)
        {
          y += 10.f;
          continue;
        }
        if (!text)
          text = "";
        const bool title = kind == 1u;
        const bool url = kind == 2u;
        const IColor col = title ? skmint::kTheme : (url ? skmint::kAccent : skmint::kInk);
        const float sz = title ? 14.f : 12.f;
        if (y + sz > -4.f && y < body.H())
          g.DrawText(IText(sz, col, skmint::kFont, EAlign::Near, EVAlign::Top), text,
                     IRECT(body.L, body.T + y, body.R - 10.f, body.T + y + sz + 4.f));
        y += lineH;

        /* Insert logo after CREDITS title (same order as firmware menu_document). */
        if (i == 5u)
        {
          const IRECT slot(body.L, body.T + y, body.R - 10.f, body.T + y + kLogoDrawH);
          if (slot.B > body.T && slot.T < body.B)
            DrawLcLogo(g, slot);
          y += logoBlock;
        }
      }
      g.PathClipRegion();
      if (mScrollMax > 1.f)
      {
        const float trackH = body.H();
        const float thumbH = std::max(20.f, trackH * trackH / (trackH + mScrollMax));
        const float thumbY = body.T + (mScrollY / mScrollMax) * (trackH - thumbH);
        g.FillRoundRect(skmint::kWell, body.GetFromRight(5.f), 2.f);
        g.FillRoundRect(skmint::kTheme, IRECT(body.R - 5.f, thumbY, body.R, thumbY + thumbH), 2.f);
      }
      return;
    }
    if (mKind == Kind::UbootHw)
    {
      line(20, "UBOOT MODE in device.", skmint::kTheme, 16.f);
      line(52, "Hardware entered UBOOT.", skmint::kInk);
      line(80, "This companion window will close automatically.", skmint::kInk, 12.f);
      return;
    }
    line(8, "why the fuck you wanted UBOOT on VST?", skmint::kTheme, 14.f);
    line(32, "go do that on your hardware!", skmint::kTheme, 14.f);
    line(64, "for developers: this WAS NOT a 1:1", skmint::kInk);
    line(82, "JieLi AC791N emulator. go have fun", skmint::kInk);
    line(100, "somewhere else.", skmint::kInk);
  }

  Kind mKind = Kind::About;
  uint32_t mAutoCloseMs = 0;
  uint32_t mOpenedAt = 0;
  float mScrollY = 0.f;
  float mScrollMax = 0.f;
  IRECT mCloseHit;
  CloseFn mOnClose;
};

} // namespace sksettings
