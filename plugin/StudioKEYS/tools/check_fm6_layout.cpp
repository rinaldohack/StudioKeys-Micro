/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
// Layout verifier for skfm6::AttachFm6Desk (plugin/StudioKEYS/Fm6Desk.h).
// Re-implements the desk's IRECT math with the exact iPlug2 helpers it uses and
// asserts that no two control rectangles overlap, and that everything stays
// inside the desk body. Build/run: tools/check_fm6_layout.sh
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct R
{
  float L, T, Rr, B;
  float W() const { return Rr - L; }
  float H() const { return B - T; }
  float MW() const { return 0.5f * (L + Rr); }
  float MH() const { return 0.5f * (T + B); }
  R GetPadded(float p) const { return {L - p, T - p, Rr + p, B + p}; }
  R GetFromTop(float a) const { return {L, T, Rr, T + a}; }
  R GetFromBottom(float a) const { return {L, B - a, Rr, B}; }
  R GetFromRight(float a) const { return {Rr - a, T, Rr, B}; }
  R GetFromLeft(float a) const { return {L, T, L + a, B}; }
  R GetReducedFromTop(float a) const { return {L, T + a, Rr, B}; }
  R GetReducedFromBottom(float a) const { return {L, T, Rr, B - a}; }
  R GetReducedFromRight(float a) const { return {L, T, Rr - a, B}; }
  R GetMidVPadded(float p) const { return {L, MH() - p, Rr, MH() + p}; }
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
  R GridCell(int row, int col, int nr, int nc) const { return SubV(nr, row).SubH(nc, col); }
  R GridCell(int idx, int nr, int nc) const
  {
    const int row = idx / nc, col = idx % nc;
    return SubV(nr, row).SubH(nc, col);
  }
};

struct Ctrl
{
  std::string name;
  R rect;
};

static std::vector<Ctrl> g;

static void Add(const char* name, R r) { g.push_back({name, r}); }

static R Stacked(const R& a, int i, int n)
{
  const float rh = a.H() / (float) std::max(1, n);
  return {a.L, a.T + rh * i, a.Rr, a.T + rh * (i + 1)};
}

static bool Overlap(const R& a, const R& b)
{
  const float iw = std::min(a.Rr, b.Rr) - std::max(a.L, b.L);
  const float ih = std::min(a.B, b.B) - std::max(a.T, b.T);
  return iw > 0.5f && ih > 0.5f; // >0.5px counts as a real overlap
}

static bool IsPanel(const std::string& n)
{
  return n == "pegPanel" || n == "lfoPanel" || n == "voicePanel";
}

static bool IsKnob(const std::string& n)
{
  return n.rfind("r0k", 0) == 0 || n.rfind("r1k", 0) == 0 || n.rfind("r2k", 0) == 0 ||
         n.rfind("r3k", 0) == 0 || n.rfind("pr", 0) == 0 || n.rfind("pl", 0) == 0 || n.rfind("lfoA", 0) == 0 ||
         n.rfind("lfoB", 0) == 0 || n.rfind("glob", 0) == 0;
}

static void GridRow(const R& row, int n, float pad, const char* tag)
{
  for (int i = 0; i < n; ++i)
    Add((std::string(tag) + std::to_string(i)).c_str(), row.GridCell(i, 1, n).GetPadded(-pad));
}

