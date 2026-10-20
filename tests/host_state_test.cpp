/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */

/* P8b — the companion's repaint signature.
 *
 * The desk used to poll a hand-written list of parameter ids in OnIdle(): P_GLIDE, P_GLMODE,
 * P_VOICE, ADSR and E0..E7.  Everything else the keyboard changed stayed stale until some
 * unrelated click forced a rebuild, which is why "some parameters update, some don't" kept
 * coming back.  sk_host_state_sig() replaces that list: it hashes every track parameter of every
 * track, the engine requests, the globals, the menu rows, the split, the FM6 voice bytes, the
 * sample slots and the page/desk metadata.  The desk compares the hash in OnIdle() and rebuilds
 * when it moves.
 *
 * This test proves the property that matters: *every* mutation path moves the hash, and a quiet
 * machine does not.  It also drives the encoder detent path (sk_host_enc_phys -> ui_input inside
 * the render) — the one the virtual panel's knobs use.
 *
 * Built by tests/run_tests.sh against host/sk_host.c.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sk_host.h"
#include "DeskParams.h"

namespace {

int g_checks = 0, g_fails = 0;

void chk(const char* what, int ok)
{
  ++g_checks;
  if (!ok) {
    ++g_fails;
    std::printf("FAIL  %s\n", what);
  }
}

const uint32_t kGBpm = 0; /* core.h global enum order (G_BPM first) */

void render_ms(uint32_t ms)
{
  static std::vector<float> L(512), R(512);
  uint32_t frames = (uint32_t) ((uint64_t) ms * SK_HOST_FS / 1000u);
  while (frames) {
    uint32_t n = frames < 512u ? frames : 512u;
    sk_host_render_f32(L.data(), R.data(), n);
    frames -= n;
  }
}

/* Set a track param to the far end of its own descriptor range — the engines have very different
 * ranges (the WHEEL drawbars are 0..8, the LOFI knobs 0..127) and a write outside the range is
 * clamped away, which would make a probe look like a broken signature.  The signature snapshot is
 * taken *after* the track selection, since selecting a track is itself a state change. */
struct Probe
{
  uint32_t track;
  uint32_t before;
  int old;
  bool ok;
};

Probe probe(uint32_t track, uint32_t id, int far_end)
{
  Probe p{};
  sk_host_select_track(track);
  p.track = track;
  p.before = sk_host_state_sig();
  sk_host_param_info info{};
  if (!sk_host_get_param_info(id, &info) || info.max <= info.min)
    return p;
  int target = far_end ? info.max : info.min;
  const int old = sk_host_trk_param(track, id);
  if (old == target)
    target = far_end ? info.min : info.max;
  if (old == target)
    return p; /* a single-valued param: nothing to prove here */
  sk_host_set_trk_param(track, id, target);
  p.old = old;
  p.ok = true;
  return p;
}

void unprobe(const Probe& p, uint32_t id)
{
  if (p.ok)
    sk_host_set_trk_param(p.track, id, p.old);
}

} // namespace

