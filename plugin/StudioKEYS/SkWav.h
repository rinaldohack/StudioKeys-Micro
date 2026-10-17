/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

/* Minimal RIFF/WAVE reader for the companion's P15 drop targets (no third-party decoder).
 * Accepts PCM 8/16/24/32, IEEE float 32/64 and WAVE_FORMAT_EXTENSIBLE (its subformat tag),
 * any channel count (mixed to mono), and tolerates a truncated data chunk (as
 * tools/sampleio.py's read_any_wav does). The host does the resample / ADPCM encode. */

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace skwav {

struct Audio
{
  uint32_t rate = 0;
  std::vector<float> mono;
};

namespace detail {

inline uint32_t U32(const uint8_t* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
inline uint16_t U16(const uint8_t* p) { return (uint16_t) ((uint16_t) p[0] | (uint16_t) p[1] << 8); }

struct Chunk
{
  size_t body = 0;
  size_t len = 0;
  bool there = false;
};

/** RIFF chunk table; the first chunk of each id wins (sampleio.wav_chunks). */
inline bool Chunks(const std::vector<uint8_t>& d, Chunk& fmt, Chunk& data, std::string& err)
{
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4))
  {
    err = "not a WAV file";
    return false;
  }
  size_t i = 12;
  while (i + 8 <= d.size())
  {
    const size_t n = U32(d.data() + i + 4);
    Chunk* slot = nullptr;
    if (!std::memcmp(d.data() + i, "fmt ", 4))
      slot = &fmt;
    else if (!std::memcmp(d.data() + i, "data", 4))
      slot = &data;
    if (slot && !slot->there)
    {
      slot->there = true;
      slot->body = i + 8;
      slot->len = n;
    }
    i += 8 + n + (n & 1u);
    if (n > d.size()) /* a bogus size: stop rather than skip past the buffer */
      break;
  }
  if (!fmt.there)
  {
    err = "WAV without a fmt chunk";
    return false;
  }
  return true;
}

inline float Sample(const uint8_t* p, uint16_t tag, uint32_t bps)
{
  if (tag == 3)
  {
    if (bps == 4)
    {
      float v;
      std::memcpy(&v, p, sizeof v);
      return v;
    }
    double v;
    std::memcpy(&v, p, sizeof v);
    return (float) v;
  }
  switch (bps)
  {
    case 1: return ((float) p[0] - 128.f) / 128.f;
    case 2:
    {
      int16_t v;
      std::memcpy(&v, p, sizeof v);
      return (float) v / 32768.f;
    }
    case 3: /* 24 bit: pad to 32 (zero low byte), exact power-of-two scale */
    {
      const uint32_t w = (uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24;
      return (float) ((int32_t) w) / 2147483648.f;
    }
    case 4:
    {
      int32_t v;
      std::memcpy(&v, p, sizeof v);
      return (float) v / 2147483648.f;
    }
    default: return 0.f;
  }
}

} // namespace detail

/** Load `path` as mono float in [-1, 1). false = err says why. */
inline bool Load(const char* path, Audio& out, std::string& err)
{
  out.rate = 0;
  out.mono.clear();
  if (!path || !*path)
  {
    err = "no file";
    return false;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f)
  {
    err = "cannot open the file";
    return false;
  }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (d.size() < 12)
  {
    err = "not a WAV file";
    return false;
  }

  detail::Chunk fmt, data;
  if (!detail::Chunks(d, fmt, data, err))
    return false;
  if (fmt.body + 16 > d.size())
  {
    err = "short fmt chunk";
    return false;
  }
  const uint8_t* fp = d.data() + fmt.body;
  uint16_t tag = detail::U16(fp);
  const uint16_t nch = detail::U16(fp + 2);
  const uint32_t rate = detail::U32(fp + 4);
  const uint16_t bits = detail::U16(fp + 14);
  if (tag == 0xFFFE && fmt.body + 26 <= d.size())
    tag = detail::U16(fp + 24); /* WAVE_FORMAT_EXTENSIBLE: the subformat's first 2 bytes */
  const uint32_t bps = bits / 8u;
  if ((tag != 1 && tag != 3) || !bps || !nch || !rate)
  {
    err = "unsupported WAV format";
    return false;
  }
  if (!data.there)
  {
    err = "WAV without audio";
    return false;
  }
  const size_t frame = (size_t) bps * nch;
  const size_t avail = data.body < d.size() ? d.size() - data.body : 0;
  const size_t nfr = (data.len < avail ? data.len : avail) / frame;
  if (!nfr)
  {
    err = "WAV without audio";
    return false;
  }

  out.rate = rate;
  out.mono.resize(nfr);
  const uint8_t* p = d.data() + data.body;
  for (size_t i = 0; i < nfr; ++i, p += frame)
  {
    float acc = 0.f;
    for (uint16_t c = 0; c < nch; ++c)
      acc += detail::Sample(p + (size_t) c * bps, tag, bps);
    out.mono[i] = acc / (float) nch;
  }
  return true;
}

/** "Kick_C4.wav" -> 60 (C4), else `def`; as tools/sampleio.note_from_name. */
inline int NoteFromName(const std::string& name, int def = 60)
{
  for (size_t i = 0; i < name.size(); ++i)
  {
    const char c = (char) std::toupper((unsigned char) name[i]);
    if (c < 'A' || c > 'G')
      continue;
    if (i && std::isalpha((unsigned char) name[i - 1]))
      continue;
    size_t j = i + 1;
    int pc = (int) (std::strchr("CDEFGAB", c) - "CDEFGAB");
    if (j < name.size() && name[j] == '#')
    {
      pc++;
      j++;
    }
    else if (j < name.size() && name[j] == 'b')
    {
      pc--;
      j++;
    }
    if (j >= name.size() || !std::isdigit((unsigned char) name[j]))
      continue;
    int oct = 0;
    while (j < name.size() && std::isdigit((unsigned char) name[j]))
      oct = oct * 10 + (name[j++] - '0');
    const int note = (oct + 1) * 12 + pc;
    return note < 0 ? 0 : (note > 127 ? 127 : note);
  }
  return def;
}

} // namespace skwav