// Mirror of AttachFm6Desk's geometry.
static void Build(float W, float H, bool verbose)
{
  g.clear();
  const R body {0, 0, W, H};
  const float c0w = W * 0.26f, c1w = W * 0.44f;
  const R c0 {body.L, body.T, body.L + c0w, body.B};
  const R c1 {body.L + c0w, body.T, body.L + c0w + c1w, body.B};
  const R c2 {body.L + c0w + c1w, body.T, body.Rr, body.B};

  { // left column
    const R col = c0.GetPadded(-2.f);
    const float algH = col.H() * 0.37f, opsH = col.H() * 0.42f;
    const R algArea {col.L, col.T, col.Rr, col.T + algH};
    Add("alg", algArea.GetPadded(-2.f));
    const R sel {col.L, algArea.B + 4.f, col.Rr, algArea.B + 4.f + opsH};
    for (int op = 0; op < 6; ++op)
      Add(("opsel" + std::to_string(op)).c_str(), sel.GridCell(op, 2, 3).GetPadded(-3.f));
    const R grow = col.GetFromBottom(col.H() * 0.18f).GetPadded(-1.f);
    GridRow(grow, 3, 3.f, "glob");
  }
  { // middle column
    const R col = c1.GetPadded(-3.f);
    const R hdr = col.GetFromTop(22.f);
    const float mw = std::min(150.f, hdr.W() * 0.42f);
    Add("header", hdr.GetReducedFromRight(mw));
    Add("mode", hdr.GetFromRight(mw).GetPadded(-2.f));
    const R rest = col.GetReducedFromTop(22.f + 4.f);
    const R eg = rest.GetFromTop(rest.H() * 0.30f).GetPadded(-1.f);
    Add("eg", eg);
    const R grid = rest.GetReducedFromTop(eg.GetPadded(1.f).H() + 4.f);
    const float rowH = grid.H() * 0.25f;
    const R r0 {grid.L, grid.T, grid.Rr, grid.T + rowH};
    const R r1 {grid.L, grid.T + rowH, grid.Rr, grid.T + rowH * 2.f};
    const R r2 {grid.L, grid.T + rowH * 2.f, grid.Rr, grid.T + rowH * 3.f};
    const R r3 {grid.L, grid.T + rowH * 3.f, grid.Rr, grid.B};
    GridRow(r0.GetPadded(-1.f), 8, 2.f, "r0k");
    GridRow(r1.GetPadded(-1.f), 4, 2.f, "r1k");
    GridRow(r2.GetPadded(-1.f), 4, 2.f, "r2k");
    GridRow(r3.GetPadded(-1.f), 4, 2.f, "r3k");
  }
  { // right column
    const R col = c2.GetPadded(-3.f);
    const R peg = col.GetFromTop(col.H() * 0.36f);
    const R info = col.GetFromBottom(col.H() * 0.22f);
    const R lfo = col.GetReducedFromTop(peg.H() + 4.f).GetReducedFromBottom(info.H() + 4.f);
    Add("pegPanel", peg);
    Add("lfoPanel", lfo);
    Add("voicePanel", info);
    {
      const R in = peg.GetReducedFromTop(15.f).GetPadded(-4.f);
      const R gph = in.GetFromLeft(in.W() * 0.40f).GetPadded(-2.f).GetMidVPadded(in.H() * 0.34f);
      Add("pegGraph", gph);
      const R kn = in.GetFromRight(in.W() * 0.58f);
      GridRow(Stacked(kn, 0, 2).GetPadded(-1.f), 4, 2.f, "pr");
      GridRow(Stacked(kn, 1, 2).GetPadded(-1.f), 4, 2.f, "pl");
    }
    {
      const R in = lfo.GetReducedFromTop(15.f).GetPadded(-4.f);
      GridRow(Stacked(in, 0, 2).GetPadded(-1.f), 3, 2.f, "lfoA");
      GridRow(Stacked(in, 1, 2).GetPadded(-1.f), 3, 2.f, "lfoB");
    }
    {
      const R in = info.GetReducedFromTop(15.f).GetPadded(-4.f);
      Add("name", in.GetFromTop(in.H() * 0.5f).GetPadded(-2.f));
      const R tog = in.GetFromBottom(in.H() * 0.5f).GetPadded(-2.f);
      Add("lks", tog.GridCell(0, 1, 2).GetPadded(-3.f));
      Add("oks", tog.GridCell(1, 1, 2).GetPadded(-3.f));
    }
  }

  if (verbose)
  {
    printf("body %.1fx%.1f -> %zu controls\n", W, H, g.size());
    printf("  c0[%.0f..%.0f] c1[%.0f..%.0f] c2[%.0f..%.0f]\n", c0.L, c0.Rr, c1.L, c1.Rr, c2.L, c2.Rr);
    for (const auto& c : g)
      if (c.name.rfind("pr", 0) == 0 || c.name == "pegPanel")
        printf("   %-9s [%.1f,%.1f %.1f,%.1f] %.1fx%.1f\n",
               c.name.c_str(), c.rect.L, c.rect.T, c.rect.Rr, c.rect.B, c.rect.W(), c.rect.H());
  }
}

