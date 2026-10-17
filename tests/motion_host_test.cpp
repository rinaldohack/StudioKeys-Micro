/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* P16: the host surface the companion's AUTOMATION window drives (sk_host_motion_*):
 *   - the per-track flags (playback ON, AUTO REC armed) and the empty pool at boot
 *   - a step event's write / overwrite / enumerate / remove / clear
 *   - the refusals (a step or a parameter outside the pool, the full pool)
 *   - the label and the range of an event's parameter
 * The recording itself (knob -> step while the transport plays) is firmware motion_test.c's;
 * this only checks the API the VST sees. Built by tests/run_tests.sh. */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "sk_host.h"

static int fails;

static void check(const char* what, int ok, const std::string& note = {})
{
  std::printf("motion host: %-58s %s", what, ok ? "ok" : "FAIL");
  if (!note.empty())
    std::printf("  (%s)", note.c_str());
  std::printf("\n");
  fails += !ok;
}

int main()
{
  sk_host_init();
  sk_host_boot();

  const uint32_t pe0 = sk_host_pe0_id();
  for (uint32_t tr = 0; tr < 4u; ++tr)
    sk_host_motion_clear(tr);

  check("a fresh pool is empty and unarmed",
        sk_host_motion_count(0) == 0u && !sk_host_motion_on(0) && !sk_host_motion_armed(0));
  check("every track has its own flags", !sk_host_motion_on(1) && !sk_host_motion_armed(1));

  sk_host_motion_set_armed(0, 1);
  check("AUTO REC arms one track only", sk_host_motion_armed(0) && !sk_host_motion_armed(1));
  sk_host_motion_set_armed(0, 0);
  sk_host_motion_set_on(1, 1);
  check("playback follows its track", sk_host_motion_on(1) && !sk_host_motion_on(0));

  /* a knob move without REC / AUTO writes no event */
  int lo = 0, hi = 0;
  check("the parameter's range", sk_host_motion_range(0, pe0, &lo, &hi) && lo <= hi);
  const int32_t v1 = lo + (hi - lo) / 3;
  sk_host_set_param(pe0, sk_host_trk_param(0, pe0) + 1);
  check("an ordinary knob move records nothing", sk_host_motion_count(0) == 0u);

  check("write an event", sk_host_motion_set_event(0, 3, pe0, v1) == 0 && sk_host_motion_count(0) == 1u);
  uint32_t step = 0, param = 0;
  int32_t value = 0;
  check("read it back at its step and value",
        sk_host_motion_event(0, 0, &step, &param, &value) && step == 3u && param == pe0 && value == v1);

  const int32_t v2 = lo + 2 * (hi - lo) / 3;
  check("the same step/param overwrites", sk_host_motion_set_event(0, 3, pe0, v2) == 0 &&
        sk_host_motion_count(0) == 1u && sk_host_motion_event(0, 0, &step, &param, &value) && value == v2);
  check("another step is another event", sk_host_motion_set_event(0, 9, pe0, 1) == 0 &&
        sk_host_motion_count(0) == 2u);

  check("track 1 sees none of track 0's events", sk_host_motion_count(1) == 0u);
  check("an event on track 1 stays there", sk_host_motion_set_event(1, 0, pe0, 7) == 0 &&
        sk_host_motion_count(1) == 1u && sk_host_motion_count(0) == 2u);

  check("step 64 is refused", sk_host_motion_set_event(0, 64, pe0, v1) == 1);

  char label[16] = {};
  check("the parameter's label", sk_host_motion_label(0, pe0, label, sizeof label) && label[0]);

  check("remove one event", sk_host_motion_remove_event(0, 9, pe0) == 0 && sk_host_motion_count(0) == 1u);
  check("removing it again reports none", sk_host_motion_remove_event(0, 9, pe0) == 1);
  check("removing the last one", sk_host_motion_remove_event(0, 3, pe0) == 0 && sk_host_motion_count(0) == 0u);

  sk_host_motion_clear(1);                         /* the pool is shared by the four tracks */
  for (uint32_t i = 0; i < 64u; ++i)
    sk_host_motion_set_event(0, i, pe0, (i & 1u) ? lo : hi);
  check("the pool holds 64 events", sk_host_motion_count(0) == 64u);
  check("a 65th is refused as full", sk_host_motion_set_event(0, 0, pe0 + 1u, lo) == 2);
  check("but an existing cell still overwrites", sk_host_motion_set_event(0, 5, pe0, v1) == 0);

  sk_host_motion_remove_event(0, 0, pe0);
  sk_host_motion_set_event(1, 0, pe0, lo);
  sk_host_motion_clear(0);
  check("clear empties the track and disarms it",
        sk_host_motion_count(0) == 0u && !sk_host_motion_on(0) && !sk_host_motion_armed(0));
  check("clear keeps the other track's events", sk_host_motion_count(1) == 1u);

  std::printf("%s\n", fails ? "MOTION HOST TEST FAILED" : "motion host API tests passed");
  return fails != 0;
}
