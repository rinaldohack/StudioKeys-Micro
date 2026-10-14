/* SPDX-License-Identifier: GPL-3.0-only
 * Palette / color smoke: set two palettes, RGB565 differ, draws bump. */
#include "sk_host.h"
#include <stdio.h>
#include <string.h>

/* UI frames start after ~430 ms boot, then every ~15 ms. */
static int pump_until_draws(uint32_t before, uint32_t max_blocks)
{
  float L[1024], R[1024];
  uint32_t i;
  memset(L, 0, sizeof L);
  memset(R, 0, sizeof R);
  for (i = 0; i < max_blocks; i++) {
    sk_host_render_f32(L, R, 1024);
    if (sk_host_screen_draws() > before)
      return 0;
  }
  return -1;
}

static int set_and_draw(uint32_t idx, uint16_t rgb5[5])
{
  uint32_t before, after;

  /* Ensure a real change so set_palette arms ui.force. */
  if (sk_host_palette() == idx) {
    uint32_t other = (idx == 0u) ? 1u : 0u;
    sk_host_set_palette(other);
  }

  before = sk_host_screen_draws();
  sk_host_set_palette(idx);
  if (sk_host_palette() != idx) {
    fprintf(stderr, "palette want %u got %u\n", idx, sk_host_palette());
    return -1;
  }
  if (pump_until_draws(before, 48u) != 0) {
    after = sk_host_screen_draws();
    fprintf(stderr, "screen_draws did not increase after set_palette(%u) (%u → %u)\n", idx,
            before, after);
    return -1;
  }
  sk_host_palette_rgb565(rgb5);
  return 0;
}

int main(void)
{
  uint32_t n, a, b;
  uint16_t rgb_a[5], rgb_b[5];

  sk_host_init();
  sk_host_boot();

  n = sk_host_npalettes();
  if (n < 2u) {
    fprintf(stderr, "npalettes too small: %u\n", n);
    return 1;
  }

  a = 0u;
  b = 1u;
  if (n > 2u)
    b = n - 1u; /* prefer last when available */

  /* Clear boot splash delay so later set_palette draws are observable. */
  if (pump_until_draws(sk_host_screen_draws(), 48u) != 0) {
    fprintf(stderr, "boot frames never drew (draws=%u)\n", sk_host_screen_draws());
    return 1;
  }

  if (set_and_draw(a, rgb_a) != 0)
    return 1;
  if (set_and_draw(b, rgb_b) != 0)
    return 1;

  if (memcmp(rgb_a, rgb_b, sizeof rgb_a) == 0) {
    fprintf(stderr, "palette %u and %u RGB565 identical\n", a, b);
    return 1;
  }

  puts("color smoke ok");
  return 0;
}
