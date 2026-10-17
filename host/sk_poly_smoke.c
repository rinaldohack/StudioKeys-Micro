/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * Phase 5: Limited sheds; Unleashed keeps budget at ceiling. */
#include "sk_host.h"
#include <stdio.h>

int main(void)
{
  uint32_t i;
  float L[128], R[128];

  sk_host_init();
  sk_host_boot();
  if (sk_host_voice_budget() != 8u) {
    fprintf(stderr, "Limited want 8 got %u\n", sk_host_voice_budget());
    return 1;
  }

  /* Hold a dense chord under Limited — still Limited budget. */
  for (i = 0; i < 12u; i++)
    sk_host_midi(0x90, (uint8_t)(48u + i), 100);
  for (i = 0; i < 20u; i++)
    sk_host_render_f32(L, R, 128);
  if (sk_host_voice_budget() != 8u) {
    fprintf(stderr, "Limited budget drifted %u\n", sk_host_voice_budget());
    return 1;
  }

  for (i = 0; i < 12u; i++)
    sk_host_midi(0x80, (uint8_t)(48u + i), 0);

  sk_host_set_unleashed(1);
  if (sk_host_voice_budget() != sk_host_voice_ceiling()) {
    fprintf(stderr, "Unleashed want %u got %u\n", sk_host_voice_ceiling(),
            sk_host_voice_budget());
    return 1;
  }
  for (i = 0; i < 12u; i++)
    sk_host_midi(0x90, (uint8_t)(48u + i), 100);
  for (i = 0; i < 20u; i++)
    sk_host_render_f32(L, R, 128);
  if (sk_host_voice_budget() != sk_host_voice_ceiling()) {
    fprintf(stderr, "Unleashed shed budget %u\n", sk_host_voice_budget());
    return 1;
  }

  puts("poly smoke ok Limited=8 Unleashed=ceiling under dense chord");
  return 0;
}
