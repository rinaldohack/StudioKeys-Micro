/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "DeskParams.h"
#include "sk_host.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/** Hardware-ready vs VST-only libraries (plan phase 7). */
enum class LibKind { HardwareReady, VstOnly };

inline const char* LibKindName(LibKind k)
{
  return k == LibKind::HardwareReady ? "Hardware-ready" : "VST-only";
}

struct LibPatch
{
  std::string name;
  LibKind kind = LibKind::HardwareReady;
  std::string engine; /* ANALOG, FM6, … */
  int mono = 0;       /* P_VOICE */
  int glide = 0;
  int glmode = 0;
  int slot = -1;                 /* user slot 0..89 when from NOR, else -1 */
  std::vector<uint8_t> blob;     /* LE int16[P_COUNT] when from NOR */
  std::vector<uint8_t> original; /* DX7 / Virus dump if imported */
};

/* NOR flash map (firmware/hal/smk_map.h + storage.c). Every object has an A/B sector pair; the copy with the
 * newer seq wins. The map is no longer one run of pairs: the five high user-preset banks and the three high
 * FM6 patch banks sit above the user samples (P8c), so the object addresses are a table. */
namespace sknor {

static constexpr uint32_t kSector = 4096u;
static constexpr uint32_t kPayloadOff = 256u;
static constexpr uint32_t kPayloadMax = kSector - kPayloadOff;
static constexpr uint32_t kStMagic = 0x554C4546u; /* "FELU" */
static constexpr uint32_t kUpBankMagic = 0x31425055u; /* "UPB1" */
static constexpr uint32_t kSetBase = 0x000C2000u;
static constexpr uint32_t kProjBase = 0x000C4000u;
static constexpr uint32_t kPreBase = 0x000CC000u; /* OBJ_UPRESET0/1 A/B */
static constexpr uint32_t kFm6A = 0x000D0000u;
static constexpr uint32_t kFm6B = 0x000D1000u;
static constexpr uint32_t kMixBase = 0x000D2000u;
static constexpr uint32_t kPreHi = 0x000EE000u; /* OBJ_UPRESET2..6 A/B */
static constexpr uint32_t kUpfBase = 0x000F8000u; /* OBJ_UPFM6_1..3 A/B */
static constexpr uint32_t kObjSettings = 0u;
static constexpr uint32_t kObjProject0 = 1u;
static constexpr uint32_t kObjUpreset0 = kObjProject0 + 4u; /* 5 */
static constexpr uint32_t kUpPerBank = 16u;
static constexpr uint32_t kUpBanks = 7u; /* object stride: OBJ_UPRESET0..6, so OBJ_FM6BANK stays kObjUpreset0+7.
                                              * Firmware keeps the 7th object allocated but never addresses it at
                                              * 90 slots; scanning it is harmless (its slots are >= kUpSlots). */
static constexpr uint32_t kUpSlots = 90u; /* core.h UP_SLOTS: the addressable ceiling (6 PERFORM pages x 15) */
static constexpr uint32_t kObjFm6Bank = kObjUpreset0 + kUpBanks; /* 12 (retired; OBJ_UPFM6_0 shares its pair) */
static constexpr uint32_t kObjUpfm60 = kObjFm6Bank + 1u; /* 13 */
static constexpr uint32_t kUpRecSize = 192u;
static constexpr uint32_t kUpUsed = 0xA5u;
static constexpr uint32_t kUpVerGrid = 5u;
static constexpr uint32_t kUpPmax = 72u;
static constexpr uint32_t kPCount = 95u; /* core.h P_COUNT: P_E0..P_E11 are the last 12 */
static constexpr uint32_t kPE0 = 83u;
static constexpr uint32_t kNEngines = 14u; /* with SLICE */

/* storage.c st_addr[]: object id -> sector offset of copy A */
static constexpr uint32_t kObjAddr[] = {
  kSetBase,                                        /* OBJ_SETTINGS */
  kProjBase, kProjBase + 2u * kSector,             /* OBJ_PROJECT0..3 */
  kProjBase + 4u * kSector, kProjBase + 6u * kSector,
  kPreBase, kPreBase + 2u * kSector,                /* OBJ_UPRESET0/1 */
  kPreHi, kPreHi + 2u * kSector, kPreHi + 4u * kSector, /* OBJ_UPRESET2..6 */
  kPreHi + 6u * kSector, kPreHi + 8u * kSector,
  kFm6A,                                            /* OBJ_FM6BANK */
  kFm6A,                                            /* OBJ_UPFM6_0 (the bank's pair; told apart by the type) */
  kUpfBase, kUpfBase + 2u * kSector,                /* OBJ_UPFM6_1..3 */
  kUpfBase + 4u * kSector,
};


#pragma pack(push, 1)
struct StHdr
{
  uint32_t magic;
  uint16_t type, slot;
  uint32_t seq, len, crc, rsv[2];
  uint32_t hcrc;
};
struct UpRec
{
  uint8_t used, ver, engine, np;
  char name[12];
  union
  {
    int16_t p[kUpPmax];
    uint8_t packed[kUpPmax * 2u];
  };
  uint8_t note[16], flags[16];
};
struct UpBank
{
  uint32_t magic;
  uint16_t rsize, nslot;
  UpRec r[kUpPerBank];
};
#pragma pack(pop)

static_assert(sizeof(StHdr) == 32, "storage commit record");
static_assert(sizeof(UpRec) == 192, "user preset record");

inline uint32_t Crc32(const void* p, uint32_t n)
{
  static const uint32_t T[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
  const uint8_t* b = (const uint8_t*) p;
  uint32_t c = 0xFFFFFFFFu;
  while (n--)
  {
    c ^= *b++;
    c = (c >> 4) ^ T[c & 15u];
    c = (c >> 4) ^ T[c & 15u];
  }
  return ~c;
}

inline uint32_t SectorOf(uint32_t obj, uint32_t copy)
{
  const uint32_t n = (uint32_t) (sizeof kObjAddr / sizeof kObjAddr[0]);
  return obj < n ? kObjAddr[obj] + copy * kSector : 0u;
}

inline const char* FwEngineName(uint8_t e)
{
  static const char* kNames[] = {
    "ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL",
    "GRAIN", "PHYS", "DRUM", "NOISE", "FM6", "SLICE"};
  if (e == 1u) /* DIGITAL → played as FM6 */
    return "FM6";
  if (e < sizeof(kNames) / sizeof(kNames[0]))
    return kNames[e];
  return "ANALOG";
}

inline int16_t RecValue(const UpRec& r, uint32_t k)
{
  if (r.ver >= 4u)
    return (int16_t) r.packed[k] - 64;
  return r.p[k];
}

inline bool RecValid(const UpRec& r)
{
  if (!(r.used == kUpUsed && r.ver >= 1u && r.ver <= kUpVerGrid && r.engine < kNEngines &&
        r.np >= 8u && r.np <= (r.ver >= 4u ? kUpPmax * 2u : kUpPmax) && r.name[0]))
    return false;
  if (r.ver >= 4u)
  {
    for (uint32_t i = 0; i < r.np; ++i)
      if (r.packed[i] > 191u)
        return false;
  }
  return true;
}

/** Map a stored record's parameters onto the host's fixed layout.
 *
 *  The record stores the engine block LAST, and its size is the firmware's NE, not a constant: the VST has to
 *  read the same engine params the keyboard wrote. NE is 12 (P_E0..P_E11, the WHEEL's nine drawbars plus
 *  REG/DRV/VIB); this file said 8, so every engine param of a NOR row landed four slots early - E0 got E4's
 *  value, and E8..E11 were dropped entirely. NE is now derived from kPCount - kPE0, so the two cannot drift
 *  apart again: raise kPCount in step with core.h's P_COUNT and this follows.
 *
 *  np > kPCount is a record from newer firmware: its engine block is still the last kNE, which is what keeps
 *  this readable instead of guessing. */
inline void ParamsByCount(int16_t* out, const int16_t* in, uint32_t np)
{
  const uint32_t ne = kPCount - kPE0;          /* the engine block's size (12: P_E0..P_E11) */
  const uint32_t nc = np > ne ? np - ne : 0u;  /* firmware params before the engine block */
  for (uint32_t i = 0; i < kPE0; ++i)
    out[i] = i < nc ? in[i] : 0;
  for (uint32_t i = 0; i < ne; ++i)
    out[kPE0 + i] = i < np ? in[nc + i] : 0;
}

inline bool HeadOk(const uint8_t* nor, uint32_t norSize, uint32_t obj, uint32_t copy, StHdr& h)
{
  const uint32_t base = SectorOf(obj, copy);
  if (!nor || base + sizeof(StHdr) > norSize)
    return false;
  std::memcpy(&h, nor + base, sizeof h);
  if (h.magic != kStMagic || h.type != obj || h.slot != copy || h.len > kPayloadMax)
    return false;
  if (h.hcrc != Crc32(&h, sizeof(StHdr) - 4u))
    return false;
  return true;
}

inline bool BodyOk(const uint8_t* nor, uint32_t norSize, uint32_t obj, uint32_t copy, const StHdr& h,
                   uint8_t* dst)
{
  const uint32_t base = SectorOf(obj, copy) + kPayloadOff;
  if (!nor || !dst || base + h.len > norSize)
    return false;
  std::memcpy(dst, nor + base, h.len);
  return Crc32(dst, h.len) == h.crc;
}

/** Newest valid A/B payload for obj → dst; returns length or 0. */
inline uint32_t LoadObject(const uint8_t* nor, uint32_t norSize, uint32_t obj, uint8_t* dst, uint32_t max)
{
  StHdr a {}, b {};
  const bool va = HeadOk(nor, norSize, obj, 0, a);
  const bool vb = HeadOk(nor, norSize, obj, 1, b);
  auto tryCopy = [&](uint32_t copy, const StHdr& h) -> uint32_t {
    if (h.len > max)
      return 0;
    if (!BodyOk(nor, norSize, obj, copy, h, dst))
      return 0;
    return h.len;
  };
  if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u)))
  {
    if (uint32_t n = tryCopy(1, b))
      return n;
  }
  if (va)
  {
    if (uint32_t n = tryCopy(0, a))
      return n;
  }
  if (vb)
  {
    if (uint32_t n = tryCopy(1, b))
      return n;
  }
  return 0;
}

