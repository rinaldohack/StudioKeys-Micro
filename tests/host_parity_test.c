/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The companion's engine against the device's: one hash per voice mode, from the same gestures and
 * the same render calls (host/studiokeys_host.c). Built twice, with the device's NVOICE=8 and the
 * companion's NVOICE=128, the two outputs must be identical.
 *
 * This is the test for SK_UNISON (core.h): an engine with no cap of its own (TRIO) used to spread
 * UNISON over NVOICE voices, so the companion played a 128-voice UNISON where the keyboard plays 8 -
 * a different detune spread and level. PHYS caps at 3, which the spread also scales.
 *
 *   cc ... -o parity8 tests/host_parity_test.c -DNVOICE=8
 *   cc ... -o parity128 tests/host_parity_test.c
 *   parity8 | diff - <(parity128)
 */
#include "studiokeys_host.c"

static uint64_t h;

static void render_hash(double ms)
{
    uint32_t n = (uint32_t)(ms * 44.1 + 0.5), k, i;
    for (k = 0; k < n; k += 128u) {
        uint32_t m = n - k < 128u ? n - k : 128u;
        web_render(m);
        for (i = 0; i < m; i++) {
            union { float f; uint32_t u; } a = {out_l[i]}, b = {out_r[i]};
            h = (h ^ a.u) * 0x100000001B3ull;
            h = (h ^ b.u) * 0x100000001B3ull;
        }
    }
}

static void chord(uint32_t on, uint8_t base)
{
    static const uint8_t iv[4] = {0, 7, 12, 16};
    uint32_t i;
    for (i = 0; i < 4u; i++)
        web_midi(on ? 0x90u : 0x80u, (uint32_t)(base + iv[i]), on ? 100u : 0u);
}

static uint64_t take(uint32_t mode, uint32_t engine, uint8_t base)
{
    track_t *t = &trk[0];
    h = 0;
    host_preset(t, engine, 0);
    t->p[P_VOICE] = (uint8_t) mode;
    chord(1u, base);
    render_hash(1200.0);
    chord(0u, base);
    render_hash(400.0);
    return h;
}

int main(void)
{
    web_nor_erase();
    web_boot();
    render_hash(500.0);
    printf("UNISON TRIO %016llx\n", (unsigned long long) take(V_UNISON, 8u, 48u));
    printf("UNISON PHYS %016llx\n", (unsigned long long) take(V_UNISON, ENGI_PHYS, 50u));
    printf("MONO        %016llx\n", (unsigned long long) take(V_MONO, 8u, 55u));
    printf("LEGATO      %016llx\n", (unsigned long long) take(V_LEGATO, 8u, 57u));
    printf("POLY        %016llx\n", (unsigned long long) take(V_POLY, 8u, 59u));
    printf("POLY PHYS   %016llx\n", (unsigned long long) take(V_POLY, ENGI_PHYS, 60u));
    return 0;
}
