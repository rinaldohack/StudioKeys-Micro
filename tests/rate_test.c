/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The output-rate measurement and the pitch lock (skm_rate.h), on their own: the math audio.c runs on
 * two ISR stamps (skm_rate_mark / skm_rate_service), and the trim dsp.c applies to every pitch.
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

/* audio.c stamps every 2048 halves; a half is HALF_FRAMES (core.h: 128) frames. */
#define MARK_HALVES 2048u
#define MARK_FRAMES (MARK_HALVES * 128u)
/* 24 MHz / 44,117.647 Hz = 544 ticks a frame exactly -> the span of one stamp interval */
#define MARK_TICKS (MARK_FRAMES * 544u)
/* 24 MHz / 44,100 Hz: a nominal clock's span for the same frames */
#define NOMINAL_TICKS ((uint32_t)((uint64_t)MARK_FRAMES * 24000000u / 44100u))

int main(void)
{
    uint32_t board = skm_rate_milli_hz(MARK_FRAMES, MARK_TICKS, 24u);
    uint32_t nominal = skm_rate_milli_hz(MARK_FRAMES, NOMINAL_TICKS, 24u);
    /* Ten microseconds of ISR-entry jitter between the two stamps, over a 5.94 s span. */
    uint32_t jit_hi = skm_rate_milli_hz(MARK_FRAMES, MARK_TICKS + 240u, 24u);
    uint32_t jit_lo = skm_rate_milli_hz(MARK_FRAMES, MARK_TICKS - 240u, 24u);

    check("a mark pair at 544 ticks a frame reads 44,117,647 milli-Hz", board == 44117647u);
    check("a mark pair at a true 44.1 kHz reads 44,100,000 milli-Hz",
          nominal >= 44099990u && nominal <= 44100010u);
    check("the span is ~5.9 s (2048 halves of 128 frames)", MARK_FRAMES == 262144u);
    check("the in-band test accepts both and rejects a stopped clock",
          skm_rate_valid(board) && skm_rate_valid(nominal) && !skm_rate_valid(0u));
    check("the in-band test rejects 32 kHz / 96 kHz",
          !skm_rate_valid(32000000u) && !skm_rate_valid(96000000u));

    /* The lock: 65,536 x FS / measured. 17.647 Hz fast is 400 ppm, i.e. 65,536 - 26.2 = 65,509;
     * the Q16 step is 15 ppm, so the residual is below 0.01 cent. */
    check("44,100,000 milli-Hz is unity (65,536)", skm_trim_q16(44100000u) == SKM_TRIM_UNITY);
    check("44,117,647 milli-Hz trims to 65,509", skm_trim_q16(44117647u) == 65509u);
    check("the measured board rate's own trim reads back 65,509", skm_trim_q16(board) == 65509u);
    check("a 44,600 Hz clock trims to 64,801", skm_trim_q16(44600000u) == 64801u);
    check("48 kHz is out of the lock's range and clamps at 64,000", skm_trim_q16(48000000u) == 64000u);
    check("a slow clock clamps at 67,100 (+0.34 semitone at most)", skm_trim_q16(40000000u) == 67100u);

    /* Why stamps and not a 4 s window of halves: one half is 2.9 ms, so a 4 s window quantizes by
     * 725 ppm - twice the error being corrected. The stamp span brings that down to the jitter. */
    check("one half over a 4 s window is 725 ppm (why the marks exist)",
          (uint32_t)((128u * 1000000u) / (44100u * 4u)) == 725u);
    check("10 us of stamp jitter is under 20 ppm",
          (int32_t)(jit_lo - board) < (int32_t)(board / 50000u) &&
          (int32_t)(board - jit_lo) < (int32_t)(board / 50000u) &&
          (int32_t)(jit_hi - board) < (int32_t)(board / 50000u) &&
          (int32_t)(board - jit_hi) < (int32_t)(board / 50000u));
    check("10 us of stamp jitter still trims to 65,509",
          skm_trim_q16(jit_lo) == 65509u && skm_trim_q16(jit_hi) == 65509u);

    printf("rate: %s (%d failure%s)\n", bad ? "FAILED" : "all passed", bad, bad == 1 ? "" : "s");
    return bad != 0;
}
