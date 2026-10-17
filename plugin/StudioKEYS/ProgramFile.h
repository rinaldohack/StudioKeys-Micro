#pragma once

/* P11 — the StudioKEYS program file (.skp).
 *
 * A plain-text, line-oriented container so a program exported from one instance can be
 * imported by any other (or by the CFW through the user bank). It carries the engine name and
 * the full parameter set, which is exactly what a user-slot record holds; FM6 programs keep
 * their DX7 voice separately (the .syx export), because an FM6 sound is not just its 91 params.
 *
 * Layout:
 *
 *   SKPG 1
 *   program ANALOG 91 Big Brass
 *   64 0 40 12 ...            (91 space-separated signed values)
 *   end
 *   program LOFI 91
 *   ...
 *   end
 *
 * `values` are the track's parameters in firmware order (engine block first, amp block last),
 * i.e. ids 0..sk_host_param_count()-1. The name is the rest of the header line and may be
 * empty; names are never quoted so spaces are fine.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace skpg {

static constexpr int kMaxParams = 160; /* firmware P_COUNT is 91; leave headroom */

struct Program
{
  std::string engine;
  std::string name;
  std::vector<int16_t> values;
};

inline std::string Encode(const std::vector<Program>& programs)
{
  std::string out = "SKPG 1\n";
  for (const auto& p : programs)
  {
    char hdr[64];
    std::snprintf(hdr, sizeof hdr, "program %s %u", p.engine.c_str(),
                  (unsigned) p.values.size());
    out += hdr;
    if (!p.name.empty())
    {
      out += ' ';
      out += p.name;
    }
    out += '\n';
    for (size_t i = 0; i < p.values.size(); ++i)
    {
      if (i)
        out += ' ';
      out += std::to_string((int) p.values[i]);
    }
    out += "\nend\n";
  }
  return out;
}

inline bool Parse(const std::string& text, std::vector<Program>& out)
{
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line))
    return false;
  if (line.rfind("SKPG 1", 0) != 0)
    return false;

  out.clear();
  while (std::getline(in, line))
  {
    if (line.empty())
      continue;
    if (line.rfind("program ", 0) != 0)
      return false;
    std::istringstream hs(line.substr(8));
    Program p;
    unsigned n = 0;
    if (!(hs >> p.engine >> n) || n > (unsigned) kMaxParams)
      return false;
    /* The name is the remainder of the line, leading space trimmed. */
    std::string rest;
    std::getline(hs, rest);
    if (!rest.empty() && rest[0] == ' ')
      rest.erase(0, 1);
    p.name = rest;

    if (!std::getline(in, line))
      return false;
    std::istringstream vs(line);
    int v = 0;
    while (vs >> v)
      p.values.push_back((int16_t) v);
    if (p.values.size() != n)
      return false;
    if (!std::getline(in, line) || line.rfind("end", 0) != 0)
      return false;
    out.push_back(std::move(p));
  }
  return !out.empty();
}

inline bool WriteFile(const std::string& path, const std::vector<Program>& programs)
{
  std::ofstream f(path, std::ios::binary);
  if (!f)
    return false;
  const std::string s = Encode(programs);
  f.write(s.data(), (std::streamsize) s.size());
  return f.good();
}

inline bool ReadFile(const std::string& path, std::vector<Program>& out)
{
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return Parse(text, out);
}

/** Raw bytes as read from disk (used to sniff SKPG vs SysEx before parsing). */
inline bool ReadBytes(const std::string& path, std::vector<uint8_t>& out)
{
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return !out.empty();
}

inline bool LooksLikeSkpg(const std::vector<uint8_t>& b)
{
  return b.size() > 5 && b[0] == 'S' && b[1] == 'K' && b[2] == 'P' && b[3] == 'G';
}

} // namespace skpg
