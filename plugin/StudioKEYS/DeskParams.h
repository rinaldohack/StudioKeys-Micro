/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
// Engine desks — order matches firmware ENGINE_ORDER (DIGITAL omitted).
#pragma once

#include <cstring>

namespace skdesk {

struct EngineDesk
{
  const char* name;
  const char* const* params;
  int nParams;
};

static constexpr const char* kParams_ANALOG[] = {"WAVE", "DTN", "MIX", "NOIS", "CUT", "RES", "DRV", "KTR"};
static constexpr const char* kParams_FM6[] = {"ALG", "FB", "MLVL", "MRAT", "MEG", "VMOD", "DTUN", "SLOT"};
static constexpr const char* kParams_PHASE[] = {"WAVE", "WAVE2", "DCW", "ENV", "DTN", "LINE", "SUB", "-"};
static constexpr const char* kParams_LOFI[] = {"CHIP", "WAVE", "DUTY", "CRSH", "SWP", "VIB", "ARP", "TONE"};
static constexpr const char* kParams_SAMPLE[] = {"SET", "TUNE", "BITS", "LOOP", "CUT", "-", "DRV", "-"};
static constexpr const char* kParams_VOICE[] = {"VOWL", "VOWL2", "TALK", "SHIFT", "BUZZ", "BRTH", "Q", "RAND"};
static constexpr const char* kParams_TRIO[] = {"WAVE", "INT2", "INT3", "DTN", "MODE", "CUT", "RES", "PW"};
static constexpr const char* kParams_WHEEL[] = {"REG", "SUB", "BODY", "TOP", "PERC", "CLICK", "DRV", "ROTR"};
static constexpr const char* kParams_GRAIN[] = {"SRC", "POS", "SIZE", "DENS", "PTCH", "SPRD", "RAND", "TONE"};
static constexpr const char* kParams_PHYS[] = {"MODEL", "STRC", "BRIT", "DAMP", "POS", "ACC", "BOW", "EXC"};
static constexpr const char* kParams_NOISE[] = {"MODE", "COLR", "FREQ", "RES", "TRK", "DENS", "DRFT", "CRSH"};
static constexpr const char* kParams_SLICE[] = {"SRC", "DIV", "START", "PTCH", "MODE", "REV", "DCAY", "TONE"};
static constexpr const char* kParams_DRUM[] = {"KIT", "TUNE", "TONE", "DECY", "SNAP", "ACC", "KICK", "DRV"};

/* ANALOG FM6 PHASE LOFI SAMPLE VOICE TRIO WHEEL GRAIN PHYS NOISE SLICE DRUM */
static constexpr EngineDesk kEngines[] = {
  {"ANALOG", kParams_ANALOG, 8},
  {"FM6", kParams_FM6, 8},
  {"PHASE", kParams_PHASE, 8},
  {"LOFI", kParams_LOFI, 8},
  {"SAMPLE", kParams_SAMPLE, 8},
  {"VOICE", kParams_VOICE, 8},
  {"TRIO", kParams_TRIO, 8},
  {"WHEEL", kParams_WHEEL, 8},
  {"GRAIN", kParams_GRAIN, 8},
  {"PHYS", kParams_PHYS, 8},
  {"NOISE", kParams_NOISE, 8},
  {"SLICE", kParams_SLICE, 8},
  {"DRUM", kParams_DRUM, 8},
};

static constexpr int kNumEngines = (int) (sizeof(kEngines) / sizeof(kEngines[0]));

/* Settings desk is driven live via sk_host_menu_* (DISPLAY/CONTROL/AUDIO/SYSTEM). */

/* Extra automatable track params beyond E0–E7 / ADSR (DAW learn). */
static constexpr const char* kAutoExtra[] = {"LEVEL", "PAN", "VOICE", "PRIO", "ALLOC", "DETUNE"};
static constexpr int kNumAutoExtra = (int) (sizeof(kAutoExtra) / sizeof(kAutoExtra[0]));

/* Firmware track param ids (core.h enum order — keep in sync). */
enum FwParam : int
{
  kP_LEVEL = 0,
  kP_ATK = 1,
  kP_DEC = 2,
  kP_SUS = 3,
  kP_REL = 4,
  kP_VOICE = 37,
  kP_GLIDE = 38,
  kP_PAN = 39,
  kP_GLMODE = 41,
  kP_PRIO = 42,
  kP_ALLOC = 43,
  kP_DETUNE = 44,
  kP_E0 = 83,
  kP_E1 = 84,
  kP_E2 = 85,
  kP_E3 = 86,
  kP_E4 = 87,
  kP_E5 = 88,
  kP_E6 = 89,
  kP_E7 = 90,
  kP_E8 = 91,
  kP_E9 = 92,
  kP_E10 = 93,
  kP_E11 = 94,
};

inline int FindEngine(const char* name)
{
  if (!name)
    return -1;
  for (int i = 0; i < kNumEngines; ++i)
  {
    if (std::strcmp(kEngines[i].name, name) == 0)
      return i;
  }
  return -1;
}

} // namespace skdesk
