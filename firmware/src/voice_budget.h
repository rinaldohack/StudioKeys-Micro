/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
 * The keyboard's voice pool and voice budget, in one place for both builds.
 *
 * core.h's VOICE_POOL (the firmware) and host/sk_host.c's Limited mode (the companion) used to hold
 * this number separately, so raising the device left the plugin emulating the old hardware. Both
 * include this header; the companion's *ceiling* (Unleashed) is still its own, larger pool.
 *
 * THE POOL (voice.c vpool): one arena all four parts draw from. 48 x 84 B = 4,032 B, which is exactly
 * what the old four per-part arrays held - except those could only ever make 12 voices sound (the
 * shared budget was 12) while these make 48, and any one part may hold all of them: a 24-note
 * pedalled piano is 30 slots, which no per-part split of the same memory could give without starving
 * the other three parts.
 *
 * THE BUDGET (SKM_VOICE_LOAD) is what the CPU pays, not how many voices there are: every voice is
 * charged engine_t.cost (host instructions per 44.1 kHz sample, tests/cpu_baseline.txt), so the same
 * pot buys 48 LOFI voices or 12 ANALOG supersaw ones - the Korg Voice Reserve idea, one cost per
 * engine. 6,312 is the worst case the old count budget could ever be asked for: 12 voices (the old
 * shared budget) all of them the heaviest voice on the instrument (ANALOG 12_SUPER: 4,519 measured
 * minus the 307 idle mix = 4,212 for 8 voices = 526 a voice). So no preset plays fewer voices than
 * the old firmware gave it, and every lighter engine plays more: 48 SAMPLE, 48 WHEEL, 48 LOFI,
 * 38 PHASE, 29 TRIO, 12 supersaw. audio.c lowers the pot when a block overruns real time (and
 * raises it back when the load is light), so the maximum polyphony is what the unit sustains
 * rather than a guess about an unknown clock; this value is the start, not a claim. */
#ifndef SKM_VOICE_POOL
#define SKM_VOICE_POOL 48
#endif
#ifndef SKM_VOICE_LOAD
#define SKM_VOICE_LOAD 6312          /* 12 x 526: the old budget, every voice the heaviest */
#endif
