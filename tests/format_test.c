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

/* ui_input() runs in the render, so a queued detent needs a few hundred ms of audio to land. */
static void render_ms(uint32_t ms)
{
    static float L[512], R[512];
    uint32_t frames = (uint32_t) ((uint64_t) ms * (uint64_t) SK_HOST_FS / 1000u);
    while (frames) {
        const uint32_t n = frames < 512u ? frames : 512u;
        sk_host_render_f32(L, R, n);
        frames -= n;
    }
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

    /* 6: the WHEEL's EDIT page is the organ, so its eight knobs are the first eight drawbars and the
     * strip must say so. It used to describe the HOME bank on a page: knob 5 read "2.7'" while the
     * turn landed in G_BPM (the label and the routing disagreed). sk_host.c's page_knobs() and
     * ui_input.c's page branch are the two halves of that promise, so assert the half a test can
     * see — every label, in footage order — and that the ids the page edits are the drawbars. */
    n = 0;
    ok = 1;
    {
        static const char *const feet[8] = {"16'", "5.3'", "8'", "4'", "2.7'", "2'", "1.6'", "1.3'"};
        const int wheel = find_engine("WHEEL");
        uint32_t pg = 64u;
        if (wheel < 0) {
            printf("       no WHEEL engine in the desk table\n");
            ok = 0;
        } else {
            uint32_t ids[8];
            sk_host_engine_factory_apply((uint32_t) wheel, 0);
            sk_host_ui_pump();
            for (p = 0; p < 64u; p++)
                if (!strcmp(page_title(p), "EDIT")) {
                    pg = p;
                    break;
                }
            if (pg >= 64u || sk_host_goto_page(pg) != 0) {
                printf("       no EDIT page to go to (pg %u)\n", pg);
                ok = 0;
            } else {
                if (sk_host_page_params(pg, ids) != 8u) {
                    printf("       page %u is not an eight-knob engine page\n", pg);
                    ok = 0;
                }
                if (ids[4] != sk_host_pe0_id() + 4u) { /* the 2 2/3' bar is E4 */
                    printf("       slot 4 is id %d, not E4 (%u)\n", ids[4], sk_host_pe0_id() + 4u);
                    ok = 0;
                }
                for (slot = 0; slot < 8u; slot++) {
                    if (strcmp(sk_host_page_param_label(pg, slot), feet[slot]) != 0) {
                        printf("       slot %u card says '%s', not '%s'\n", slot,
                               sk_host_page_param_label(pg, slot), feet[slot]);
                        ok = 0;
                    }
                    if (strcmp(sk_host_knob_label(slot), feet[slot]) != 0) {
                        printf("       slot %u strip says '%s', not '%s'\n", slot, sk_host_knob_label(slot), feet[slot]);
                        ok = 0;
                    }
                    n++;
                }
                /* the ninth bar has no knob: the desk's row is eight, so slot 8 is unlabelled */
                if (sk_host_page_param_label(pg, 8u)[0]) {
                    printf("       slot 8 says '%s', not nothing\n", sk_host_page_param_label(pg, 8u));
                    ok = 0;
                }
                n++;
            }
        }
        printf("format: WHEEL EDIT knob row %s\n", ok ? "reads as the drawbars" : "DRIFTED");
    }
    check("WHEEL EDIT: knob 5 is the 2 2/3' bar, and the strip says so", ok, n);

    /* 7: and it must *turn* the bar. The bug was a turn that looked right (the strip said 2.7') but
     * landed in the tempo: on a page the physical knob row kept its old jobs and slot 4 was G_BPM.
     * Drive the real path — sk_host_enc_phys() -> ui_input() in the render — on the organ and on a
     * synth, where P5h says knob 5 is card 5 (EDIT's E4), never the tempo. */
    n = 0;
    ok = 1;
    {
        const uint32_t pe0 = sk_host_pe0_id();
        const int32_t tempo = 120;
        const int wheel = find_engine("WHEEL");
        uint32_t pg = 64u;
        if (wheel < 0) {
            ok = 0;
        } else {
            sk_host_engine_factory_apply((uint32_t) wheel, 0);
            sk_host_ui_pump();
            for (p = 0; p < 64u; p++)
                if (!strcmp(page_title(p), "EDIT")) {
                    pg = p;
                    break;
                }
            if (pg >= 64u || sk_host_goto_page(pg) != 0) {
                printf("       no WHEEL EDIT page to turn\n");
                ok = 0;
            } else {
                render_ms(30);
                sk_host_set_gparam(0u, tempo); /* G_BPM */
                sk_host_set_trk_param(0u, pe0 + 4u, 0);
                sk_host_enc_phys(4u, 3); /* knob 5 */
                render_ms(500);
                if (sk_host_trk_param(0u, pe0 + 4u) == 0) {
                    printf("       knob 5 did not move the 2 2/3' bar\n");
                    ok = 0;
                }
                if (sk_host_get_gparam(0u) != tempo) {
                    printf("       knob 5 moved the tempo to %d\n", (int) sk_host_get_gparam(0u));
                    ok = 0;
                }
                n += 2u;
                /* every knob of the row is a bar, so slot 7 must not be the preset browser either */
                sk_host_set_trk_param(0u, pe0 + 7u, 0);
                sk_host_enc_phys(7u, 3);
                render_ms(500);
                if (sk_host_trk_param(0u, pe0 + 7u) == 0)
                    ok = 0;
                n++;
            }
        }
        /* the other engines read the row the same way: knob 5 is card 5, and the tempo never moves */
        {
            int lofi = wheel >= 0 ? find_engine("LOFI") : -1;
            if (lofi < 0) {
                ok = 0;
            } else {
                pg = 64u;
                sk_host_engine_factory_apply((uint32_t) lofi, 0);
                sk_host_ui_pump();
                for (p = 0; p < 64u; p++)
                    if (!strcmp(page_title(p), "EDIT")) {
                        pg = p;
                        break;
                    }
                if (pg >= 64u || sk_host_goto_page(pg) != 0) {
                    ok = 0;
                } else {
                    render_ms(30);
                    sk_host_set_gparam(0u, tempo);
                    sk_host_set_trk_param(0u, pe0 + 4u, 0);
                    sk_host_enc_phys(4u, 3);
                    render_ms(500);
                    if (sk_host_get_gparam(0u) != tempo) {
                        printf("       a synth page's knob 5 moved the tempo\n");
                        ok = 0;
                    }
                    if (sk_host_trk_param(0u, pe0 + 4u) == 0) {
                        printf("       a synth page's knob 5 did not move its card 5\n");
                        ok = 0;
                    }
                    n++;
                }
            }
        }
        printf("format: WHEEL EDIT knob 5 %s\n", ok ? "turns its drawbar" : "DRIFTED");
    }
    check("WHEEL EDIT: knob 5 turns the 2 2/3' bar, not the tempo (synths: card 5)", ok, n);

    printf("format test %s\n", bad ? "FAILED" : "passed");
    return bad ? 1 : 0;
}
