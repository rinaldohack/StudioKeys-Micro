/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* P18: the companion's undo/redo (sk_host_undo_*), the CFW's SAVE-held swap on the selected
 * track's sound:
 *   - empty stacks at boot, undo/redo doing nothing there
 *   - a program load is one step: undo restores engine/preset/parameters, redo re-applies it
 *   - a knob sweep is one step, a later sweep is another (the coalescing on fm1_ms)
 *   - the mix (LEVEL/PAN/MUTE) and the performance params stay out of the swap
 *   - the FM6 patch and the track's motion pool travel with the sound
 *   - the step remembers its track (undo on T1 brings the T3 step back to T3)
 *   - the stack is bounded (SK_UNDO_DEPTH) and redo only lives until the next edit
 * Built by tests/run_tests.sh against host/sk_host.c. */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sk_host.h"

static int fails;

static void check(const char* what, int ok, const std::string& note = {})
{
  std::printf("undo host: %-56s %s", what, ok ? "ok" : "FAIL");
  if (!note.empty())
    std::printf("  (%s)", note.c_str());
  std::printf("\n");
  fails += !ok;
}

static const uint32_t kLevel = 0;  /* P_LEVEL (core.h enum order) — a mix param: kept */
static const uint32_t kAtk = 1;    /* P_ATK */
static const uint32_t kDec = 2;
static const uint32_t kPe0 = 83;   /* P_E0 */

static void advance_ms(uint32_t ms)
{
  static std::vector<float> L(512), R(512);
  uint32_t frames = (uint32_t) ((uint64_t) ms * SK_HOST_FS / 1000u);
  while (frames)
  {
    uint32_t n = frames < 512u ? frames : 512u;
    sk_host_render_f32(L.data(), R.data(), n);
    frames -= n;
  }
}

