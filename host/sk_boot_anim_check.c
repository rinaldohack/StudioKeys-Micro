/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * P6f: the boot splash the companion's window opens on, once per power-on.
 *
 * Closing and reopening the VST editor destroys and rebuilds the IGraphics, so LayoutUI asks for the
 * splash again. It used to replay, and because the splash advances on the *device* clock (web_ms, fed
 * by the audio thread) a host that suspends a silent synth left the window parked on the mark with
 * the pad bar half filled until some other control change forced a repaint. sk_host_boot_anim() now
 * answers 1 the first time and 0 afterwards; this checks both, and that a second ask does not restart
 * the 1 s hold. */
#include "sk_host.h"
#include <stdio.h>
#include <string.h>

#define HALF_BLOCK 1024u          /* 1024 frames: 23.2 ms of device time at 44.1 kHz */
#define PAD_LEGEND "the pad's own legend is back once the splash ends"

static float L[HALF_BLOCK], R[HALF_BLOCK];

/* device ms to render */
static void run_ms(uint32_t ms)
{
  while (ms > 0)
  {
    sk_host_render_f32(L, R, HALF_BLOCK);
    ms = ms > 24 ? ms - 24 : 0;
  }
}

/* render until the CFW draws a frame again, or fail after `blocks` (each ~23 ms) */
static int wait_draws(uint32_t before, uint32_t blocks)
{
  uint32_t i;
  for (i = 0; i < blocks; i++)
  {
    sk_host_render_f32(L, R, HALF_BLOCK);
    if (sk_host_screen_draws() != before)
      return 0;
  }
  return -1;
}

int main(void)
{
  const char* legend;
  uint32_t draws;

  /* A device the host holds suspended: not one millisecond of its clock has run since power-on, so a
   * splash whose hold is device time would never end. The ask is answered 0 and the mark the boot
   * itself drew stays on the screen. */
  sk_host_init();
  sk_host_boot();
  if (sk_host_boot_anim() != 0)
  {
    fprintf(stderr, "a suspended device was given the splash\n");
    return 1;
  }
  legend = sk_host_pad_abbr(0);
  if (!legend || !legend[0])
  {
    fprintf(stderr, "the splash took the pads on a suspended device\n");
    return 1;
  }

  /* a live device */
  sk_host_init();
  sk_host_boot();
  /* the instance boot holds the screen ~430 ms, then the CFW draws */
  if (wait_draws(sk_host_screen_draws(), 96u) != 0)
  {
    fprintf(stderr, "boot frames never drew (draws=%u)\n", sk_host_screen_draws());
    return 1;
  }
  legend = sk_host_pad_abbr(0);
  if (!legend || !legend[0])
  {
    fprintf(stderr, "pads carry no legend before the splash\n");
    return 1;
  }

  /* the first editor layout: the splash plays, the pads become the loading bar */
  if (sk_host_boot_anim() != 1)
  {
    fprintf(stderr, "first sk_host_boot_anim() must play the splash\n");
    return 1;
  }
  if (sk_host_pad_abbr(0) && sk_host_pad_abbr(0)[0])
  {
    fprintf(stderr, "splash asked for but the pad legends are still there\n");
    return 1;
  }
  run_ms(300u);
  if (sk_host_pad_abbr(0) && sk_host_pad_abbr(0)[0])
  {
    fprintf(stderr, "the splash ended before its second (%s)\n", PAD_LEGEND);
    return 1;
  }
  draws = sk_host_screen_draws();

  /* an editor reopen (a new IGraphics): no second splash, and the hold it would have started is not
   * restarted either - so the device comes back on its own */
  if (sk_host_boot_anim() != 0)
  {
    fprintf(stderr, "a reopen replayed the splash\n");
    return 1;
  }
  if (wait_draws(draws, 96u) != 0)
  {
    fprintf(stderr, "the second ask restarted the hold: no frame in 2.2 s (draws=%u)\n",
            sk_host_screen_draws());
    return 1;
  }
  legend = sk_host_pad_abbr(0);
  if (!legend || !legend[0])
  {
    fprintf(stderr, "the pads never left the loading bar (%s)\n", PAD_LEGEND);
    return 1;
  }
  if (memcmp(legend, "SET", 3) != 0 && memcmp(legend, "TUN", 3) != 0)
  {
    fprintf(stderr, "pad 1 reads '%s', not the page's legend\n", legend);
    return 1;
  }

  puts("boot anim ok (once per power-on, no reopen replay)");
  return 0;
}
