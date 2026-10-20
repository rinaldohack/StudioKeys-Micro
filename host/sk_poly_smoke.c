/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * Phase 5: Limited sheds; Unleashed keeps budget at ceiling. */
#include "sk_host.h"
#include "../firmware/src/voice_budget.h"
#include <stdio.h>

/* the pot, in engine-cost units: 4,400 is 33 ANALOG voices or 8 supersaw ones */
#define LIMITED_LOAD ((uint32_t)SKM_VOICE_LOAD)
#define SET_LOAD (VOICE_POOL * 130u)

int main(void)
{
  uint32_t i;
  float L[128], R[128];

  sk_host_init();
  sk_host_boot();
  if (sk_host_voice_load() != LIMITED_LOAD) {
    fprintf(stderr, "Limited want %u got %u\n", LIMITED_LOAD, sk_host_voice_load());
    return 1;
  }

  /* Hold a dense chord under Limited — still Limited budget. */
  for (i = 0; i < 12u; i++)
    sk_host_midi(0x90, (uint8_t)(48u + i), 100);
  for (i = 0; i < 20u; i++)
    sk_host_render_f32(L, R, 128);
  if (sk_host_voice_load() != LIMITED_LOAD) {
    fprintf(stderr, "Limited budget drifted %u\n", sk_host_voice_load());
    return 1;
  }

  for (i = 0; i < 12u; i++)
    sk_host_midi(0x80, (uint8_t)(48u + i), 0);

  sk_host_set_unleashed(1);
  if (sk_host_voice_load() != SET_LOAD) {
    fprintf(stderr, "Unleashed want %u got %u\n", SET_LOAD, sk_host_voice_load());
    return 1;
  }
  for (i = 0; i < 12u; i++)
    sk_host_midi(0x90, (uint8_t)(48u + i), 100);
  for (i = 0; i < 20u; i++)
    sk_host_render_f32(L, R, 128);
  if (sk_host_voice_load() != SET_LOAD) {
    fprintf(stderr, "Unleashed shed budget %u\n", sk_host_voice_load());
    return 1;
  }

  printf("poly smoke ok Limited=%u Unleashed=%u under dense chord\n", LIMITED_LOAD, SET_LOAD);
  return 0;
}
