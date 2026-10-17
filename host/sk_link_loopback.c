/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * In-process SKL lock + SKS surface loopback (no USB keyboard required). */
#include "sk_host.h"
#include "sk_skl.h"
#include "sk_sks.h"
#include <stdio.h>
#include <string.h>

static void push_frame(const uint8_t *f, uint32_t n) { sk_host_link_push(f, n); }

static void push_skl(uint8_t cmd, const uint8_t *pay, uint32_t np)
{
  uint8_t f[64];
  uint32_t n = 0, i;
  f[n++] = 0xF0;
  f[n++] = SK_SKL_HDR0;
  f[n++] = SK_SKL_HDR1;
  f[n++] = SK_SKL_HDR2;
  f[n++] = SK_SKL_HDR3;
  f[n++] = cmd;
  for (i = 0; i < np && n + 1u < sizeof f; i++)
    f[n++] = (uint8_t)(pay[i] & 0x7Fu);
  f[n++] = 0xF7;
  push_frame(f, n);
}

static int drain_has(uint8_t want)
{
  uint8_t buf[256];
  uint32_t n, i;
  int found = 0;
  while ((n = sk_host_link_pop(buf, sizeof buf)) > 0) {
    for (i = 0; i + 6u < n; i++) {
      if (buf[i] == 0xF0u && buf[i + 1] == SK_SKL_HDR0 && buf[i + 4] == SK_SKL_HDR3 &&
          buf[i + 5] == want)
        found = 1;
    }
  }
  return found;
}

int main(void)
{
  const char *ver;
  uint8_t sks[9];
  uint8_t pay[32];
  uint32_t k = 0;

  sk_host_init();
  sk_host_boot();
  if (!sk_host_lock_claim_instance()) {
    fprintf(stderr, "lock seat busy\n");
    return 1;
  }

  ver = sk_host_version();
  for (; ver[k] && k + 1u < sizeof pay; k++)
    pay[k] = (uint8_t)ver[k];
  pay[k++] = 0;
  push_skl(SK_SKL_HELLO, pay, k);
  if (!drain_has(SK_SKL_HELLO_ACK)) {
    fprintf(stderr, "missing HELLO_ACK\n");
    return 1;
  }

  /* Direct choose path (same as lock_smoke) after HELLO opened ASK. */
  if (sk_host_lock_get() == SK_LOCK_NONE)
    sk_host_lock_ask();
  sk_host_lock_choose(1);
  if (sk_host_lock_get() != SK_LOCK_ON) {
    fprintf(stderr, "lock want ON got %d (%s)\n", sk_host_lock_get(),
            sk_host_lock_reject_reason());
    return 1;
  }
  if (!sk_host_lock_plugin_clock()) {
    fprintf(stderr, "plugin clock off\n");
    return 1;
  }

  /* SKS frames dispatch through sk_host_link_push → sk_surf_link_sysex. */
  sks[0] = 0xF0;
  sks[1] = SK_SKS_HDR0;
  sks[2] = SK_SKS_HDR1;
  sks[3] = SK_SKS_HDR2;
  sks[4] = SK_SKS_HDR3;
  sks[5] = SK_SKS_LIVE_NOTE;
  sks[6] = 36;
  sks[7] = 100;
  sks[8] = 0xF7;
  push_frame(sks, sizeof sks);

  sks[5] = SK_SKS_LIVE_CC;
  sks[6] = 21;
  sks[7] = 64;
  push_frame(sks, sizeof sks);

  sks[5] = SK_SKS_HUI_FADER;
  sks[6] = 0;
  sks[7] = 100;
  push_frame(sks, sizeof sks);

  sks[5] = SK_SKS_HUI_NOTE;
  sks[6] = 0x4E; /* HUI play */
  sks[7] = 127;
  push_frame(sks, sizeof sks);

  if (sk_host_surf_hits() < 4u) {
    fprintf(stderr, "SKS hits want >=4 got %u\n", sk_host_surf_hits());
    return 1;
  }

  /* Lock must survive surface traffic. */
  if (sk_host_lock_get() != SK_LOCK_ON) {
    fprintf(stderr, "lock dropped after SKS\n");
    return 1;
  }

  sk_host_lock_drop();
  sk_host_lock_release_instance();
  puts("link loopback ok HELLO→ON + SKS note/cc/hui");
  return 0;
}
