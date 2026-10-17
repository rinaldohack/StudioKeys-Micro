// Layout verifier for the P7 engine desks (plugin/StudioKEYS/{Lofi,Sample,Voice,Trio,Grain,
// Phys,Noise,Slice,Drum}Desk.h).
//
// The nine desks share one skeleton: three module columns (scope | params | params) over a
// full-width ADSR strip. This re-implements the IRECT math with the same iPlug2 helpers and
// asserts, for the real editor size and a few smaller ones, that nothing overlaps, everything
// stays inside the desk body, and the knobs/scope keep a usable size.
//
// Build/run: tools/check_desks_layout.sh
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct R
{
  float L, T, Rr, B;
  float W() const { return Rr - L; }
  float H() const { return B - T; }
  float MH() const { return 0.5f * (T + B); }
  R GetPadded(float p) const { return {L - p, T - p, Rr + p, B + p}; }
  R GetFromTop(float a) const { return {L, T, Rr, T + a}; }
  R GetFromBottom(float a) const { return {L, B - a, Rr, B}; }
  R GetFromRight(float a) const { return {Rr - a, T, Rr, B}; }
  R GetFromLeft(float a) const { return {L, T, L + a, B}; }
  R GetReducedFromTop(float a) const { return {L, T + a, Rr, B}; }
  R GetReducedFromBottom(float a) const { return {L, T, Rr, B - a}; }
  R GetReducedFromLeft(float a) const { return {L + a, T, Rr, B}; }
  R GetReducedFromRight(float a) const { return {L, T, Rr - a, B}; }
  R SubV(int n, int i) const
  {
    const float h = H() / (float) n;
    return {L, T + h * i, Rr, T + h * (i + 1)};
  }
  R SubH(int n, int i) const
  {
    const float w = W() / (float) n;
    return {L + w * i, T, L + w * (i + 1), B};
  }
  R GridCell(int idx, int nr, int nc) const { return SubV(nr, idx / nc).SubH(nc, idx % nc); }
  R GetCentredInside(float side) const
  {
    const float x = std::min(side, W()), y = std::min(side, H());
    const float cx = 0.5f * (L + Rr), cy = 0.5f * (T + B);
    return {cx - x * 0.5f, cy - y * 0.5f, cx + x * 0.5f, cy + y * 0.5f};
  }
};

static int gFail = 0;

static bool Overlaps(const R& a, const R& b)
{
  const float ix = std::min(a.Rr, b.Rr) - std::max(a.L, b.L);
  const float iy = std::min(a.B, b.B) - std::max(a.T, b.T);
  return ix > 0.01f && iy > 0.01f;
}

static void Check(bool ok, const char* desk, const char* what, float w = 0.f, float h = 0.f)
{
  if (ok)
    return;
  std::printf("FAIL %-7s %-28s (%.1f x %.1f)\n", desk, what, w, h);
  ++gFail;
}

static void Sane(const R& r, const char* desk, const char* what)
{
  Check(r.W() > 0.f && r.H() > 0.f, desk, what, r.W(), r.H());
}