inline std::string RecName(const UpRec& r)
{
  char b[13];
  uint32_t i;
  for (i = 0; i < 12u && r.name[i]; ++i)
    b[i] = r.name[i] >= 'a' && r.name[i] <= 'z' ? (char) (r.name[i] - 32) : r.name[i];
  b[i] = 0;
  return std::string(b);
}

} // namespace sknor

class Librarian
{
public:
  std::vector<LibPatch>& Hw() { return mHw; }
  std::vector<LibPatch>& Vst() { return mVst; }
  const std::vector<LibPatch>& Hw() const { return mHw; }
  const std::vector<LibPatch>& Vst() const { return mVst; }

  void Add(LibPatch p)
  {
    if (p.kind == LibKind::VstOnly)
      mVst.push_back(std::move(p));
    else
      mHw.push_back(std::move(p));
  }

  bool CanSendToKeyboard(const LibPatch& p) const
  {
    return p.kind == LibKind::HardwareReady;
  }

  void SeedDemo()
  {
    if (!mHw.empty() || !mVst.empty())
      return;
    LibPatch a;
    a.name = "Init Analog";
    a.engine = "ANALOG";
    a.kind = LibKind::HardwareReady;
    Add(a);
    LibPatch b;
    b.name = "Wide FM6";
    b.engine = "FM6";
    b.kind = LibKind::VstOnly;
    b.glide = 24;
    b.glmode = 1;
    b.mono = 1;
    Add(b);
    /* Phase 7: two FM6 patches with different glide times. */
    LibPatch c;
    c.name = "Tight FM6";
    c.engine = "FM6";
    c.kind = LibKind::HardwareReady;
    c.glide = 6;
    c.glmode = 0;
    c.mono = 1;
    Add(c);
  }