int main()
{
  sk_host_init();
  sk_host_boot();
  sk_host_select_track(0);

  const uint32_t nparams = sk_host_param_count();
  std::vector<int16_t> before(nparams), after(nparams);

  check("a fresh session has empty stacks",
        sk_host_undo_depth() == 0u && sk_host_redo_depth() == 0u);
  check("undo/redo do nothing there", sk_host_undo() == 0 && sk_host_redo() == 0 &&
        sk_host_undo_depth() == 0u && sk_host_redo_depth() == 0u);
  check("the capacity is published", sk_host_undo_capacity() >= 8u);

  /* ---- a program load is one step ------------------------------------------------ */
  const uint32_t bank = sk_host_preset_bank_count();
  check("the preset bank has sounds", bank >= 2u, std::to_string(bank));
  const uint32_t rank0 = sk_host_sel_engine_rank();
  const int preset0 = sk_host_sel_preset();
  for (uint32_t i = 0; i < nparams; ++i)
    before[i] = (int16_t) sk_host_get_param(i);

  check("load the bank's last sound", sk_host_preset_bank_go(bank - 1u) == 1);
  check("the load is one undo step", sk_host_undo_depth() == 1u);
  for (uint32_t i = 0; i < nparams; ++i)
    after[i] = (int16_t) sk_host_get_param(i);
  const bool load_changed = std::memcmp(before.data(), after.data(), nparams * sizeof before[0]) != 0 ||
                            sk_host_sel_engine_rank() != rank0 || sk_host_sel_preset() != preset0;
  check("the load changed the sound", load_changed);

  check("undo swaps the loaded sound out", sk_host_undo() == 1 &&
        sk_host_undo_depth() == 0u && sk_host_redo_depth() == 1u);
  bool back = sk_host_sel_engine_rank() == rank0 && sk_host_sel_preset() == preset0;
  for (uint32_t i = 0; i < nparams && back; ++i)
    back = (int16_t) sk_host_get_param(i) == before[i];
  check("the sound before the load is back", back);

  check("redo re-applies the load", sk_host_redo() == 1 &&
        sk_host_undo_depth() == 1u && sk_host_redo_depth() == 0u);
  bool again = true;
  for (uint32_t i = 0; i < nparams && again; ++i)
    again = (int16_t) sk_host_get_param(i) == after[i];
  check("the loaded sound is back", again);

  /* ---- a knob sweep is one step; a later sweep is another ------------------------ */
  advance_ms(1000);                         /* a quiet spell: the next move starts a step */
  const int32_t atk0 = sk_host_get_param(kAtk);
  const int32_t dec0 = sk_host_get_param(kDec);
  sk_host_set_param(kAtk, atk0 == 33 ? 32 : 33);
  advance_ms(20);
  sk_host_set_param(kDec, dec0 == 44 ? 43 : 44);
  check("a sweep is undone as one move", sk_host_undo() == 1 && sk_host_get_param(kAtk) == atk0 &&
        sk_host_get_param(kDec) == dec0,
        "atk " + std::to_string(atk0) + "->" + std::to_string(sk_host_get_param(kAtk)) + " dec " +
          std::to_string(dec0) + "->" + std::to_string(sk_host_get_param(kDec)) + " depth " +
          std::to_string(sk_host_undo_depth()));
  check("redo brings the sweep back", sk_host_redo() == 1 &&
        sk_host_get_param(kAtk) == (atk0 == 33 ? 32 : 33) && sk_host_get_param(kDec) == (dec0 == 44 ? 43 : 44));

  /* an edit made after an undo is a new branch: the redo of the swap is gone */
  check("undo leaves a redo", sk_host_undo() == 1 && sk_host_redo_depth() == 1u);
  const int32_t pe0a = sk_host_get_param(kPe0);
  sk_host_set_param(kPe0, pe0a == 70 ? 64 : 70);
  check("an edit discards redo", sk_host_redo_depth() == 0u && !sk_host_redo());

  /* ---- the mix stays out of it --------------------------------------------------- */
  sk_host_set_param(kLevel, sk_host_get_param(kLevel) == 90 ? 80 : 90); /* a mix move: kept */
  const int32_t level_now = sk_host_get_param(kLevel);
  const int32_t atk_now = sk_host_get_param(kAtk);
  const uint32_t rank_now = sk_host_sel_engine_rank();
  const int preset_now = sk_host_sel_preset();
  check("load over a mix edit", sk_host_preset_bank_go(0) == 1);
  const bool load_moved = sk_host_sel_engine_rank() != rank_now || sk_host_sel_preset() != preset_now ||
                          sk_host_get_param(kAtk) != atk_now;
  check("the load moved the sound", load_moved);
  check("undo restores it and does not touch LEVEL", sk_host_undo() == 1 &&
        sk_host_get_param(kAtk) == atk_now && sk_host_get_param(kLevel) == level_now);

  /* ---- the FM6 patch travels with the sound ------------------------------------- */
  uint32_t fm_rank = 0u;
  bool found_fm6 = false;
  for (uint32_t r = 0; r < 16u && !found_fm6; ++r)
  {
    if (sk_host_engine_factory_count(r) == 0u || sk_host_engine_factory_apply(r, 0) == 0)
      continue;
    if (std::strcmp(sk_host_trk_engine_name(0), "FM6") == 0)
      found_fm6 = true, fm_rank = r;
  }
  if (found_fm6)
  {
    uint8_t v1[155] = {}, v2[155] = {};
    sk_host_fm6_get(v1, sizeof v1);
    sk_host_engine_factory_apply(fm_rank, 1);
    sk_host_fm6_get(v2, sizeof v2);
    const bool patch_changed = std::memcmp(v1, v2, sizeof v1) != 0;
    check("the FM6 patch changed with the load", patch_changed);
    check("undo restores the FM6 patch", sk_host_undo() == 1);
    sk_host_fm6_get(v2, sizeof v2);
    check("the old patch is back", std::memcmp(v1, v2, sizeof v1) == 0);
  }
  else
    check("FM6 is in the bank", 0, "no FM6 rank found");

  /* ---- the motion pool travels with the sound ----------------------------------- */
  sk_host_select_track(1);
  sk_host_motion_clear(1);
  sk_host_undo_mark();
  check("an armed pool records", sk_host_motion_set_event(1, 5, kAtk, 40) == 0 &&
        sk_host_motion_count(1) == 1u);
  check("undo drops the event", sk_host_undo() == 1 && sk_host_motion_count(1) == 0u);
  check("redo brings it back", sk_host_redo() == 1 && sk_host_motion_count(1) == 1u);
  sk_host_motion_clear(1);

  /* ---- the step remembers its track --------------------------------------------- */
  sk_host_select_track(2);
  const int32_t t3_before = sk_host_get_param(kAtk);
  sk_host_undo_mark();
  sk_host_set_param(kAtk, t3_before == 55 ? 56 : 55);
  const uint32_t d_t3 = sk_host_undo_depth();
  sk_host_select_track(0);
  check("undo away from the track still swaps it", sk_host_undo() == 1 &&
        sk_host_selected_track() == 2u && sk_host_undo_depth() == d_t3 - 1u);
  check("... and restores its sound", sk_host_get_param(kAtk) == t3_before);

  /* ---- bounded, and redo lives only until the next edit ------------------------- */
  sk_host_select_track(0);
  for (uint32_t i = 0; i < 40u; ++i)
  {
    sk_host_undo_mark();
    sk_host_set_param(kAtk, (int32_t) (i % 64u) + 10);
  }
  check("the stack is bounded", sk_host_undo_depth() <= sk_host_undo_capacity(),
        std::to_string(sk_host_undo_depth()));
  check("the newest step is on top", sk_host_undo() == 1 && sk_host_redo_depth() == 1u);
  sk_host_set_param(kAtk, sk_host_get_param(kAtk) == 64 ? 63 : 64);
  check("an edit discards redo", sk_host_redo_depth() == 0u && !sk_host_redo());

  std::printf(fails ? "UNDO HOST: %d FAILED\n" : "UNDO HOST: all ok\n", fails);
  return fails ? 1 : 0;
}