/** Desk skeleton check. Fractions come from each desk's Attach*Desk(). */
static void Desk(const char* name, float scopeFrac, float midFrac, float rightFrac, float w, float h)
{
  const R body{0.f, 0.f, w, h};
  const float stripH = std::clamp(h * 0.28f, 60.f, 92.f);
  const R top = body.GetReducedFromBottom(stripH + 6.f);
  const R strip = body.GetFromBottom(stripH);

  const R scopeCol = top.GetFromLeft(w * scopeFrac).GetPadded(-3.f);
  const R midCol = top.GetReducedFromLeft(w * scopeFrac).GetFromLeft(w * midFrac).GetPadded(-3.f);
  const R rightCol = top.GetFromRight(w * rightFrac).GetPadded(-3.f);

  Sane(top, name, "top");
  Sane(strip, name, "strip");
  Sane(scopeCol, name, "scope column");
  Sane(midCol, name, "mid column");
  Sane(rightCol, name, "right column");

  /* Columns must not touch: the -3 gutters are what keeps them apart. */
  Check(!Overlaps(scopeCol, midCol), name, "scope/mid overlap", w, h);
  Check(!Overlaps(midCol, rightCol), name, "mid/right overlap", w, h);
  Check(!Overlaps(scopeCol, rightCol), name, "scope/right overlap", w, h);
  Check(!Overlaps(top, strip), name, "top/strip overlap", w, h);
  Check(rightCol.Rr <= body.Rr + 0.01f, name, "right column spills", w, h);
  Check(strip.B <= body.B + 0.01f, name, "strip spills", w, h);

  /* The scope's control area. */
  const R scope = scopeCol.GetReducedFromTop(22.f).GetPadded(-5.f);
  Sane(scope, name, "scope area");
  Check(scope.W() >= 120.f && scope.H() >= 60.f, name, "scope too small", scope.W(), scope.H());
  Check(!Overlaps(scope, strip), name, "scope/strip overlap", w, h);

  /* Two 2x2 param blocks. A knob cell needs room for its label, dial and value. */
  for (int c = 0; c < 2; ++c)
  {
    const R col = c == 0 ? midCol : rightCol;
    const R inner = col.GetReducedFromTop(22.f).GetPadded(-3.f);
    Sane(inner, name, "param block");
    std::vector<R> cells;
    for (int i = 0; i < 4; ++i)
      cells.push_back(inner.GridCell(i, 2, 2).GetPadded(-3.f));
    for (int i = 0; i < 4; ++i)
    {
      Sane(cells[(size_t) i], name, "param cell");
      /* SkParamKnob sizes its label and value strips from the cell, so 52 px still gives a
       * ~25 px dial — below that the desk reads as a grid of dots. */
      Check(cells[(size_t) i].W() >= 44.f && cells[(size_t) i].H() >= 52.f, name, "param cell too small",
            cells[(size_t) i].W(), cells[(size_t) i].H());
      for (int j = i + 1; j < 4; ++j)
        Check(!Overlaps(cells[(size_t) i], cells[(size_t) j]), name, "param cells overlap", w, h);
    }
    for (const R& cell : cells)
      Check(cell.Rr <= col.Rr + 0.01f && cell.B <= col.B + 0.01f, name, "param cell spills", w, h);
  }

  /* The ADSR strip: compact (no title row) when it is short, as AttachAdsrStrip() does. */
  const bool compact = strip.H() < 92.f;
  const R inner = strip.GetReducedFromTop(compact ? 6.f : 22.f).GetPadded(compact ? -4.f : -6.f);
  const R graph = inner.GetFromLeft(inner.W() * 0.44f).GetPadded(-2.f);
  const R knobs = inner.GetReducedFromLeft(inner.W() * 0.46f).GetPadded(-2.f);
  Sane(graph, name, "adsr graph");
  Sane(knobs, name, "adsr knobs");
  Check(!Overlaps(graph, knobs), name, "graph/knobs overlap", w, h);
  for (int i = 0; i < 4; ++i)
  {
    const R k = knobs.GridCell(i, 1, 4).GetCentredInside(54.f);
    Sane(k, name, "adsr knob");
    Check(k.H() >= 42.f, name, "adsr knob too small", k.W(), k.H());
    Check(k.B <= strip.B + 0.01f, name, "adsr knob spills", w, h);
  }
}

int main()
{
  struct D
  {
    const char* name;
    float a, b, c;
  };
  /* scope | params | params — each row sums to 1.0 (the -3 gutters make the gaps). */
  static const D desks[] = {
    {"LOFI", 0.32f, 0.30f, 0.38f},   {"SAMPLE", 0.40f, 0.28f, 0.32f},
    {"VOICE", 0.40f, 0.28f, 0.32f},  {"TRIO", 0.36f, 0.30f, 0.34f},
    {"GRAIN", 0.40f, 0.28f, 0.32f},  {"PHYS", 0.38f, 0.30f, 0.32f},
    {"NOISE", 0.40f, 0.28f, 0.32f},  {"SLICE", 0.40f, 0.28f, 0.32f},
    {"DRUM", 0.36f, 0.30f, 0.34f},
  };

  /* The hero desk body at the shipped editor size (1280x880, PanelLayout.h). */
  struct Size
  {
    const char* label;
    float w, h;
  };
  static const Size sizes[] = {{"1280x880 (shipped)", 937.4f, 239.6f},
                               {"narrow window", 860.f, 230.f},
                               {"smallest supported", 800.f, 225.f}};

  for (const D& d : desks)
    for (const Size& s : sizes)
    {
      std::printf("== %s @ %s\n", d.name, s.label);
      Desk(d.name, d.a, d.b, d.c, s.w, s.h);
    }

  if (gFail)
  {
    std::printf("\n%d layout check(s) failed\n", gFail);
    return 1;
  }
  std::printf("\nall engine desk layouts ok\n");
  return 0;
}