  /** Scan NOR user-preset banks into Hw(); clear Hw first. The keyboard holds 100 (7 banks, the last one
   *  4 records); the VST library itself has no cap - a row that is dragged beyond the hardware's 100 stays in
   *  the VST-only side, and the first 90 are what syncs (kUpSlots; the 7th bank is scanned but its
   *  slots are above the ceiling, so they never become HardwareReady rows). */
  void ScanNorIntoHw()
  {
    mHw.clear();
    const uint8_t* nor = Nor();
    const uint32_t nsz = NorSize();
    if (!nor || nsz < sknor::kPreBase + 2u * sknor::kSector)
      return;

    std::vector<uint8_t> payload(sknor::kPayloadMax);
    for (uint32_t bank = 0; bank < sknor::kUpBanks; ++bank)
    {
      const uint32_t len =
        sknor::LoadObject(nor, nsz, sknor::kObjUpreset0 + bank, payload.data(), (uint32_t) payload.size());
      if (len != sizeof(sknor::UpBank))
        continue;
      sknor::UpBank bk {};
      std::memcpy(&bk, payload.data(), sizeof bk);
      if (bk.magic != sknor::kUpBankMagic || bk.rsize != sizeof(sknor::UpRec) || bk.nslot != sknor::kUpPerBank)
        continue;
      for (uint32_t i = 0; i < sknor::kUpPerBank; ++i)
      {
        const uint32_t slot = bank * sknor::kUpPerBank + i;
        if (slot >= sknor::kUpSlots)
          break;
        const sknor::UpRec& r = bk.r[i];
        if (!sknor::RecValid(r))
          continue;
        int16_t raw[sknor::kUpPmax * 2u] = {};
        int16_t mapped[sknor::kPCount] = {};
        for (uint32_t k = 0; k < r.np && k < sknor::kUpPmax * 2u; ++k)
          raw[k] = sknor::RecValue(r, k);
        sknor::ParamsByCount(mapped, raw, r.np);

        LibPatch p;
        p.name = sknor::RecName(r);
        p.kind = LibKind::HardwareReady;
        p.engine = sknor::FwEngineName(r.engine);
        p.mono = mapped[skdesk::kP_VOICE];
        p.glide = mapped[skdesk::kP_GLIDE];
        p.glmode = mapped[skdesk::kP_GLMODE];
        p.slot = (int) slot;
        p.blob.resize(sknor::kPCount * sizeof(int16_t));
        std::memcpy(p.blob.data(), mapped, p.blob.size());
        mHw.push_back(std::move(p));
      }
    }
  }

