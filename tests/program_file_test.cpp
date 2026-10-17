/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* P11 — the StudioKEYS program file (.skp) round-trip.
 *
 * The companion exports a program as text and imports it back into a user slot, so the file has
 * to carry the engine name and every parameter value losslessly, reject damaged input, and
 * survive names with spaces. Build: c++ -std=c++17 -Iplugin/StudioKEYS tests/program_file_test.cpp
 */
#include "ProgramFile.h"

#include <cstdio>
#include <string>
#include <vector>

static int fails;

static void check(const char* what, bool ok)
{
  std::printf("skp: %-62s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++fails;
}

int main()
{
  /* 1. one program, a name with spaces, negative and positive values. */
  {
    skpg::Program p;
    p.engine = "ANALOG";
    p.name = "Big Brass Lead";
    for (int i = 0; i < 91; ++i)
      p.values.push_back((int16_t) ((i % 2) ? 64 - i : i - 40));

    std::vector<skpg::Program> in {p};
    const std::string text = skpg::Encode(in);
    check("header", text.rfind("SKPG 1\n", 0) == 0);

    std::vector<skpg::Program> out;
    check("parses", skpg::Parse(text, out) && out.size() == 1);
    check("engine kept", out.size() == 1 && out[0].engine == "ANALOG");
    check("name with spaces kept", out.size() == 1 && out[0].name == "Big Brass Lead");
    check("all values kept bit-for-bit", out.size() == 1 && out[0].values == p.values);
  }

  /* 2. a bank of several programs, an empty name, and a trailing newline. */
  {
    std::vector<skpg::Program> in;
    const char* engines[] = {"LOFI", "SAMPLE", "VOICE", "TRIO", "GRAIN", "PHYS", "NOISE", "SLICE", "DRUM"};
    for (const char* e : engines)
    {
      skpg::Program p;
      p.engine = e;
      p.values.assign(91, 7);
      in.push_back(p);
    }
    std::vector<skpg::Program> out;
    check("nine engines parse", skpg::Parse(skpg::Encode(in), out) && out.size() == 9);
    bool all = out.size() == 9;
    for (size_t i = 0; i < out.size() && all; ++i)
      all = out[i].engine == engines[i] && out[i].name.empty() && out[i].values.size() == 91;
    check("engines and empty names kept in order", all);
  }

  /* 3. damaged input is refused, never half-parsed. */
  {
    std::vector<skpg::Program> out;
    check("wrong magic refused", !skpg::Parse("NOPE 1\nprogram ANALOG 8\n1 2 3\nend\n", out));
    check("wrong magic leaves the list empty", out.empty());
    check("bad version refused", !skpg::Parse("SKPG 2\nprogram ANALOG 8\n1 2 3\nend\n", out));
    check("value count mismatch refused",
          !skpg::Parse("SKPG 1\nprogram ANALOG 8\n1 2 3\nend\n", out));
    check("missing end refused", !skpg::Parse("SKPG 1\nprogram ANALOG 8\n1 2 3 4 5 6 7 8\n", out));
    check("unknown key refused", !skpg::Parse("SKPG 1\npatch ANALOG 8\n1 2 3 4 5 6 7 8\nend\n", out));
    check("empty file refused", !skpg::Parse("", out));
  }

  /* 4. a file written to disk reads back identically. */
  {
    skpg::Program p;
    p.engine = "FM6";
    p.name = "E.PIANO 1";
    p.values.assign(91, -12);
    const char* path = "/tmp/skpg_roundtrip.skp";
    check("writes", skpg::WriteFile(path, {p}));
    std::vector<skpg::Program> out;
    check("reads back", skpg::ReadFile(path, out) && out.size() == 1 &&
                          out[0].engine == "FM6" && out[0].name == "E.PIANO 1" &&
                          out[0].values == p.values);
    std::remove(path);
  }

  std::printf("%s\n", fails ? "PROGRAM FILE TEST FAILED" : "program file test passed");
  return fails != 0;
}
