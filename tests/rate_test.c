/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The output-rate measurement and the pitch lock (skm_rate.h), on their own: the math audio.c runs
 * every 4 s on the device, and the trim dsp.c applies to every pitch increment.
 *
 *   cc -Ibuild/gen -Ifirmware/src -o build/host/rate_test tests/rate_test.c */
#include <stdio.h>
#include <stdint.h>
#include "skm_rate.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("rate: %-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    /* A clean 4 s window at the nominal rate: 4 * 44,100 = 176,400 frames over 4 * 24,000,000 ticks. */
    uint32_t nominal = skm_rate_milli_hz(176400u, 96000000u, 24u);
    /* The same window on this board's clock: 24 MHz / 544 = 44,117.647 Hz. */
    uint32_t actual = skm_rate_milli_hz(176471u, 96000000u, 24u);   /* 176,471 / 4 s = 44,117.75 */

    check("milli-Hz of a nominal 4 s window is exactly 44,100,000", nominal == 44100000u);
    check("44,117.6 Hz reads as ~44,117,750 milli-Hz", actual > 44117000u && actual < 44118000u);
    check("the in-band test accepts 44.1 kHz and 44,117.6 Hz", skm_rate_valid(nominal) && skm_rate_valid(actual));
    check("the in-band test rejects a stopped clock", !skm_rate_valid(0u));
    check("the in-band test rejects 32 kHz / 96 kHz", !skm_rate_valid(32000000u) && !skm_rate_valid(96000000u));

    /* The lock: 65,536 x FS / measured. The I2S clock is 17.647 Hz fast, i.e. the increments shrink
     * by 400 ppm: 65,536 - 26.2 = 65,509 (Q16 resolution is 15 ppm, so the residual is ~0.002 cent). */
    check("44,100,000 milli-Hz is unity (65,536)", skm_trim_q16(44100000u) == SKM_TRIM_UNITY);
    check("44,117,647 milli-Hz trims to 65,509", skm_trim_q16(44117647u) == 65509u);
    check("44,600 Hz trims to 64,801 (the same math on a coarse rate)",
          skm_trim_q16(44600000u) == 64801u);
    check("48 kHz is out of the lock's range and clamps at 64,000", skm_trim_q16(48000000u) == 64000u);
    check("the trim is never 0 for a rate above the band", skm_trim_q16(100000000u) == 64000u);
    check("the trim clamps a slow clock at 67,100 (+0.34 semitone at most)",
          skm_trim_q16(40000000u) == 67100u);

    /* A 60 s window gives the same answer: the measurement does not depend on the window length. */
    check("60 s window agrees with the 4 s one",
          skm_rate_milli_hz(2647059u, 1440000000u, 24u) == 44117650u);

    /* The clock in TIMER4 ticks: 544 ticks a frame is 44,117.647 Hz (what DAC_TICKS says). */
    check("544 ticks per frame is 44,117.647 Hz",
          skm_rate_milli_hz(1000u, 544000u, 24u) == 44117647u);

    printf("rate: %s (%d failure%s)\n", bad ? "FAILED" : "all passed", bad, bad == 1 ? "" : "s");
    return bad != 0;
}
