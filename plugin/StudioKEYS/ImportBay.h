#pragma once

#include "Librarian.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct BayRow
{
  std::string name;
  std::string engine; /* FM6 or ANALOG */
  int mono = 0;
  int glide = 0;
  int glmode = 0;
  std::vector<uint8_t> converted;
  std::vector<uint8_t> original;
};

/*
 * Import bay blobs (plugin format; BayRow::original keeps the source SysEx)
 *
 * FM6 converted — 128 bytes, firmware FM6_PACKED (eng_fm6.c / fm6_pack).
 *   Same wire layout as Yamaha DX7 packed voice. Built from FM6_INIT, then
 *   overlays DX7 name, algorithm, feedback, operators, pitch EG, LFO, transpose.
 *   DX7 voice bulk has no portamento; bay mono/glide default to 0 (optional
 *   format-0x02 function dump in the same buffer can supply bank-wide values).
 *
 * ANALOG converted — 24 bytes: 12× int16 LE
 *   [0] WAVE [1] DTN [2] MIX [3] NOIS [4] CUT [5] RES
 *   [6] DRV  [7] KTR [8] ATK [9] DEC [10] SUS [11] REL
 *   (P_E0..P_E7 + amp ADSR). DTN/KTR are bipolar (0 center = 64 on Virus).
 *
 * Virus TI / Access single-dump map (page A+B = 256 bytes)
 *   Classic B/C: F0 00 20 33 01 dd 10 bb ss [256] cs F7  → pages at +9
 *   TI ~524:     same Access id; page A at abs msg+8 (A5 porta @ 0x0D,
 *                name B112 @ 0xF8 per TI captures / waf80 + redconfetti).
 *
 *   Page-A index → ANALOG (unmapped Virus bytes → kVirusInitPages on export):
 *     A5  Portamento Time     → glide
 *     A17 Osc1 Shape          → WAVE hint
 *     A26 Osc2 Detune         → DTN (store as value−64)
 *     A28 Osc2 Sync           → WAVE=6 when set
 *     A33 Osc Balance         → MIX
 *     A37 Noise Volume        → NOIS
 *     A40 Cutoff              → CUT
 *     A42 Filter1 Resonance   → RES
 *     A46 Filter1 Keyfollow   → KTR (value−64)
 *     A49 Saturation Curve    → DRV (0..6 scaled)
 *     A59 Amp Attack          → ATK
 *     A60 Amp Decay           → DEC
 *     A61 Amp Sustain         → SUS
 *     A63 Amp Release         → REL  (A62 sustain-time skipped)
 *     A94 Key Mode            → mono (0=poly, 1..4=mono)
 *     B112..B121              → patch name
 */

/** Plugin ANALOG blob: 12× int16 LE = WAVE DTN MIX NOIS CUT RES DRV KTR ATK DEC SUS REL */
inline void AnalogBlobPack(std::vector<uint8_t>& out, const int16_t p[12])
{
  out.resize(24);
  for (int i = 0; i < 12; ++i)
  {
    out[(size_t) i * 2] = (uint8_t) (p[i] & 0xFF);
    out[(size_t) i * 2 + 1] = (uint8_t) ((p[i] >> 8) & 0xFF);
  }
}

inline bool AnalogBlobUnpack(const std::vector<uint8_t>& in, int16_t p[12])
{
  if (in.size() < 24)
    return false;
  for (int i = 0; i < 12; ++i)
    p[i] = (int16_t) (in[(size_t) i * 2] | (in[(size_t) i * 2 + 1] << 8));
  return true;
}

