/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Every value the companion can print, through the host API the plugin actually calls
 * (host/sk_host.c): sk_host_format_param / sk_host_format_gparam back the PAGE inspector, the desk's
 * enum menus and the sample bay.
 *
 * Live 12 crashed on 2026-10-19: the companion kept its own copy of the firmware's formatting rules
 * and indexed an enum's names by the raw value. The engine desc hooks return F_INT descriptors whose
 * list is much shorter than the range they cover (LOFI WAV# 16 names over 0..127, DCY 32), so
 * names[32] was read from whatever global followed the array and snprintf("%s", ...) faulted inside
 * strlen. The companion now asks the firmware, which counts the list and splits the range.
 *
 * 1. every page the firmware has, every slot, every value in range: non-empty, printable, fits.
 * 2. every engine and factory preset, every EDIT slot 0..127: the sweep over the desc hooks
 *    (LOFI WRAM / STEP, NOISE LEN, PHYS CHRD, FM6 ALG).
 * 3. every name row maps to a value in range (the enum menus build one row per name).
 * 4. the globals as the GLOBAL / SYSTEM pages show them.
 * 5. the exact case: LOFI WAVE = WRAM (WAV#) and CHIP = STEP (DCY) over 0..127.
 * Build it with -fsanitize=address as well: a names[] overrun is a global-buffer-overflow there. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sk_host.h"

static int bad;
static void check(const char *what, int ok, uint32_t n)
{
    printf("format: %-70s %6u values  %s\n", what, n, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* printable, non-empty, and short enough for the companion's 48-byte box */
static int text_ok(const char *s)
{
    uint32_t i;
    if (!s || !s[0])
        return 0;
    if (strlen(s) >= 48u)
        return 0;
    for (i = 0; s[i]; i++)
        if ((unsigned char) s[i] < 0x20 || (unsigned char) s[i] == 0x7F)
            return 0;
    return 1;
}

static const char *page_title(uint32_t p)
{
    const char *t = sk_host_page_title(p);
    return t ? t : "";
}

static int fmt_slot(const struct sk_host_page_slot *s, int32_t v, char *buf, uint32_t cap)
{
    return s->kind == 2 ? sk_host_format_gparam((uint32_t) s->id, v, buf, cap)
                        : sk_host_format_param((uint32_t) s->id, v, buf, cap);
}

/* the desk rank of the engine called `name`, or -1 */
static int find_engine(const char *name)
{
    uint32_t rank;
    for (rank = 0; rank < 32u; rank++) {
        if (!sk_host_engine_factory_count(rank) || !sk_host_engine_factory_apply(rank, 0))
            continue;
        sk_host_ui_pump();
        if (!strcmp(sk_host_trk_engine_name(0), name))
            return (int) rank;
    }
    return -1;
}

int main(void)
{
    char buf[48];
    uint32_t p, slot, rank, n;
    int ok;

    sk_host_init();
    sk_host_select_track(0);

    /* 1: the PAGE inspector: every page, every slot, every value in its own range */
    n = 0;
    ok = 1;
    for (p = 0; p < 64u; p++) {
        if (!page_title(p)[0])
            continue;
        for (slot = 0; slot < 8u; slot++) {
            struct sk_host_page_slot s;
            int32_t v;
            if (!sk_host_page_slot(p, slot, &s))
                continue;
            for (v = s.min; v <= s.max; v++) {
                if (!fmt_slot(&s, v, buf, sizeof buf) || !text_ok(buf)) {
                    if (ok)
                        printf("       first bad: page %s slot %u id %d value %d -> '%s'\n", page_title(p), slot,
                               s.id, (int) v, buf);
                    ok = 0;
                }
                n++;
            }
        }
    }
    check("page inspector: every page / slot / value", ok, n);

    /* 2: every engine, every factory preset, every EDIT slot over 0..127 */
    n = 0;
    ok = 1;
    for (rank = 0; rank < 32u; rank++) {
        const uint32_t nf = sk_host_engine_factory_count(rank);
        uint32_t i;
        if (!nf)
            continue;
        for (i = 0; i < nf; i++) {
            int32_t v;
            if (!sk_host_engine_factory_apply(rank, i))
                continue;
            sk_host_ui_pump();
            for (slot = 0; slot < 12u; slot++) {
                const uint32_t id = sk_host_pe0_id() + slot;
                for (v = 0; v <= 127; v++) {
                    sk_host_set_trk_param(0, id, v);
                    if (!sk_host_format_param(id, v, buf, sizeof buf) || !text_ok(buf)) {
                        if (ok)
                            printf("       first bad: rank %u preset %u slot %u value %d -> '%s'\n", rank, i, slot,
                                   (int) v, buf);
                        ok = 0;
                    }
                    n++;
                }
            }
        }
    }
    check("every engine / preset / EDIT slot 0..127", ok, n);

    /* 3: the enum menus: every row must map to a value in range */
    n = 0;
    ok = 1;
    for (rank = 0; rank < 32u; rank++) {
        const uint32_t nf = sk_host_engine_factory_count(rank);
        uint32_t i;
        if (!nf)
            continue;
        for (i = 0; i < nf; i++) {
            int32_t v;
            if (!sk_host_engine_factory_apply(rank, i))
                continue;
            sk_host_ui_pump();
            for (slot = 0; slot < 12u; slot++) {
                const uint32_t id = sk_host_pe0_id() + slot;
                for (v = 0; v <= 127; v++) {
                    const int k = sk_host_param_name_count(id, 0);
                    int r;
                    sk_host_set_trk_param(0, id, v);          /* the desc hook may switch on it */
                    for (r = 0; r < k; r++) {
                        const int nv = sk_host_param_name_value(id, 0, r);
                        if (nv < 0 || nv > 127)
                            ok = 0;
                        n++;
                    }
                }
            }
        }
    }
    check("enum menus: every name row -> a value in 0..127", ok && n > 0, n);

    /* 4: the globals as the GLOBAL / SYSTEM pages show them (page scope 1) */
    n = 0;
    ok = 1;
    for (p = 0; p < 64u; p++) {
        if (!page_title(p)[0] || sk_host_page_scope(p) != 1u)
            continue;
        for (slot = 0; slot < 8u; slot++) {
            struct sk_host_page_slot s;
            int32_t v;
            if (!sk_host_page_slot(p, slot, &s))
                continue;
            for (v = s.min; v <= s.max; v++) {
                if (!sk_host_format_gparam((uint32_t) s.id, v, buf, sizeof buf) || !text_ok(buf))
                    ok = 0;
                n++;
            }
        }
    }
    check("globals: every GLOBAL / SYSTEM slot over its range", ok && n > 0, n);

    /* 5: the case Live crashed on */
    n = 0;
    ok = 1;
    {
        const int lofi = find_engine("LOFI");
        if (lofi < 0) {
            ok = 0;
        } else {
            const uint32_t pe0 = sk_host_pe0_id();
            int32_t v;
            sk_host_engine_factory_apply((uint32_t) lofi, 0);
            sk_host_ui_pump();
            for (v = 0; v <= 127; v++) {
                /* WAVE = WRAM (4): E2 is WAV# over the whole 0..127 range */
                sk_host_set_trk_param(0, pe0 + 1u, 4);
                sk_host_set_trk_param(0, pe0 + 2u, v);
                if (!sk_host_format_param(pe0 + 2u, v, buf, sizeof buf) || !text_ok(buf))
                    ok = 0;
                /* CHIP = STEP (4): E3 is DCY over the whole range */
                sk_host_set_trk_param(0, pe0 + 0u, 4);
                sk_host_set_trk_param(0, pe0 + 3u, v);
                if (!sk_host_format_param(pe0 + 3u, v, buf, sizeof buf) || !text_ok(buf))
                    ok = 0;
                n += 2u;
            }
            {   /* the desk's menu rows for the same two knobs */
                int r;
                const int k2 = sk_host_param_name_count(pe0 + 2u, 0);
                const int k3 = sk_host_param_name_count(pe0 + 3u, 0);
                for (r = 0; r < k2; r++)
                    if (sk_host_param_name_value(pe0 + 2u, 0, r) < 0)
                        ok = 0;
                for (r = 0; r < k3; r++)
                    if (sk_host_param_name_value(pe0 + 3u, 0, r) < 0)
                        ok = 0;
                printf("format: LOFI WAV# %d names, DCY %d names\n", k2, k3);
            }
        }
    }
    check("LOFI WAVE = WRAM (WAV#) and CHIP = STEP (DCY), 0..127", ok, n);

    printf("format test %s\n", bad ? "FAILED" : "passed");
    return bad ? 1 : 0;
}