static int Check(float W, float H, bool verbose)
{
  Build(W, H, verbose);
  int fails = 0;

  // 1. nothing escapes the body
  for (const auto& c : g)
  {
    if (c.rect.L < -0.5f || c.rect.T < -0.5f || c.rect.Rr > W + 0.5f || c.rect.B > H + 0.5f)
    {
      printf("  OUT OF BOUNDS %-10s [%.1f,%.1f %.1f,%.1f] body %.0fx%.0f\n",
             c.name.c_str(), c.rect.L, c.rect.T, c.rect.Rr, c.rect.B, W, H);
      ++fails;
    }
    if (c.rect.W() <= 1.f || c.rect.H() <= 1.f)
    {
      printf("  DEGENERATE   %-10s %.1fx%.1f\n", c.name.c_str(), c.rect.W(), c.rect.H());
      ++fails;
    }
  }

  // 2. Two controls must not overlap unless it is a panel containing its own
  //    children. Group membership is by name prefix (the desk's own modules).
  auto Group = [](const std::string& n) -> int {
    if (n == "alg" || n.rfind("opsel", 0) == 0 || n.rfind("glob", 0) == 0) return 0;
    if (n == "header" || n == "mode" || n == "eg" ||
        n.rfind("r0k", 0) == 0 || n.rfind("r1k", 0) == 0 || n.rfind("r2k", 0) == 0 ||
        n.rfind("r3k", 0) == 0) return 1;
    if (n == "pegPanel" || n == "pegGraph" || n.rfind("pr", 0) == 0 || n.rfind("pl", 0) == 0) return 2;
    if (n == "lfoPanel" || n.rfind("lfoA", 0) == 0 || n.rfind("lfoB", 0) == 0) return 3;
    return 4; // voice panel + name/lks/oks
  };

  for (size_t i = 0; i < g.size(); ++i)
    for (size_t j = i + 1; j < g.size(); ++j)
    {
      if (!Overlap(g[i].rect, g[j].rect))
        continue;
      const std::string& ni = g[i].name;
      const std::string& nj = g[j].name;
      const bool panelContain = (IsPanel(ni) && Group(ni) == Group(nj) && IsPanel(ni)) ||
                                (IsPanel(nj) && Group(ni) == Group(nj));
      if (panelContain)
        continue; // panel background under its own child
      printf("  OVERLAP      %s <-> %s\n", ni.c_str(), nj.c_str());
      ++fails;
    }

  // 2b. Every panel must fully contain its own children.
  for (size_t i = 0; i < g.size(); ++i)
  {
    if (!IsPanel(g[i].name))
      continue;
    for (size_t j = 0; j < g.size(); ++j)
    {
      if (i == j || !(Group(g[i].name) == Group(g[j].name)))
        continue;
      const R& pan = g[i].rect;
      const R& kid = g[j].rect;
      if (kid.L < pan.L - 0.5f || kid.T < pan.T - 0.5f || kid.Rr > pan.Rr + 0.5f || kid.B > pan.B + 0.5f)
      {
        printf("  ESCAPES      %s from %s\n", g[j].name.c_str(), g[i].name.c_str());
        ++fails;
      }
    }
  }

  // 3. a knob must be able to show a readable dial: after its (adaptive) label
  //    strip the dial side (0.96 * min side) needs to be >= 16px. Below that the
  //    knob hides itself, so report it (the desk is unusable at that size).
  for (const auto& c : g)
  {
    if (!IsKnob(c.name))
      continue;
    const float labH = std::min(11.f, c.rect.H() * 0.34f);
    const float dial = std::min(c.rect.W(), c.rect.H() - labH) * 0.96f;
    if (dial < 16.f)
    {
      printf("  TOO SMALL    %-10s dial %.1f (cell %.1fx%.1f)\n",
             c.name.c_str(), dial, c.rect.W(), c.rect.H());
      ++fails;
    }
  }
  return fails;
}

int main()
{
  const float sizes[][2] = {
      {1200.f, 320.f}, // a future resizable window (native is 1280x880 -> 937x248)
      {937.f, 248.f}};
  int fails = 0;
  for (const auto& s : sizes)
  {
    printf("== %gx%g ==\n", s[0], s[1]);
    fails += Check(s[0], s[1], true);
  }
  if (fails)
  {
    printf("FAIL: %d layout problems\n", fails);
    return 1;
  }
  printf("PASS: no overlaps, all inside bounds, all knobs readable\n");
  return 0;
}