namespace skimport {
namespace {

constexpr uint8_t kFm6Init[128] = {
  99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0,
  0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99,
  99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0,
  2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 99, 2, 0, 99, 99, 99, 99, 50, 50, 50, 50,
  0, 8, 35, 0, 0, 0, 49, 24, 73, 78, 73, 84, 32, 86, 79, 73, 67, 69};

inline void FillVirusInitPages(uint8_t page[256])
{
  std::memset(page, 0, 256);
  page[10] = 64;
  page[17] = 64;
  page[18] = 127;
  page[21] = 32;
  page[22] = 64;
  page[23] = 127;
  page[31] = 32;
  page[33] = 64;
  page[36] = 127;
  page[40] = 90;
  page[41] = 64;
  page[42] = 30;
  page[46] = 64;
  page[47] = 64;
  page[48] = 64;
  page[55] = 64;
  page[56] = 127;
  page[57] = 64;
  page[58] = 40;
  page[59] = 10;
  page[60] = 70;
  page[61] = 90;
  page[62] = 64;
  page[63] = 60;
  page[91] = 100;
  page[93] = 64;
  static const char kInitName[10] = {'-', 'I', 'N', 'I', 'T', '-', ' ', ' ', ' ', ' '};
  std::memcpy(page + 128 + 112, kInitName, 10);
}

inline uint8_t Clamp7(int v)
{
  if (v < 0)
    return 0;
  if (v > 127)
    return 127;
  return (uint8_t) v;
}

inline int16_t Clamp16(int v, int lo, int hi)
{
  if (v < lo)
    return (int16_t) lo;
  if (v > hi)
    return (int16_t) hi;
  return (int16_t) v;
}

inline std::string TrimName(const char* nm, int n)
{
  std::string s;
  for (int i = 0; i < n; ++i)
  {
    char c = nm[i];
    if (c < 32 || c > 126)
      c = ' ';
    s.push_back(c);
  }
  while (!s.empty() && s.back() == ' ')
    s.pop_back();
  return s;
}

inline void Dx7ToFm6Packed(const uint8_t* dx7, uint8_t* out)
{
  std::memcpy(out, kFm6Init, 128);
  for (int k = 0; k < 6; ++k)
  {
    const uint8_t* s = dx7 + k * 17;
    uint8_t* d = out + k * 17;
    for (int i = 0; i < 17; ++i)
      d[i] = s[i] & 0x7F;
  }
  for (int i = 102; i < 118; ++i)
    out[i] = dx7[i] & 0x7F;
  out[110] &= 31;
  out[111] = (uint8_t) ((dx7[111] & 7) | (dx7[111] & 8));
  for (int i = 0; i < 10; ++i)
  {
    uint8_t c = dx7[118 + i] & 0x7F;
    out[118 + i] = (c < 32 || c > 126) ? (uint8_t) ' ' : c;
  }
}

inline bool FindDx7Porta(const uint8_t* data, size_t n, int* mono, int* glide)
{
  *mono = 0;
  *glide = 0;
  for (size_t i = 0; i + 8 < n; ++i)
  {
    if (data[i] != 0xF0 || data[i + 1] != 0x43)
      continue;
    const uint8_t fmt = data[i + 3];
    if (fmt == 0x09 || fmt == 0x00)
      continue;
    if (fmt == 0x02 && i + 10 < n)
    {
      size_t j = i + 4;
      while (j < n && data[j] != 0xF7)
        ++j;
      if (j > i + 6)
      {
        const uint8_t* body = data + i + 6;
        if (body + 1 < data + j)
        {
          *mono = body[0] > 0 ? 1 : 0;
          *glide = body[1] & 0x7F;
          return true;
        }
      }
    }
  }
  return false;
}

inline int VirusWaveFromPage(const uint8_t* a)
{
  if (a[28] & 1)
    return 6;
  const int shape = a[17];
  if (shape < 32)
    return 3;
  if (shape > 96)
    return 1;
  return 0;
}

inline void VirusPagesToAnalog(const uint8_t* page, int16_t an[12], int* mono, int* glide,
                               std::string* name)
{
  const uint8_t* a = page;
  const uint8_t* b = page + 128;
  an[0] = (int16_t) VirusWaveFromPage(a);
  an[1] = Clamp16((int) a[26] - 64, -64, 63);
  an[2] = a[33];
  an[3] = a[37];
  an[4] = a[40];
  an[5] = a[42];
  an[6] = Clamp7((int) a[49] * 21);
  an[7] = Clamp16((int) a[46] - 64, -64, 63);
  an[8] = a[59];
  an[9] = a[60];
  an[10] = a[61];
  an[11] = a[63];
  *mono = a[94] > 0 ? 1 : 0;
  *glide = a[5];
  *name = TrimName(reinterpret_cast<const char*>(b + 112), 10);
  if (name->empty())
    *name = "Virus TI";
}

inline void AnalogToVirusPages(const int16_t an[12], const std::string& name, int mono, int glide,
                               uint8_t page[256])
{
  FillVirusInitPages(page);
  page[5] = Clamp7(glide);
  page[17] = an[0] == 1 ? 110 : an[0] == 3 ? 16 : 64;
  page[26] = Clamp7((int) an[1] + 64);
  page[28] = an[0] == 6 ? 1 : 0;
  page[33] = Clamp7(an[2]);
  page[37] = Clamp7(an[3]);
  page[40] = Clamp7(an[4]);
  page[42] = Clamp7(an[5]);
  page[49] = (uint8_t) (Clamp7(an[6]) / 21);
  if (page[49] > 6)
    page[49] = 6;
  page[46] = Clamp7((int) an[7] + 64);
  page[59] = Clamp7(an[8]);
  page[60] = Clamp7(an[9]);
  page[61] = Clamp7(an[10]);
  page[63] = Clamp7(an[11]);
  page[94] = mono ? 1 : 0;
  char nm[10];
  std::memset(nm, ' ', 10);
  for (size_t i = 0; i < name.size() && i < 10; ++i)
  {
    char c = name[i];
    nm[i] = (c < 32 || c > 126) ? ' ' : c;
  }
  std::memcpy(page + 128 + 112, nm, 10);
}

} // namespace
} // namespace skimport