int main()
{
  sk_host_init();
  sk_host_boot();
  sk_host_select_track(0);
  sk_host_goto_page(0);
  render_ms(50);

  /* Belt and braces: the desk's engine table is the firmware's. */
  chk("thirteen engine desks", skdesk::kNumEngines == 13);
  for (int e = 0; e < skdesk::kNumEngines; ++e)
    chk("desk engine names match the firmware",
        std::strcmp(sk_host_engine_name((uint32_t) e), skdesk::kEngines[e].name) == 0);

  /* The plugin's DeskParams ids are the firmware's.  If this drifts, the desk writes the wrong
   * parameter on every knob turn, so assert it against the host's public accessor. */
  const uint32_t e0id = sk_host_pe0_id();
  chk("DeskParams E0 is the firmware's P_E0", e0id == (uint32_t) skdesk::kP_E0);
  chk("DeskParams ADSR ids are the firmware's",
      skdesk::kP_LEVEL == 0u && skdesk::kP_ATK == 1u && skdesk::kP_DEC == 2u &&
          skdesk::kP_SUS == 3u && skdesk::kP_REL == 4u);
  {
    sk_host_param_info info{};
    chk("P_ATK resolves in the firmware's descriptors",
        sk_host_get_param_info(skdesk::kP_ATK, &info) == 1 && info.max > info.min);
  }

  const uint32_t s0 = sk_host_state_sig();
  const uint32_t noise_id = e0id + 3; /* E0..E7 are consecutive */

  /* 1. Silence must not move it, otherwise the desk rebuilds every idle tick. */
  chk("a quiet machine keeps the signature", sk_host_state_sig() == s0);
  render_ms(30);
  chk("rendering without input keeps the signature", sk_host_state_sig() == s0);

  /* 2. Every mutation path must move it. */

  /* 2a. an exposed engine parameter (what the E0..E7 knobs write) */
  {
    const Probe p = probe(0, noise_id, 0);
    chk("an engine parameter moves it", p.ok && sk_host_state_sig() != p.before);
    unprobe(p, noise_id);
    sk_host_select_track(0);
    chk("rolling it back restores it", sk_host_state_sig() == s0);
  }

  /* 2c. the ADSR knobs */
  {
    const Probe p = probe(0, skdesk::kP_ATK, 1);
    chk("ADSR moves it", p.ok && sk_host_state_sig() != p.before);
    unprobe(p, skdesk::kP_ATK);
    sk_host_select_track(0);
    chk("rolling ADSR back restores it", sk_host_state_sig() == s0);
  }

  /* 2d. the fader (P_LEVEL) — the value text and the cap both read it */
  {
    const Probe p = probe(0, skdesk::kP_LEVEL, 0);
    float n = -1.0f;
    chk("a fader move moves it", p.ok && sk_host_state_sig() != p.before);
    chk("the fader cap normalisation follows",
        sk_host_fader_norm(0, &n) == 1 && n >= 0.0f && n <= 1.0f);
    unprobe(p, skdesk::kP_LEVEL);
    sk_host_select_track(0);
    chk("rolling the fader back restores it", sk_host_state_sig() == s0);
  }

  /* 2d2. the four caps map to T1..T4 and are independent: moving T4 must leave T1's cap alone.
   * This is what used to go stale when a preset load or the CFW's fader scan moved the level. */
  {
    sk_host_select_track(3);
    sk_host_param_info info{};
    float b0 = -1.f, b3 = -1.f;
    const int old = sk_host_trk_param(3, skdesk::kP_LEVEL);
    if (sk_host_get_param_info(skdesk::kP_LEVEL, &info) && info.max > info.min &&
        sk_host_fader_norm(0, &b0) == 1 && sk_host_fader_norm(3, &b3) == 1) {
      const int target = old > (info.min + info.max) / 2 ? info.min : info.max;
      sk_host_set_trk_param(3, skdesk::kP_LEVEL, target);
      float a0 = -1.f, a3 = -1.f;
      chk("each fader cap follows its own track",
          sk_host_fader_norm(3, &a3) == 1 && sk_host_fader_norm(0, &a0) == 1 &&
              std::fabs(a3 - (target == info.min ? 0.f : 1.f)) < 0.01f &&
              std::fabs(a0 - b0) < 1e-6f);
      sk_host_set_trk_param(3, skdesk::kP_LEVEL, old);
      sk_host_select_track(0);
      chk("restoring T4 leaves the signature alone", sk_host_state_sig() == s0);
    } else
      chk("the LEVEL descriptors resolve", 0);
  }

  /* 2e. a global (the BPM sits in the transport / settings) */
  {
    const int cur = sk_host_get_gparam(kGBpm);
    chk("the global table is reachable", cur >= 0);
    if (cur >= 0) {
      sk_host_set_gparam(kGBpm, cur == 120 ? 121 : 120);
      chk("a global moves it", sk_host_state_sig() != s0);
      sk_host_set_gparam(kGBpm, cur);
      chk("rolling the global back restores it", sk_host_state_sig() == s0);
    }
  }

  /* 2f. the page (page inspector, pad legends, encoders) */
  if (sk_host_page_count() > 1u) {
    const uint32_t here = sk_host_ui_page();
    const uint32_t next = (here + 1u) % sk_host_page_count();
    const uint32_t before = sk_host_state_sig();
    chk("a page change is reachable", sk_host_goto_page(next) == 0);
    chk("a page change moves it", sk_host_state_sig() != before);
    sk_host_goto_page(here);
    chk("going back restores it", sk_host_state_sig() == s0);

    /* P5f: the PERFORM page is the only one that reports itself, and its pads are the recall slots
     * in reading order (1..8 on top, 9..15 under them, 16 the edit jump). The companion keys its
     * desk off this: a recall must not throw the player into the loaded engine's editor. */
    uint32_t perf = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < sk_host_page_count(); ++i) {
      sk_host_ui_t u {};
      sk_host_goto_page(i);
      sk_host_ui_get(&u);
      const bool is = sk_host_ui_is_perf() != 0;
      if (is && !strcmp(u.page_title, "PERFORM"))
        perf = i;
      if (is && strcmp(u.page_title, "PERFORM"))
        chk("only the PERFORM page reports itself as PERFORM", 0);
    }
    chk("the PERFORM page is reachable and reports itself", perf != 0xFFFFFFFFu);
    if (perf != 0xFFFFFFFFu) {
      sk_host_goto_page(perf);
      chk("PERFORM: pads 1..8 are U01..U08, 9..15 U09..U15, 16 ED",
          !strcmp(sk_host_pad_abbr(0), "U01") && !strcmp(sk_host_pad_abbr(7), "U08") &&
              !strcmp(sk_host_pad_abbr(8), "U09") && !strcmp(sk_host_pad_abbr(14), "U15") &&
              sk_host_pad_level(0) < 0);
      chk("PERFORM: only a recall pad has a second line", !strcmp(sk_host_pad_sub(15), "") &&
                                                             sk_host_pad_sub(0)[0] != 0);
    }
    sk_host_goto_page(here);
  } else
    chk("the firmware has more than one page", 0);

  /* 2g. the menu rows (settings desk): a settings change is not a track param */
  {
    uint32_t row = 0;
    for (uint32_t i = 0; i < sk_host_menu_values(); ++i)
      if (sk_host_menu_n(i) > 1u) {
        row = i;
        break;
      }
    chk("the settings desk has a row with values", sk_host_menu_n(row) > 1u);
    const uint32_t old = sk_host_menu_get(row);
    sk_host_menu_put(row, (old + 1u) % sk_host_menu_n(row));
    chk("a settings change moves it", sk_host_state_sig() != s0);
    sk_host_menu_put(row, old);
    chk("rolling the settings back restores it", sk_host_state_sig() == s0);
  }

  /* 2h. the split (a hold-button edit, not a track param). The split needs two different parts,
   * so arm it on T1/T2 first. */
  {
    int32_t note = 0, lo = 0, hi = 0;
    sk_host_split_get(&note, &lo, &hi);
    sk_host_split_set(60, 0, 1);
    const uint32_t armed = sk_host_state_sig();
    chk("arming the split moves it", armed != s0);
    sk_host_split_set(59, 0, 1);
    chk("the split note moves it", sk_host_state_sig() != armed);
    sk_host_split_set(0, lo, hi);
    chk("disarming the split restores it", sk_host_state_sig() == s0);
    sk_host_split_set(0, 1, 0);
    chk("the split floor moves it", sk_host_state_sig() != s0);
    sk_host_split_set(note, lo, hi);
    chk("rolling the floor back restores it", sk_host_state_sig() == s0);
  }

  /* 2i. an FM6 voice byte (the preset page writes these behind the param array's back). adopt()
   * derives the macro params from the patch, so compare against the state right before the
   * probe. */
  {
    std::vector<uint8_t> patch(SK_HOST_FM6_VOICE, 0);
    const uint32_t before = sk_host_state_sig();
    chk("an FM6 voice reads", sk_host_fm6_get(patch.data(), (uint32_t) patch.size()) > 0);
    const uint8_t saved = patch[13];
    sk_host_fm6_set_byte(13, (uint8_t) (saved == 31u ? 30u : 31u));
    chk("an FM6 voice byte moves it", sk_host_state_sig() != before);
    sk_host_fm6_set_byte(13, saved);
    chk("rolling the FM6 byte back restores it", sk_host_state_sig() == before);
  }

  /* 2j. a track the desk is not showing must count too: the mixer strip of T3 is on screen while
   * the selected track is T1. The signature is global, not per-track. */
  {
    sk_host_select_track(0);
    const uint32_t before = sk_host_state_sig();
    const Probe p = probe(2, noise_id, 1);
    chk("an edit on another track moves it", p.ok && sk_host_state_sig() != before);
    unprobe(p, noise_id);
    sk_host_select_track(0);
    chk("rolling another track back restores it", sk_host_state_sig() == before);
  }

  /* 2k. the physical encoder path: sk_host_enc_phys() queues a detent, ui_input() applies it
   * during the render. This is exactly what the virtual panel's SkEncKnob / SkParamKnob do.  The
   * batch rings a few hundred ms of audio before ui_input's 15 ms frame gate runs, so allow a
   * second of render; the first direction may sit at a limit, hence both. */
  {
    const uint32_t before = sk_host_state_sig();
    int moved = 0;
    for (uint32_t e = 0; e < 8u && !moved; ++e) {
      const int32_t dirs[2] = {7, -7};
      for (int d = 0; d < 2 && !moved; ++d) {
        sk_host_enc_phys(e, dirs[d]);
        for (int i = 0; i < 8 && !moved; ++i) {
          render_ms(125);
          moved = sk_host_state_sig() != before;
        }
      }
    }
    chk("an encoder detent moves it", moved);
  }

  /* 2l. a program load (the preset selector): the whole sound changes, and every desk that shows
   * it must repaint in the same idle tick. */
  {
    sk_host_select_track(3);
    const uint32_t rank = sk_host_trk_engine_rank(3);
    const uint32_t before = sk_host_state_sig();
    chk("a factory preset load applies",
        sk_host_engine_factory_apply((rank + 1u) % (uint32_t) skdesk::kNumEngines, 0) == 1);
    chk("a preset load moves it", sk_host_state_sig() != before);
  }

  /* 2m. an unexposed engine parameter: no knob reaches E10, but a preset load / automation writes
   * it, and the desk still has to show the new sound. Only an engine that fills engine_t.edit[12]
   * has a range there (WHEEL's REG / DRV / VIB, past the nine drawbars the knobs reach); an
   * 8-parameter engine leaves the tail zeroed, where there is nothing to move. The boot sound
   * depends on the user's saved NOR image, so pick the engine here instead of assuming one: this
   * check used to pass only on a machine whose boot track happened to be WHEEL. */
  {
    const uint32_t id = e0id + 10;
    int found = 0;
    sk_host_select_track(3);
    for (uint32_t r = 0; r < (uint32_t) skdesk::kNumEngines && !found; ++r) {
      sk_host_param_info inf{};
      if (sk_host_engine_factory_apply(r, 0) != 1 || !sk_host_get_param_info(id, &inf) ||
          inf.max <= inf.min)
        continue;
      found = 1;
      const Probe p = probe(3, id, 1);
      chk("an engine parameter outside E0..E7 moves it", p.ok && sk_host_state_sig() != p.before);
      unprobe(p, id);
      sk_host_select_track(3);
      chk("rolling the unexposed one back restores it", sk_host_state_sig() == p.before);
    }
    chk("an engine that fills E10 (12 engine parameters) is reachable", found);
  }

  /* 3. The master pot (the top-bar fader) is an ADC with no parameter behind it; the desk's
   * SkMasterFader reads it live instead of through the signature. */
  {
    const uint32_t m = sk_host_master_get();
    chk("the master pot reads", m <= 1023u);
  }

  std::printf("%s  (%d checks, %d failures)\n", g_fails ? "FAILED" : "ok", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
