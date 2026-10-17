/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Output rate: measure what the I2S clock really is, and lock the pitch to it.
 *
 * The frame clock is an integer divider of the audio clock - 24 MHz / 544 = 44,117.647 Hz as this
 * board runs it (the stock firmware stores 44118 for the same reason) - while every table in
 * skm_tables.h is built for FS = 44,100. A note would therefore sound 400 ppm (0.69 cent) sharp
 * against the same patch in the VST at 44.1 kHz: inaudible alone, but a unison or a long pad beats
 * against the DAW once every ~5.7 s at A4.
 *
 * No table has to change for that: count the frames the DMA really delivers against TIMER4 (the same
 * crystal, so the ratio is exact whatever the crystal's own tolerance) and scale the pitch increments
 * by FS / measured (dsp.c tuned_inc). The phase then advances at the frequency the VST plays.
 * Envelopes, LFOs and delay times stay 0.04 % short - 10x below anything audible - and nothing here
 * assumes the error exists: on a true 44.1 kHz clock the factor comes out 1:1.
 *
 * audio.c skm_rate_service does the measuring; 65536 = unity, the value host tests and the VST keep. */
#pragma once
#include <stdint.h>

#define SKM_FS_NOMINAL 44100u          /* what the tables are built for (skm_tables.h FS) */
#define SKM_TRIM_UNITY 65536u          /* Q16 1.0: no correction */
#define SKM_RATE_LO_HZ 40000u          /* a measurement outside 40..48 kHz is a bug, not a rate */
#define SKM_RATE_HI_HZ 48000u

extern uint32_t skm_pitch_trim;        /* Q16 x FS / measured (dsp.c) */

/* frames delivered over `ticks` TIMER4 ticks (ticks_per_us = FM1_TICKS_PER_US), in milli-Hz */
static inline uint32_t skm_rate_milli_hz(uint32_t frames, uint32_t ticks, uint32_t ticks_per_us)
{
    return ticks ? (uint32_t)(((uint64_t)frames * ticks_per_us * 1000000000u) / ticks) : 0u;
}

static inline int skm_rate_valid(uint32_t milli_hz)
{
    return milli_hz >= SKM_RATE_LO_HZ * 1000u && milli_hz <= SKM_RATE_HI_HZ * 1000u;
}

/* Q16 FS / measured: a note comes out at the frequency the VST plays for the same pitch. Clamped to
 * +-2.6 % (about half a semitone) so a bad window can never turn into a wrong note. */
static inline uint32_t skm_trim_q16(uint32_t milli_hz)
{
    uint32_t t = milli_hz ? (uint32_t)(((uint64_t)SKM_TRIM_UNITY * SKM_FS_NOMINAL * 1000u) / milli_hz)
                          : SKM_TRIM_UNITY;
    if (t < 64000u)
        t = 64000u;
    if (t > 67100u)
        t = 67100u;
    return t;
}