class ImportBay
{
public:
  const std::vector<BayRow>& Rows() const { return mRows; }

  /** DX7 32-voice bulk: F0 43 00 09 20 00 … F7 — signature unchanged. */
  int LoadDx7Bulk(const uint8_t* data, size_t n)
  {
    if (!data || n < 6 + 128 || data[0] != 0xF0)
      return 0;
    size_t off = 0;
    while (off + 6 < n && !(data[off] == 0xF0 && data[off + 1] == 0x43
                            && data[off + 3] == 0x09 && data[off + 4] == 0x20))
      ++off;
    if (off + 6 >= n)
      return 0;
    const uint8_t* body = data + off;
    size_t remain = n - off;
    int bankMono = 0, bankGlide = 0;
    skimport::FindDx7Porta(data, n, &bankMono, &bankGlide);
    int count = 0;
    size_t voiceOff = 6;
    for (int v = 0; v < 32 && voiceOff + 128 <= remain; ++v)
    {
      BayRow row;
      char nm[11] {};
      std::memcpy(nm, body + voiceOff + 118, 10);
      row.name = skimport::TrimName(nm, 10);
      if (row.name.empty())
        row.name = "DX7 " + std::to_string(v + 1);
      row.engine = "FM6";
      row.mono = bankMono;
      row.glide = bankGlide;
      row.glmode = 0;
      row.converted.resize(128);
      skimport::Dx7ToFm6Packed(body + voiceOff, row.converted.data());
      row.original.assign(body + voiceOff, body + voiceOff + 128);
      mRows.push_back(std::move(row));
      voiceOff += 128;
      ++count;
    }
    return count;
  }

