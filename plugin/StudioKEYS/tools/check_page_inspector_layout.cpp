/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
// Layout verifier for the P8 PAGE inspector under the 4-track mixer (StudioKEYS.cpp RebuildDesk
// and SkPageKnob in SkPanelControls.h).
//
// The cells are 4 x 2 in a column ~290 px wide, so each one is ~64 x 39 px. The first version
// drew an 18 px label and a 26 px value strip in that cell (44 px of content in 33 px of room)
// plus a 16 px needle circle in the bottom-right corner: the ring ran through the label and the
// value, and the value ran off the edge (the 'CH BIT / W LS' mess). This walks the same IRECT
// math with the same iPlug2 semantics (GetPadded: positive expands, negative contracts) and
// asserts the three pieces stack without touching at the real editor size and a few smaller ones.
//
// Build/run: tools/check_page_inspector_layout.sh
#include <algorithm>
#include <cstdio>
#include <string>

struct R
{
  float L, T, Rr, B;
  float W() const { return Rr - L; }
  float H() const { return B - T; }
  R GetPadded(float p) const { return {L - p, T - p, Rr + p, B + p}; }
  R GetFromTop(float a) const { return {L, T, Rr, T + a}; }
  R GetFromBottom(float a) const { return {L, B - a, Rr, B}; }
  R GetFromLeft(float a) const { return {L, T, L + a, B}; }
  R GetFromRight(float a) const { return {Rr - a, T, Rr, B}; }
  R GetReducedFromTop(float a) const { return {L, T + a, Rr, B}; }
  R GetReducedFromBottom(float a) const { return {L, T, Rr, B - a}; }
  R SubV(int n, int i) const
  {
    const float h = H() / (float) n;
    return {L, T + h * (float) i, Rr, T + h * (float) (i + 1)};
  }
  R SubH(int n, int i) const
  {
    const float w = W() / (float) n;
    return {L + w * (float) i, T, L + w * (float) (i + 1), B};
  }
  R GridCell(int idx, int nr, int nc) const { return SubV(nr, idx / nc).SubH(nc, idx % nc); }
};

static int gFail;

static bool Overlaps(const R& a, const R& b)
{
  const float ix = std::min(a.Rr, b.Rr) - std::max(a.L, b.L);
  const float iy = std::min(a.B, b.B) - std::max(a.T, b.T);
  return ix > 0.01f && iy > 0.01f;
}

static void Check(bool ok, const char* what, const char* detail = "", float a = 0.f, float b = 0.f)
{
  if (ok)
    return;
  std::printf("FAIL PAGE %-42s %s (%.1f x %.1f)\n", what, detail, a, b);
  ++gFail;
}

/* enough for a 14 px sans string in a 48 px box (Roboto-ish: 0.58 em per glyph) */
static float TextW(const char* s, float size)
{
  float n = 0.f;
  for (; *s; ++s)
    n += 1.f;
  return n * size * 0.58f;
}

/* the longest strings the formatter can put in a cell (sk_host_format_param's box is 48 too) */
static const char* kValues[] = {"-64 %", "+100 %", "1.20 s", "120 BPM", "SQ50", "STAIR", "127B", "OFF", "DIMWH",
                                "ROT", "REUSE", "83.3 Hz", "4BIT", "0.0 dB", "OWN"};
static const char* kLabels[] = {"WAV#", "MODEL", "CHRD", "DTUN", "LVL", "RATE", "SLOT", "MODE"};

static void PageBox(float winW, float winH, bool strict)
{
  /* StudioKEYS.cpp RebuildDesk: the same chain the PAGE inspector is built from */
  const float chromeH = 32.f, deskFrac = 0.46f;

  const R b = R{10.f, 10.f, winW - 10.f, winH - 10.f};
  const R deskBand = b.GetReducedFromTop(chromeH + 4.f).GetFromTop(b.H() * deskFrac);
  const R body = deskBand.GetReducedFromTop(32.f).GetPadded(-8.f);
  const R mixerBody = body.GetFromRight(body.W() * 0.24f).GetPadded(-4.f);
  const R kPageInspH = mixerBody.GetFromBottom(std::clamp(mixerBody.H() * 0.36f, 96.f, 124.f));
  const R pageBox = kPageInspH.GetPadded(-2.f);
  const R strips = mixerBody.GetReducedFromBottom(kPageInspH.H() + 4.f).GetReducedFromTop(24.f).GetPadded(-2.f);
  const R pg = pageBox.GetReducedFromTop(22.f).GetPadded(-4.f);

  std::printf("== %.0f x %.0f: mixer column %.0f x %.0f, page box %.0f x %.0f, strips %.0f x %.0f\n", winW, winH,
              mixerBody.W(), mixerBody.H(), pageBox.W(), pageBox.H(), strips.W(), strips.H());

  /* the mixer strips keep a fader: 56 px of header + 72 px of buttons at the bottom (Fm6Desk.h) */
  {
    const float fader = strips.H() - 56.f - 72.f;
    if (strict)
      Check(fader >= 40.f, "mixer strips keep a usable fader", "", strips.W(), fader);
    else if (fader < 40.f)
      std::printf("   note: at this size the mixer faders are already %.0f px (not this panel's doing)\n", fader);
  }

  for (int i = 0; i < 8; ++i)
  {
    const R cell = pg.GridCell(i, 2, 4).GetPadded(-3.f);
    const R in = cell.GetPadded(-4.f);
    const R label = in.GetFromTop(11.f);
    const R value = R{in.L, in.T + 11.f, in.Rr, in.B - 2.f};
    const R bar = in.GetFromBottom(2.f);

    if (strict)
      Check(in.W() > 30.f && in.H() > 20.f, "cell has room", "", in.W(), in.H());
    else if (in.H() < 20.f)
      std::printf("   note: at this size a cell is only %.0f px tall (the whole mixer column is cramped)\n", in.H());
    /* these hold at every size: the three strips are sized from the cell, so they can never
     * cross each other (the old fixed 18 + 26 px strips could, and did) */
    Check(!Overlaps(label, value), "label / value overlap", "", label.W(), value.H());
    Check(!Overlaps(value, bar), "value / bar overlap", "", value.W(), bar.H());
    Check(label.T >= cell.T - 0.01f && bar.B <= cell.B + 0.01f, "cell content spills", "", in.W(), in.H());

    /* the value is drawn at 14 px and shrinks to 9 px before it may clip (SkPageKnob); the
     * label is a fixed 11 px and must fit as it is */
    float worst = 0.f;
    const char* worstS = "";
    for (const char* v : kValues)
    {
      const float floor = TextW(v, 9.f);
      if (floor > in.W() && floor > worst)
      {
        worst = floor;
        worstS = v;
      }
    }
    if (strict)
      Check(worst == 0.f, "value fits at the 9 px floor", worstS, worst, in.W());
    if (strict)
      for (const char* l : kLabels)
        Check(TextW(l, 11.f) <= in.W(), "label fits at 11 px", l, TextW(l, 11.f), in.W());
  }
}

int main()
{
  PageBox(1280.f, 880.f, true);    /* PLUG_WIDTH x PLUG_HEIGHT: the size the plugin opens at */
  PageBox(1100.f, 740.f, false);
  PageBox(900.f, 620.f, false);
  if (gFail)
    std::printf("page inspector layout: %d FAIL\n", gFail);
  else
    std::printf("page inspector layout: ok\n");
  return gFail ? 1 : 0;
}
