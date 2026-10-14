/* SPDX-License-Identifier: GPL-3.0-only
 * Lock refuse paths: bad HELLO version, choose mismatch, second claim, dismiss. */
#include "sk_host.h"
#include "sk_skl.h"
#include <stdio.h>
#include <string.h>

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
  sk_host_link_push(f, n);
}

/* Drain link outbox; set *got_refuse if RESULT payload is a refuse code. */
static void drain_result(int *got_refuse)
{
  uint8_t buf[256];
  uint32_t n, i;
  if (got_refuse)
    *got_refuse = 0;
  while ((n = sk_host_link_pop(buf, sizeof buf)) > 0) {
    for (i = 0; i + 6u < n; i++) {
      if (buf[i] == 0xF0u && buf[i + 1] == SK_SKL_HDR0 && buf[i + 4] == SK_SKL_HDR3 &&
          buf[i + 5] == SK_SKL_RESULT) {
        uint8_t code = (i + 6u < n) ? buf[i + 6] : 0;
        if (code == (uint8_t)SK_SKL_REFUSE_VERSION || code == (uint8_t)SK_SKL_REFUSE_CRC ||
            code == (uint8_t)SK_SKL_REFUSE_STATE) {
          if (got_refuse)
            *got_refuse = 1;
        }
      }
    }
  }
}

int main(void)
{
  static const char old_ver[] = "v0.0.0-old";
  uint8_t pay[32];
  uint32_t k;
  int got_refuse;
  const char *reason;

  sk_host_init();
  sk_host_boot();

  if (!sk_host_lock_claim_instance()) {
    fprintf(stderr, "claim A failed\n");
    return 1;
  }

  /* HELLO with wrong version → refuse reason and/or RESULT refuse; lock not ON. */
  for (k = 0; old_ver[k] && k + 1u < sizeof pay; k++)
    pay[k] = (uint8_t)old_ver[k];
  pay[k++] = 0;
  push_skl(SK_SKL_HELLO, pay, k);
  drain_result(&got_refuse);
  reason = sk_host_lock_reject_reason();
  if ((!reason || !reason[0]) && !got_refuse && sk_host_lock_get() == SK_LOCK_ON) {
    fprintf(stderr, "HELLO old ver: expected refuse (reason/RESULT) or lock not ON\n");
    return 1;
  }
  if (sk_host_lock_get() == SK_LOCK_ON) {
    fprintf(stderr, "HELLO old ver: lock unexpectedly ON\n");
    return 1;
  }

  /* Direct API path: mismatched peer version must refuse choose. */
  sk_host_lock_set_peer_version(old_ver);
  sk_host_lock_ask();
  sk_host_lock_choose(1);
  reason = sk_host_lock_reject_reason();
  if (sk_host_lock_get() == SK_LOCK_ON) {
    fprintf(stderr, "choose mismatch: lock ON (%s)\n", reason);
    return 1;
  }
  if (!reason || !reason[0]) {
    fprintf(stderr, "choose mismatch: empty reject reason\n");
    return 1;
  }

  /* Second claim while A holds must fail. */
  if (sk_host_lock_claim_instance() != 0) {
    fprintf(stderr, "second claim should return 0\n");
    return 1;
  }

  /* Dismiss path: ask → dismiss → NONE. */
  sk_host_lock_set_peer_version(NULL);
  sk_host_lock_ask();
  if (sk_host_lock_get() != SK_LOCK_ASK) {
    fprintf(stderr, "ask want ASK got %d\n", sk_host_lock_get());
    return 1;
  }
  sk_host_lock_dismiss();
  if (sk_host_lock_get() != SK_LOCK_NONE) {
    fprintf(stderr, "dismiss want NONE got %d\n", sk_host_lock_get());
    return 1;
  }

  sk_host_lock_release_instance();
  puts("lock refuse ok");
  return 0;
}