  /** Rebuild libraries from NOR; SeedDemo only when no user presets found. */
  void BootLibraries()
  {
    mHw.clear();
    mVst.clear();
    ScanNorIntoHw();
    if (mHw.empty())
      SeedDemo();
  }

  /** Apply per-patch voice/glide and desk engine params onto the host.
   *  NOR rows: LE int16[P_COUNT]. Import-bay ANALOG: 12× int16 LE
   *  (E0..E7 + ATK/DEC/SUS/REL). FM6 bay packed (128 B) stays for export. */
  static bool LoadIntoHost(const LibPatch& p)
  {
    sk_host_set_param((uint32_t) skdesk::kP_VOICE, p.mono);
    sk_host_set_param((uint32_t) skdesk::kP_GLIDE, p.glide);
    sk_host_set_param((uint32_t) skdesk::kP_GLMODE, p.glmode);

    int16_t mapped[sknor::kPCount] = {};
    bool have = false;
    if (p.blob.size() >= sknor::kPCount * sizeof(int16_t))
    {
      std::memcpy(mapped, p.blob.data(), sizeof mapped);
      have = true;
    }
    else if (p.engine == "ANALOG" && p.blob.size() >= 24)
    {
      /* ImportBay AnalogBlob: WAVE DTN MIX NOIS CUT RES DRV KTR ATK DEC SUS REL */
      int16_t an[12] = {};
      for (int i = 0; i < 12; ++i)
        an[i] = (int16_t) (p.blob[(size_t) i * 2] | (p.blob[(size_t) i * 2 + 1] << 8));
      for (int i = 0; i < 8; ++i)
        mapped[skdesk::kP_E0 + i] = an[i];
      mapped[skdesk::kP_ATK] = an[8];
      mapped[skdesk::kP_DEC] = an[9];
      mapped[skdesk::kP_SUS] = an[10];
      mapped[skdesk::kP_REL] = an[11];
      have = true;
    }
    if (have)
    {
      sk_host_set_param((uint32_t) skdesk::kP_ATK, mapped[skdesk::kP_ATK]);
      sk_host_set_param((uint32_t) skdesk::kP_DEC, mapped[skdesk::kP_DEC]);
      sk_host_set_param((uint32_t) skdesk::kP_SUS, mapped[skdesk::kP_SUS]);
      sk_host_set_param((uint32_t) skdesk::kP_REL, mapped[skdesk::kP_REL]);
      for (int i = 0; i < 8; ++i)
        sk_host_set_param((uint32_t) (skdesk::kP_E0 + i), mapped[skdesk::kP_E0 + i]);
    }
    return true;
  }

  static uint8_t* Nor() { return sk_host_nor(); }
  static uint32_t NorSize() { return sk_host_nor_size(); }

  static bool NorRead(uint32_t offset, void* dst, uint32_t n)
  {
    uint8_t* nor = Nor();
    if (!nor || !dst || offset + n > NorSize())
      return false;
    std::memcpy(dst, nor + offset, n);
    return true;
  }

  static bool NorWrite(uint32_t offset, const void* src, uint32_t n)
  {
    uint8_t* nor = Nor();
    if (!nor || !src || offset + n > NorSize())
      return false;
    std::memcpy(nor + offset, src, n);
    return true;
  }

  size_t Selected() const { return mSel; }
  void Select(size_t i)
  {
    const size_t n = mHw.size() + mVst.size();
    if (n)
      mSel = i % n;
  }
  void SelectNext()
  {
    const size_t n = mHw.size() + mVst.size();
    if (n)
      mSel = (mSel + 1) % n;
  }
  void SelectPrev()
  {
    const size_t n = mHw.size() + mVst.size();
    if (!n)
      return;
    mSel = mSel == 0 ? n - 1 : mSel - 1;
  }
  const LibPatch* SelectedPatch() const
  {
    if (mSel < mHw.size())
      return &mHw[mSel];
    const size_t j = mSel - mHw.size();
    if (j < mVst.size())
      return &mVst[j];
    return nullptr;
  }

private:
  std::vector<LibPatch> mHw;
  std::vector<LibPatch> mVst;
  size_t mSel = 0;
};