  /** Access Virus TI / classic singles (multi-buffer OK) → ANALOG bay rows. */
  int LoadVirusTi(const uint8_t* data, size_t n)
  {
    if (!data || n < 4)
      return 0;
    int count = 0;
    size_t off = 0;
    while (off + 8 < n)
    {
      if (data[off] != 0xF0)
      {
        ++off;
        continue;
      }
      if (off + 7 >= n || data[off + 1] != 0x00 || data[off + 2] != 0x20 || data[off + 3] != 0x33)
      {
        ++off;
        continue;
      }
      size_t end = off + 1;
      while (end < n && data[end] != 0xF7)
        ++end;
      if (end >= n)
        break;
      const size_t msgLen = end - off + 1;
      if (msgLen >= 9 && data[off + 4] == 0x01 && data[off + 6] == 0x10)
      {
        const uint8_t* page = nullptr;
        if (msgLen >= 520)
        {
          if (off + 8 + 256 <= end)
            page = data + off + 8;
        }
        else if (msgLen >= 9 + 256 + 2)
        {
          page = data + off + 9;
        }
        if (page)
        {
          BayRow row;
          int16_t an[12];
          skimport::VirusPagesToAnalog(page, an, &row.mono, &row.glide, &row.name);
          row.engine = "ANALOG";
          row.glmode = 0;
          AnalogBlobPack(row.converted, an);
          row.original.assign(data + off, data + end + 1);
          mRows.push_back(std::move(row));
          ++count;
        }
      }
      off = end + 1;
    }
    if (count == 0 && data[0] == 0xF0 && data[1] == 0x00 && data[2] == 0x20 && data[3] == 0x33)
    {
      /* Short / partial Access message: one default ANALOG row (demo button). */
      BayRow row;
      row.name = "Virus TI Import";
      row.engine = "ANALOG";
      uint8_t page[256];
      skimport::FillVirusInitPages(page);
      /* If a short demo stuffed a 128-byte page after a classic header, overlay it. */
      if (n >= 9 + 128 && data[6] == 0x10)
      {
        std::memcpy(page, data + 9, 128);
        if (n >= 9 + 256)
          std::memcpy(page + 128, data + 9 + 128, 128);
      }
      int16_t an[12];
      skimport::VirusPagesToAnalog(page, an, &row.mono, &row.glide, &row.name);
      if (row.name == "-INIT-" || row.name.empty())
        row.name = "Virus TI Import";
      AnalogBlobPack(row.converted, an);
      row.original.assign(data, data + n);
      mRows.push_back(std::move(row));
      return 1;
    }
    return count;
  }

  LibPatch StoreRow(size_t i, LibKind kind) const
  {
    LibPatch p;
    if (i >= mRows.size())
      return p;
    const BayRow& r = mRows[i];
    p.name = r.name;
    p.kind = kind;
    p.engine = r.engine;
    p.mono = r.mono;
    p.glide = r.glide;
    p.glmode = r.glmode;
    p.blob = r.converted;
    p.original = r.original;
    return p;
  }

  std::vector<uint8_t> ExportDx7Voice(const LibPatch& p) const
  {
    std::vector<uint8_t> out;
    if (p.engine != "FM6")
      return out;
    uint8_t packed[128];
    if (!p.original.empty() && p.original.size() >= 128)
      std::memcpy(packed, p.original.data(), 128);
    else if (p.blob.size() >= 128)
      std::memcpy(packed, p.blob.data(), 128);
    else
      return out;
    out.push_back(0xF0);
    out.push_back(0x43);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x01);
    out.push_back(0x1B);
    out.insert(out.end(), packed, packed + 128);
    uint8_t sum = 0;
    for (size_t i = 0; i < 128; ++i)
      sum = (uint8_t) ((sum + packed[i]) & 0x7F);
    out.push_back((uint8_t) ((128 - sum) & 0x7F));
    out.push_back(0xF7);
    return out;
  }

  std::vector<uint8_t> ExportVirus(const LibPatch& p) const
  {
    if (p.engine != "ANALOG")
      return {};
    int16_t an[12] = {0, 0, 64, 0, 90, 30, 0, 0, 10, 70, 90, 60};
    AnalogBlobUnpack(p.blob, an);
    uint8_t page[256];
    skimport::AnalogToVirusPages(an, p.name, p.mono, p.glide, page);
    uint8_t device = 0x00;
    if (p.original.size() >= 7 && p.original[0] == 0xF0 && p.original[1] == 0x00
        && p.original[2] == 0x20 && p.original[3] == 0x33)
      device = p.original[5];
    std::vector<uint8_t> out;
    out.reserve(9 + 256 + 2);
    out.push_back(0xF0);
    out.push_back(0x00);
    out.push_back(0x20);
    out.push_back(0x33);
    out.push_back(0x01);
    out.push_back(device);
    out.push_back(0x10);
    out.push_back(0x00);
    out.push_back(0x40);
    out.insert(out.end(), page, page + 256);
    unsigned sum = device + 0x10 + 0x00 + 0x40;
    for (int i = 0; i < 256; ++i)
      sum += page[i];
    out.push_back((uint8_t) (sum & 0x7F));
    out.push_back(0xF7);
    return out;
  }

  void Clear() { mRows.clear(); }

private:
  std::vector<BayRow> mRows;
};
