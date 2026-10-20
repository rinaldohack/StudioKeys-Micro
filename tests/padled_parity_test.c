/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* P5g: the Leslie pad's blink, the CFW and the companion, side by side.
 *
 * The pad's fill is `ui_pads.c` `pad_rgb_value`, and since the blink follows the rotor
 * (`eng_wheel.c` `wheel_horn_phase` / `wheel_horn_mhz`) it is a function of the block count: one
 * sample per audio block, no wall clock. Both builds run the same script - ROTR SLOW, then FAST,
 * then STOP - and hash the 16 pads' RGB (the chain's value and the companion's `sk_host_pad_rgb`
 * are the same number) after every block. One file, two builds:
 *
 *   cc -Ibuild/gen -Ifirmware/src -Itests -o padled_dev tests/padled_parity_test.c -lm
 *   cc -Ibuild/gen -Ifirmware/src -Ihost -DSK_PADLED_HOST -o padled_host tests/padled_parity_test.c \
 *      build/host/sk_host_slots.o -lm -lpthread
 *   padled_dev | diff - <(padled_host)
 *
 * The device build is the firmware's own sources through tests/hostsim.c; the companion build is
 * host/sk_host.c, the TU the VST3 links (it includes host/studiokeys_host.c, which includes the
 * firmware's audio, UI and LED sources). A diff here means the two do not light the same pad. */

#ifdef SK_PADLED_HOST
#include <stdio.h>
#include <string.h>
#include "sk_host.h"
#define __attribute__(x)               /* (as tests/hostsim.c: no section placement on the host) */
#include "core.h"
#else
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#endif

/* One step is the device's I2S half (HALF_FRAMES = 128) and the companion's ring: audio.c renders
 * that half as HALF_FRAMES / CTL blocks (4), so both sides sample the LED at the same block index. */
#define NB 200u
#define STEP_BLOCKS (HALF_FRAMES / CTL)

static uint64_t h;                       /* FNV-1a over every pad's RGB, per block */
static uint32_t lo, hi, turns, prev, cur;

static void sample(void)
{
#ifdef SK_PADLED_HOST
    uint32_t i, v;
    for (i = 0; i < 16u; i++) {
        v = sk_host_pad_rgb(i);
        h = (h ^ (uint64_t)v) * 0x100000001B3ull;
    }
    v = sk_host_pad_rgb(4);
#else
    uint32_t i, v;
    for (i = 0; i < 16u; i++) {
        v = pad_rgb_value(i);
        h = (h ^ (uint64_t)v) * 0x100000001B3ull;
    }
    v = pad_rgb_value(4);
#endif
    if (v < lo)
        lo = v;
    if (v > hi)
        hi = v;
    if (cur > prev && cur >= v)            /* a maximum of the fill: the horn passes the mic */
        turns++;
    prev = cur;
    cur = v;
}

static void one_step(void)
{
#ifdef SK_PADLED_HOST
    float l[128], r[128];
    sk_host_render_f32(l, r, 128u);
#else
    static int32_t o[2u * CTL];
    uint32_t k;
    for (k = 0; k < STEP_BLOCKS; k++)                  /* (the half's own blocks, as audio.c does) */
        mix_block(o, CTL);
#endif
}

static uint64_t phase(uint32_t rot)
{
    uint32_t k;
    h = 0;
    lo = ~0u;
    hi = 0;
    turns = 0;
    prev = 0;
    cur = 0;
#ifdef SK_PADLED_HOST
    sk_host_set_param(P_E11, (int32_t)rot);
#else
    trk[0].p[P_E11] = (int16_t)rot;
#endif
    sample();
    for (k = 0; k < NB; k++) {
        one_step();
        sample();
    }
    printf("ROTR %u  hash %016llx  fill %u..%u  peaks %u\n", rot, (unsigned long long)h, lo, hi, turns);
    return h;
}

int main(void)
{
#ifdef SK_PADLED_HOST
    sk_host_ui_t ui;
    uint32_t d;
    sk_host_boot();
    for (d = 0; d < 13u; d++) {                        /* the desk whose engine is the WHEEL */
        const char *n = sk_host_engine_name(d);
        if (n && !strcmp(n, "WHEEL"))
            break;
    }
    sk_host_set_desk(d);
    sk_host_ui_get(&ui);
    printf("engine %u %s\n", (unsigned)ui.engine, ui.engine_name);
#else
    ui_power_on();
    memset(trk, 0, sizeof trk);
    memset(drw_t, 0, sizeof drw_t);                    /* a fresh rotor: phase 0, speed 0 */
    memset(drw_v, 0, sizeof drw_v);
    memset(&fx, 0, sizeof fx);
    host_tracks_init();
    host_preset(&trk[0], ENGI_WHEEL, 0);
    trk[0].p[P_E9] = trk[0].p[P_E10] = 0;              /* (percussion / click off) */
    printf("engine %u %s\n", (unsigned)trk[0].eng_req, ENGINES[trk[0].eng_req]->name);
#endif
    {
        /* the pad under test is the WHEEL map's 5th (ROT): its name has to be the one the plan says */
#ifdef SK_PADLED_HOST
        const char *ab = sk_host_pad_abbr(4);
        printf("pad 4 = %s\n", ab);
#else
        printf("pad 4 = %s\n", pad_get(4)->abbr);
#endif
    }
    phase(1u);                                         /* SLOW */
    phase(2u);                                         /* FAST */
    phase(0u);                                         /* STOP: the horn coasts */
    return 0;
}
