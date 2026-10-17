/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The engine's input mix (SKM_AUDIO_IN): sound that comes from outside the synth - the USB audio playback
 * stream today (usb.c, SKM_UAC_PLAY), a Bluetooth sink later (SKM_BT) - is written here by its producer and
 * summed into the master by the audio ISR (audio.c audio_block, after uac_tap so the host never hears itself).
 *
 * One ring of 1,024 frames (~23 ms, a frame packed L | R << 16 as the SIE hands it over) and a fixed -6 dB: a
 * DAW at 0 dBFS still leaves the master headroom. USB runs at the host's 44,100 Hz and the codec at
 * ~44,117.6 Hz, so the ring slowly fills: a mix that finds more than IN_TARGET + IN_DEAD frames drops the
 * excess from the read side (the stream stays live and its latency bounded) instead of falling behind. An
 * empty ring holds the last frame for that block and counts an underrun; it never stalls the ISR.
 *
 * The producer writes from TIMER5 (usb.c), the consumer is the audio ISR: in_w / in_r are published with
 * RING_PUBLISH(), as the capture ring in usb.c does.
 * Host test: tests/uac_test.c (-DT_PLAY=1).
 *
 * With SKM_AUDIO_IN=0 nothing here is built. studiokeys.c includes it before usb.c. */
#ifndef SKM_AUDIO_IN
#define SKM_AUDIO_IN 0
#endif

#if SKM_AUDIO_IN

#define IN_N 1024u               /* ring, frames (a power of two) */
#define IN_TARGET 192u           /* frames kept in hand at the start of a mix (~4.4 ms) */
#define IN_DEAD 32u              /* drift band: no drop before this much over target */
#define IN_LOW 96u               /* below this: repeat one frame (~2.2 ms: the codec is a hair faster) */
#define IN_GAIN 16384            /* Q15: -6 dB */

/* the pool (app.ld): 4 KiB that .bss cannot hold at 100 user presets. A Mach-O host test has no such
 * section: the array is ordinary .bss there */
#ifdef __APPLE__
#define IN_POOL
#else
#define IN_POOL __attribute__((section(".pool")))
#endif
static uint32_t in_ring[IN_N] IN_POOL;        /* L | R << 16 */
static volatile uint32_t in_w, in_r;
static uint32_t in_last;                      /* held on an underrun */
static uint32_t in_slip;                      /* blocks since the last repeated frame (the slow drift) */
static volatile uint8_t in_on;                /* something was written since the stream restarted */
static struct {
    uint32_t puts, frames, drops, underruns;
} in_stat;

/* producer: n frames from a buffer of packed frames (the SIE's format). Returns 0 when the ring had no
 * room - the frames are dropped and counted, never overwritten in place. */
static int __attribute__((unused)) skm_in_put_packed(const uint32_t *p, uint32_t n)
{
    uint32_t w = in_w, fill = w - in_r, i;
    if (!n)
        return 1;
    if (IN_N - fill < n) {
        in_stat.drops += n;
        return 0;
    }
    for (i = 0; i < n; i++)
        in_ring[(w + i) & (IN_N - 1u)] = p[i];
    RING_PUBLISH();
    in_w = w + n;
    in_on = 1;
    in_stat.puts++;
    in_stat.frames += n;
    return 1;
}

/* producer: n frames of interleaved L R (a Bluetooth sink, a host test) */
static int __attribute__((unused)) skm_in_put(const int16_t *lr, uint32_t n)
{
    uint32_t w = in_w, fill = w - in_r, i;
    if (!n)
        return 1;
    if (IN_N - fill < n) {
        in_stat.drops += n;
        return 0;
    }
    for (i = 0; i < n; i++)
        in_ring[(w + i) & (IN_N - 1u)] = (uint32_t)(uint16_t)lr[2u * i] |
                                        (uint32_t)(uint16_t)lr[2u * i + 1u] << 16;
    RING_PUBLISH();
    in_w = w + n;
    in_on = 1;
    in_stat.puts++;
    in_stat.frames += n;
    return 1;
}

/* consumer (audio ISR): add the stream to the master block, out[2i] / out[2i+1], Q15 */
static void skm_in_mix(int32_t *out, uint32_t n)
{
    uint32_t r = in_r, w = in_w, fill, k, i, adv;
    if (!in_on || !n)
        return;
    fill = w - r;
    if (!fill) {                              /* the producer stopped (an alt 0 without a reset): silence, not
                                               * a held DC frame - the next put starts the stream again */
        in_on = 0;
        in_stat.underruns++;
        return;
    }
    if (fill > IN_TARGET + IN_DEAD) {         /* the host is ahead (or a burst): drop the oldest */
        uint32_t d = fill - IN_TARGET;
        r += d;
        in_stat.drops += d;
        fill -= d;
    }
    k = fill < n ? fill : n;
    RING_PUBLISH();                           /* the slots before the index (single producer) */
    for (i = 0; i < k; i++) {
        uint32_t x = in_ring[(r + i) & (IN_N - 1u)];
        out[2u * i] += ((int32_t)(int16_t)(x & 0xFFFFu) * IN_GAIN) >> 15;
        out[2u * i + 1u] += ((int32_t)(int16_t)(x >> 16) * IN_GAIN) >> 15;
        in_last = x;
    }
    for (; i < n; i++) {                      /* underrun: hold the last frame (a click, not a gap) */
        out[2u * i] += ((int32_t)(int16_t)(in_last & 0xFFFFu) * IN_GAIN) >> 15;
        out[2u * i + 1u] += ((int32_t)(int16_t)(in_last >> 16) * IN_GAIN) >> 15;
    }
    if (k < n)
        in_stat.underruns++;
    adv = k;
    /* The codec is ~0.04 % faster than the host, so the ring slowly drains. Below IN_LOW, one frame
     * is replayed every IN_SLIP blocks (1 in 20 of 128 = 0.04 %): a sample slip, inaudible, and the
     * band never empties. */
    if (fill < IN_LOW && k > 1u && ++in_slip >= 20u) {
        in_slip = 0;
        adv = k - 1u;
    } else if (fill >= IN_LOW) {
        in_slip = 0;
    }
    RING_PUBLISH();
    in_r = r + adv;
}

/* a new stream (alt 0/1, bus reset): forget what the last one left */
static void __attribute__((unused)) skm_in_reset(void)
{
    RING_PUBLISH();
    in_r = in_w;
    in_on = 0;
    in_last = 0;
    RING_PUBLISH();
}

#endif
