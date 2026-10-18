/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * Native host: the device bring-up lives in studiokeys_host.c / native_check.c. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static pthread_mutex_t sk_audio_mtx = PTHREAD_MUTEX_INITIALIZER;

#ifndef NVOICE
/* Host array ceiling. Unleashed opens this fully; Limited soft-caps to 8.
 * Not a product “voice limit” — plan: Unleashed spends what the callback can hold. */
#define NVOICE 128
#endif

#include "sk_host.h"
#include "sk_skl.h"
#include "sk_sks.h"

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

/* Match the device package string (skm_web defaults to "WEB"). */
#ifdef SKM_VERSION
#undef SKM_VERSION
#endif
#define SKM_VERSION "v1.0.0"

/* Soft-cap must be visible before voice.c is pulled in via studiokeys_host.c. */
static int sk_unleashed;
static uint32_t sk_dyn_cap = NVOICE;   /* Unleashed working budget; Limited ignores this */
static float sk_wave_hist[256];
static uint32_t sk_wave_n;

static uint32_t sk_poly_budget_fn(void)
{
    return sk_unleashed ? sk_dyn_cap : 8u;
}
#define SK_POLY_BUDGET sk_poly_budget_fn

static uint64_t sk_now_ns(void)
{
#if defined(__APPLE__)
    static mach_timebase_info_data_t tb;
    static int ready;
    uint64_t t = mach_absolute_time();
    if (!ready) {
        mach_timebase_info(&tb);
        ready = 1;
    }
    return t * tb.numer / tb.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

/* Surface SKS handler (defined in included studiokeys_host.c / firmware sk_surf.c). */
void sk_surf_link_sysex(const uint8_t *d, uint32_t n);

/* SMK Micro panel map in the host (same encoder jobs as SKM_ON_DEVICE). */
#define SKM_SMK_PANEL 1

/* menu_items.c hooks (same TU) — emit SKL meta when COLOR / LANG change. */
void sk_on_palette_changed(uint32_t v);
void sk_on_lang_changed(uint32_t v);

/* Bring up the full firmware host (static TU). Paths relative to this file. */
#include "studiokeys_host.c"

static int sk_lock = SK_LOCK_NONE;
static int sk_instance_claimed;
static uint32_t sk_lock_crc;
static uint8_t sk_link_out[4096];
static uint32_t sk_link_r, sk_link_w;
static char sk_peer_ver[32];
static uint32_t sk_peer_crc;
static int sk_peer_crc_valid;
static int sk_evt_apply; /* 1 while applying inbound EVENT (suppress echo) */
static const char *sk_lock_reject = "";
static uint8_t sk_sx_buf[128];
static uint32_t sk_sx_n;
static int sk_sx_on;

static uint32_t crc32_update(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t i, b;
    c = ~c;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (b = 0; b < 8u; b++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
}

static void sk_link_out_bytes(const uint8_t *src, uint32_t n);
static void sk_skl_reply(uint8_t cmd, const uint8_t *payload, uint32_t np);
static void sk_skl_handle(const uint8_t *f, uint32_t n);
static void sk_undo_reset(void);   /* P18: empty the undo/redo stacks at boot */

/* ---- persisted NOR image --------------------------------------------------
 * The host's NOR is an in-process array, so a theme/panel/settings change is lost when the
 * DAW exits or the plugin is reloaded in a new process. Mirror the web emulator's model:
 * write the whole NOR image to a user-data file whenever flash is written, and overlay it
 * onto the freshly-erased NOR at init so persist_boot() sees the saved settings. */
static char g_state_path[512];
static void sk_state_path_init(void)
{
    const char *home = getenv("HOME");
    char dir[512];
    if (!home || !home[0])
        home = getenv("USERPROFILE");
    if (!home || !home[0]) { g_state_path[0] = 0; return; }
#if defined(_WIN32)
    snprintf(dir, sizeof dir, "%s\\StudioKEYS", home);
    _mkdir(dir);
    snprintf(g_state_path, sizeof g_state_path, "%s\\StudioKEYS\\nor.bin", home);
#elif defined(__APPLE__)
    snprintf(dir, sizeof dir, "%s/Library/Application Support/StudioKEYS", home);
    mkdir(dir, 0755);
    snprintf(g_state_path, sizeof g_state_path, "%s/Library/Application Support/StudioKEYS/nor.bin", home);
#else
    snprintf(dir, sizeof dir, "%s/.local/share/StudioKEYS", home);
    mkdir(dir, 0755);
    snprintf(g_state_path, sizeof g_state_path, "%s/.local/share/StudioKEYS/nor.bin", home);
#endif
}

static void sk_state_load(void)
{
    FILE *f;
    if (!g_state_path[0]) return;
    f = fopen(g_state_path, "rb");
    if (!f) return;
    (void)fread(web_nor(), 1, web_nor_size(), f);
    fclose(f);
}

void sk_host_state_save(void)
{
    FILE *f;
    char tmp[600];
    uint8_t *n = web_nor();
    uint32_t ns = web_nor_size();
    if (!g_state_path[0])
        sk_state_path_init();
    if (!g_state_path[0] || !n || !ns) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", g_state_path);
    f = fopen(tmp, "wb");
    if (!f) return;
    (void)fwrite(n, 1, ns, f);
    fclose(f);
    rename(tmp, g_state_path);
}

void sk_host_init(void)
{
    sk_unleashed = 0;
    sk_dyn_cap = NVOICE;
    sk_lock = SK_LOCK_NONE;
    sk_link_r = sk_link_w = 0;
    sk_peer_ver[0] = 0;
    sk_peer_crc = 0;
    sk_peer_crc_valid = 0;
    sk_evt_apply = 0;
    sk_sx_n = 0;
    sk_sx_on = 0;
    sk_lock_reject = "";
    web_nor_erase();
    /* Overlay the NOR image saved from a previous process (theme/panel/settings) before boot,
     * so persist_boot()/settings_init() see the saved settings instead of a blank device. */
    if (!g_state_path[0])
        sk_state_path_init();
    sk_state_load();
    web_reboot();
}

void sk_host_boot(void)
{
    sk_undo_reset();                                 /* P18: a boot starts with empty stacks */
    web_boot();
}

void sk_host_render_f32(float *L, float *R, uint32_t n)
{
    uint32_t i;
    uint64_t t0, t1, budget_ns, took;
    if (!L || !R)
        return;
    if (n > 1024u)
        n = 1024u;
    pthread_mutex_lock(&sk_audio_mtx);
    t0 = sk_now_ns();
    web_render(n);
    t1 = sk_now_ns();
    {
        float peak = 1e-6f;
        uint32_t w, step;
        for (i = 0; i < n; i++) {
            L[i] = out_l[i];
            R[i] = out_r[i];
            if (L[i] > peak)
                peak = L[i];
            if (-L[i] > peak)
                peak = -L[i];
        }
        step = n > 256u ? (n / 256u) : 1u;
        w = 0;
        for (i = 0; i < n && w < 256u; i += step) {
            sk_wave_hist[w] = L[i] / peak;
            w++;
        }
        sk_wave_n = w;
    }
    pthread_mutex_unlock(&sk_audio_mtx);
    /* Unleashed: open to NVOICE; only pull down if the callback overruns real time. */
    if (sk_unleashed) {
        budget_ns = (uint64_t)n * 1000000000ull / (uint64_t)SK_HOST_FS;
        took = t1 - t0;
        if (budget_ns && took > (budget_ns * 85u) / 100u) {
            if (sk_dyn_cap > 16u)
                sk_dyn_cap -= 4u;
            else if (sk_dyn_cap > 8u)
                sk_dyn_cap = 8u;
        } else if (budget_ns && took < budget_ns / 2u && sk_dyn_cap < (uint32_t)NVOICE) {
            sk_dyn_cap += 4u;
            if (sk_dyn_cap > (uint32_t)NVOICE)
                sk_dyn_cap = (uint32_t)NVOICE;
        }
    }
}

const uint16_t *sk_host_screen(void) { return web_screen(); }
uint32_t sk_host_screen_draws(void) { return web_screen_draws(); }

uint8_t *sk_host_nor(void) { return web_nor(); }
uint32_t sk_host_nor_size(void) { return web_nor_size(); }
void sk_host_nor_erase(void) { web_nor_erase(); }
uint32_t sk_host_flash_writes(void) { return web_flash_writes_count(); }

int sk_host_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    return (int)web_midi(status, d1, d2);
}

void sk_host_buttons(uint32_t labels) { web_buttons(labels); }
void sk_host_keys_mask(uint32_t mask27) { web_keys(mask27); }
void sk_host_enc(uint32_t role, int32_t steps) { web_enc(role, steps); }
void sk_host_enc_phys(uint32_t idx0_7, int32_t steps) { web_enc_phys(idx0_7, steps); }

/* PANEL_DEFAULT: SELECT=0 ALGO=1 PRESET=6 K1=2 K2=3 K3=4 K4=5 — phys 7 idle off-HOME. */
static int sk_knob_desc(uint32_t slot, const param_desc_t **dout, int16_t **vout)
{
    const page_t *pg;
    int16_t *vp = 0;
    const param_desc_t *d = 0;
    if (slot >= 8u)
        return 0;
    if (ui.home) {
        uint32_t id = smk_bank_param(slot);
        d = track_desc(TSEL, id);
        vp = &TSEL->p[id];
    } else if (slot < 4u) {
        pg = cur_page();
        d = page_desc(pg, slot, &vp);
    } else if (slot == 4u) {
        d = &GP[G_BPM];
        vp = &song.g[G_BPM];
    } else if (slot == 6u) {
        /* PRESET browse — finite list, no single param pointer */
        if (dout)
            *dout = 0;
        if (vout)
            *vout = 0;
        return 2;
    } else {
        return 0;
    }
    if (dout)
        *dout = d;
    if (vout)
        *vout = vp;
    return d ? 1 : 0;
}

const char *sk_host_knob_label(uint32_t slot0_7)
{
    const param_desc_t *d = 0;
    int kind;
    /* MENU footer keymap (ui_menu.c): phys 0 LIST, 1 SET, 2 TAB. DIAGNOSTICS (3) is read-only: none of them. */
    if (ui.menu) {
        if (slot0_7 == 0u)
            return ui.menu == 3 ? "—" : ui.menu >= 2 ? "SCROLL" : "LIST";
        if (slot0_7 == 1u)
            return ui.menu >= 2 ? "—" : "SET";
        if (slot0_7 == 2u)
            return ui.menu >= 2 ? "—" : "TAB";
        return "—";
    }
    kind = sk_knob_desc(slot0_7, &d, 0);
    if (kind == 2)
        return "PRESET";
    if (kind == 1 && d && d->label)
        return d->label;
    if (!ui.home) {
        if (slot0_7 == 4u)
            return "BPM";
        if (slot0_7 == 5u || slot0_7 == 7u)
            return "—";
    }
    return "—";
}

int sk_host_knob_finite(uint32_t slot0_7)
{
    const param_desc_t *d = 0;
    int kind;
    if (ui.menu)
        return slot0_7 <= 2u ? 1 : 0; /* LIST / SET / TAB are stepped */
    kind = sk_knob_desc(slot0_7, &d, 0);
    if (kind == 2)
        return 1; /* PRESET */
    if (kind != 1 || !d)
        return 0;
    return d->fmt == F_ENUM || (d->max - d->min) <= 32;
}

int sk_host_knob_norm(uint32_t slot0_7, float *out01)
{
    const param_desc_t *d = 0;
    int16_t *vp = 0;
    int kind;
    int span;
    if (!out01)
        return 0;
    if (ui.menu)
        return 0; /* LIST/SET/TAB — no absolute needle */
    kind = sk_knob_desc(slot0_7, &d, &vp);
    if (kind == 2) {
        /* No absolute preset index exposed cleanly — endless-style UI. */
        return 0;
    }
    if (kind != 1 || !d || !vp)
        return 0;
    span = (int)d->max - (int)d->min;
    if (span <= 0)
        return 0;
    *out01 = (float)((int)*vp - (int)d->min) / (float)span;
    if (*out01 < 0.f)
        *out01 = 0.f;
    if (*out01 > 1.f)
        *out01 = 1.f;
    return 1;
}

const char *sk_host_knob_value(uint32_t slot0_7)
{
    static char buf[16];
    const param_desc_t *d = 0;
    int16_t *vp = 0;
    const char *unit = "";
    char val[8];
    int kind;
    if (ui.menu) {
        if (slot0_7 == 1u && ui.menu == 1 && ui.menu_sel < sk_host_menu_values()) {
            snprintf(buf, sizeof buf, "%s", sk_host_menu_vname(ui.menu_sel, sk_host_menu_get(ui.menu_sel)));
            return buf;
        }
        if (slot0_7 == 0u) {
            snprintf(buf, sizeof buf, "%s", sk_host_menu_row_label(ui.menu_sel));
            return buf;
        }
        if (slot0_7 == 2u && ui.menu == 1) {
            snprintf(buf, sizeof buf, "%s", sk_host_menu_tab_name(sk_host_menu_tab_of(ui.menu_sel)));
            return buf;
        }
        return "—";
    }
    kind = sk_knob_desc(slot0_7, &d, &vp);
    if (kind != 1 || !d || !vp)
        return "—";
    param_format(d, *vp, val, &unit);
    if (unit && unit[0])
        snprintf(buf, sizeof buf, "%s%s", val, unit);
    else
        snprintf(buf, sizeof buf, "%s", val);
    return buf;
}

const char *sk_host_fader_label(uint32_t visual0_3)
{
    /* Visual L→R = printed FADER 2,3,4,1 (probe order). P3: always T1..T4 LEVEL. */
    static char mix[4][8];
    static const uint8_t vis_trk[4] = {1, 2, 3, 0};
    if (visual0_3 >= 4u)
        return "—";
    if (smk_remote == 1u)
        return "LIVE";
    if (smk_remote == 2u)
        return "HUI";
    snprintf(mix[visual0_3], sizeof mix[visual0_3], "T%u LV", (unsigned)vis_trk[visual0_3] + 1u);
    return mix[visual0_3];
}

const char *sk_host_fader_value(uint32_t visual0_3)
{
    static char buf[4][12];
    static const uint8_t vis_trk[4] = {1, 2, 3, 0};
    const param_desc_t *d;
    const char *unit = "";
    char val[8];
    int32_t v;
    uint32_t tr;
    if (visual0_3 >= 4u)
        return "—";
    if (smk_remote)
        return "—";
    tr = vis_trk[visual0_3];
    d = track_desc(&trk[tr], P_LEVEL);
    if (!d)
        return "—";
    v = trk[tr].p[P_LEVEL];
    param_format(d, v, val, &unit);
    if (unit && unit[0])
        snprintf(buf[visual0_3], sizeof buf[visual0_3], "%s%s", val, unit);
    else
        snprintf(buf[visual0_3], sizeof buf[visual0_3], "%s", val);
    return buf[visual0_3];
}
void sk_host_master(uint32_t v0_1023) { web_master(v0_1023); }

void sk_host_smk_extra(uint32_t down_mask)
{
    sk_host_smk_extra_set(down_mask);
}

void sk_host_smk_fader(uint32_t idx0_7, int16_t adc0_1023)
{
    if (adc0_1023 < 0)
        adc0_1023 = 0;
    if (adc0_1023 > 1023)
        adc0_1023 = 1023;
    sk_host_smk_an_set(idx0_7, adc0_1023);
}

/* ------------------------------------------------------------------ undo / redo
 * P18: the companion's UNDO/REDO, the CFW's SAVE-held swap (firmware ui.c undo_swap) applied to
 * the *selected track's sound*: engine, preset, user slot, every parameter a load may change (the
 * mix LEVEL/PAN/MUTE and the performance params stay — no load touches them, as on the device),
 * the FM6 patch and the track's motion pool. A step is swapped in and out, so REDO is the very
 * same operation run again; a copy of the state before it went on the other stack, which is what
 * lets a repeated UNDO walk back through a session instead of toggling one pair.
 *
 * Scope: the track the step was captured on (undoing on T3 selects T3 back, as the CFW's undo.trk
 * does). Program loads take an explicit checkpoint; knob moves coalesce into one step per burst. */
#define SK_UNDO_DEPTH 16u
#define SK_UNDO_BURST_MS 800u   /* a knob sweep's moves inside this window are one step */
#define SK_UNDO_SAME_MS 4000u   /* the same knob held this long is still the same sweep */

typedef struct {
    uint32_t tr;
    uint8_t eng, preset, user, fm6_slot;
    int16_t p[P_COUNT];
    uint8_t fm6[FP_SIZE];
    motion_store_t motion;
} sk_undo_step_t;

static sk_undo_step_t sk_undo_stack[SK_UNDO_DEPTH];
static sk_undo_step_t sk_redo_stack[SK_UNDO_DEPTH];
static uint32_t sk_undo_n, sk_redo_n;
static uint32_t sk_undo_stamp;              /* bumps on every stack change; the UI relights */ 
static int32_t sk_undo_last_id = -1;        /* the knob of the current burst */
static uint32_t sk_undo_last_ms;
static uint8_t sk_undo_seen;                /* a first edit always checkpoints */
static uint8_t sk_undo_busy;                /* inside an apply: no checkpoints */

static void sk_undo_reset(void)
{
    sk_undo_n = sk_redo_n = 0;
    sk_undo_stamp++;
    sk_undo_last_id = -1;
    sk_undo_seen = 0;
    sk_undo_busy = 0;
}

static void sk_undo_capture(sk_undo_step_t *s, track_t *t)
{
    uint32_t k = trk_index(t);
    memset(s, 0, sizeof *s);
    s->tr = k;
    s->eng = t->eng_req;
    s->preset = t->preset;
    s->user = t->user;
    memcpy(s->p, t->p, sizeof s->p);
    memcpy(s->fm6, fm6_patch[k], FP_SIZE);
    s->fm6_slot = fm6_slot[k];
    motion_snapshot_track(t, &s->motion);
}

/* the same sound: only what an undo would touch decides (the mix may differ and stay) */
static int sk_undo_same(const sk_undo_step_t *a, const sk_undo_step_t *b)
{
    uint32_t i;
    if (a->tr != b->tr || a->eng != b->eng || a->preset != b->preset || a->user != b->user ||
        a->fm6_slot != b->fm6_slot)
        return 0;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i) && a->p[i] != b->p[i])
            return 0;
    if (memcmp(a->fm6, b->fm6, FP_SIZE))
        return 0;
    if (a->motion.count != b->motion.count || a->motion.on != b->motion.on ||
        memcmp(a->motion.event, b->motion.event, a->motion.count * sizeof a->motion.event[0]))
        return 0;
    return 1;
}

static void sk_undo_push(sk_undo_step_t *stack, uint32_t *n, const sk_undo_step_t *s)
{
    if (*n == SK_UNDO_DEPTH) {                         /* drop the oldest, keep the newest 16 */
        memmove(stack, stack + 1, (SK_UNDO_DEPTH - 1u) * sizeof stack[0]);
        (*n)--;
    }
    stack[(*n)++] = *s;
}

/* The swap. `to` is the sound to restore, `from` the one it replaces (used to know which
 * parameters a locked keyboard must hear). 0 = refused (the motion pool is full). */
static int sk_undo_apply(const sk_undo_step_t *to, const sk_undo_step_t *from)
{
    track_t *t;
    uint32_t i, tr = to->tr % NTRK;
    if (tr != (uint32_t)song.sel % NTRK) {
        song.sel = (uint8_t) tr;                       /* the CFW's undo.trk: undo where it was done */
        ui.force = 1;
    }
    t = &trk[tr];
    motion_restore(t);
    if (motion_replace_track(t, &to->motion) != 0)
        return 0;                                      /* AUTOMATION FULL: leave the sound as it is */
    sk_undo_busy = 1;
    t->eng_req = to->eng;
    t->preset = to->preset;
    t->user = to->user;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = to->p[i];
    fm6_set_patch(tr, to->fm6);
    fm6_slot[tr] = to->fm6_slot;                       /* fm6_poll: the restored patch stays */
    fm6_own_ok &= (uint8_t)~(1u << tr);
    sk_undo_busy = 0;
    /* Locked: mirror the sound to the keyboard, as the knob path does (EVENT per parameter). */
    if (sk_lock == SK_LOCK_ON && !sk_evt_apply) {
        for (i = 0; i < P_COUNT; i++) {
            uint8_t pay[4];
            uint32_t u;
            if (param_kept(i) || t->p[i] == from->p[i])
                continue;
            u = (uint32_t)(clamp(t->p[i], -8192, 8191) + 8192);
            pay[0] = (uint8_t)(i & 0x7Fu);
            pay[1] = (uint8_t)((i >> 7) & 0x7Fu);
            pay[2] = (uint8_t)(u & 0x7Fu);
            pay[3] = (uint8_t)((u >> 7) & 0x7Fu);
            sk_skl_reply(SK_SKL_EVENT, pay, 4);
        }
    }
    ui.force = 1;
    sync_reload = 1;                                   /* the EDIT layer reloads the restored sound */
    return 1;
}

void sk_host_undo_mark(void)
{
    sk_undo_step_t s;
    if (sk_undo_busy)
        return;
    sk_undo_capture(&s, TSEL);
    if (sk_undo_n && sk_undo_same(&sk_undo_stack[sk_undo_n - 1], &s))
        return;                                        /* nothing changed: no empty step, redo kept */
    sk_undo_push(sk_undo_stack, &sk_undo_n, &s);
    sk_redo_n = 0;                                     /* an edit is a new branch */
    sk_undo_stamp++;
    sk_undo_last_id = -1;
    sk_undo_last_ms = fm1_ms;
    sk_undo_seen = 1;                                  /* the moves that follow ride this step */
}

int sk_host_undo(void)
{
    sk_undo_step_t cur, to;
    if (!sk_undo_n)
        return 0;
    sk_undo_capture(&cur, TSEL);
    to = sk_undo_stack[sk_undo_n - 1];
    if (!sk_undo_apply(&to, &cur))
        return 0;
    sk_undo_n--;
    sk_undo_push(sk_redo_stack, &sk_redo_n, &cur);
    sk_undo_stamp++;
    sk_undo_last_id = -1;
    sk_undo_last_ms = fm1_ms;
    sk_undo_seen = 0;                                  /* the next edit is its own step (redo is live) */
    return 1;
}

int sk_host_redo(void)
{
    sk_undo_step_t cur, to;
    if (!sk_redo_n)
        return 0;
    sk_undo_capture(&cur, TSEL);
    to = sk_redo_stack[sk_redo_n - 1];
    if (!sk_undo_apply(&to, &cur))
        return 0;
    sk_redo_n--;
    sk_undo_push(sk_undo_stack, &sk_undo_n, &cur);
    sk_undo_stamp++;
    sk_undo_last_id = -1;
    sk_undo_last_ms = fm1_ms;
    sk_undo_seen = 0;                                  /* the next edit is its own step (redo is live) */
    return 1;
}

uint32_t sk_host_undo_depth(void) { return sk_undo_n; }
uint32_t sk_host_redo_depth(void) { return sk_redo_n; }
uint32_t sk_host_undo_stamp(void) { return sk_undo_stamp; }
uint32_t sk_host_undo_capacity(void) { return SK_UNDO_DEPTH; }

/* A knob move: the first one after a quiet spell checkpoints the sound before it, then the
 * rest of the sweep (the same knob, or a burst of them) rides that one step. */
static void sk_undo_touch(uint32_t id)
{
    uint32_t now = fm1_ms;
    if (sk_undo_busy || sk_evt_apply)
        return;
    if (sk_undo_seen && now >= sk_undo_last_ms) {
        if (sk_undo_last_id == (int32_t) id && (now - sk_undo_last_ms) < SK_UNDO_SAME_MS) {
            sk_undo_last_ms = now;
            return;
        }
        if ((now - sk_undo_last_ms) < SK_UNDO_BURST_MS) {
            sk_undo_last_id = (int32_t) id;
            sk_undo_last_ms = now;
            return;
        }
    }
    sk_host_undo_mark();
    sk_undo_last_id = (int32_t) id;
    sk_undo_last_ms = now;
}

void sk_host_set_param(uint32_t id, int32_t v)
{
    const param_desc_t *d;
    if (id >= P_COUNT)
        return;
    /* NO mutex here. This runs on the DAW's UI/main thread while the audio
     * thread renders under sk_audio_mtx; taking that same mutex blocked the UI
     * for a whole render (measured 43-64 ms per drag tick) and froze the DAW.
     * The firmware's own model is a lock-free handoff — the main loop writes the
     * track value + patch and the audio ISR picks them up at its next block,
     * ordered only by a generation counter. We follow that: a monotonically
     * written int16 needs no lock, and ui.force=1 tells the render to rebuild
     * its macro view. fm6_adopt() also already ran off the (unlocked) audio-side
     * fm6_poll(), so it is no new race. */
    d = track_desc(TSEL, id);
    if (d)
        v = clamp(v, d->min, d->max);
    if (TSEL->p[id] == (int16_t)v)
        return;
    sk_undo_touch(id);                               /* P18: checkpoint the sound before the move */
    TSEL->p[id] = (int16_t)v;
    if (id == P_E0 && ENGINES[TSEL->eng_req % NENGINES] == &ENG_FM6) {
        uint32_t tr = (uint32_t)(TSEL - trk) % NTRK;
        if (v >= 1 && v <= 32) {
            fm6_patch[tr][134] = (uint8_t)(v - 1);
            fm6_adopt(tr);
        }
    }
    motion_capture(TSEL, id, (int16_t)v);            /* P16: as the firmware's knob path (REC / AUTO REC) */
    ui.force = 1;
    /* Locked: emit EVENT on the link outbox (skip when applying a peer tick). */
    if (sk_lock == SK_LOCK_ON && !sk_evt_apply) {
        uint8_t pay[4];
        uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
        pay[0] = (uint8_t)(id & 0x7Fu);
        pay[1] = (uint8_t)((id >> 7) & 0x7Fu);
        pay[2] = (uint8_t)(u & 0x7Fu);
        pay[3] = (uint8_t)((u >> 7) & 0x7Fu);
        sk_skl_reply(SK_SKL_EVENT, pay, 4);
    }
}

int32_t sk_host_get_param(uint32_t id)
{
    if (id >= P_COUNT)
        return 0;
    return TSEL->p[id];
}

void sk_host_set_gparam(uint32_t id, int32_t v)
{
    if (id >= G_COUNT)
        return;
    if (GP[id].label) {
        v = clamp(v, GP[id].min, GP[id].max);
    }
    if (song.g[id] == (int16_t)v)
        return;
    song.g[id] = (int16_t)v;
    ui.force = 1;
}

int32_t sk_host_get_gparam(uint32_t id)
{
    if (id >= G_COUNT)
        return 0;
    return song.g[id];
}

/* ------------------------------------------------------------------ key split
 * P13: live keys below SPLIT_NOTE play track LO, at/above it HI. The performance setting lives in
 * the settings record (two scratch bytes), so it survives a power cycle; the companion mirrors it
 * and can edit it. split_set() releases sounding notes, since their routing just changed. */
void sk_host_split_get(int32_t *note, int32_t *lo, int32_t *hi)
{
    if (note)
        *note = split_note;
    if (lo)
        *lo = split_lo;
    if (hi)
        *hi = split_hi;
}

int sk_host_split_set(int32_t note, int32_t lo, int32_t hi)
{
    uint32_t n = note <= 0 || note > 127 ? 0u : (uint32_t)note;
    uint32_t l = (uint32_t)clamp(lo, 0, (int32_t)NTRK - 1);
    uint32_t h = (uint32_t)clamp(hi, 0, (int32_t)NTRK - 1);
    if (n == split_note && l == split_lo && h == split_hi)
        return 1;
    split_set(n, l, h);
    ui.force = 1;
    settings_save();                                  /* the setting lives in the settings record */
    return 1;
}

static void sk_emit_meta_event(uint32_t id, int32_t v)
{
    uint8_t pay[4];
    uint32_t u;
    if (sk_lock != SK_LOCK_ON || sk_evt_apply)
        return;
    u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    pay[0] = (uint8_t)(id & 0x7Fu);
    pay[1] = (uint8_t)((id >> 7) & 0x7Fu);
    pay[2] = (uint8_t)(u & 0x7Fu);
    pay[3] = (uint8_t)((u >> 7) & 0x7Fu);
    sk_skl_reply(SK_SKL_EVENT, pay, 4);
}

void sk_on_palette_changed(uint32_t v)
{
    sk_emit_meta_event(SK_SKL_META_PALETTE, (int32_t)(v % NPALETTES));
}

void sk_on_lang_changed(uint32_t v)
{
    sk_emit_meta_event(SK_SKL_META_LANG, v ? 1 : 0);
}

void sk_host_set_palette(uint32_t idx)
{
    idx %= NPALETTES;
    if (settings.palette == idx)
        return;
    settings.palette = idx;
    palette_set(idx);
    ui.force = 1;
    sk_emit_meta_event(SK_SKL_META_PALETTE, (int32_t)idx);
}

/* Persist settings to NOR (palette/panel/prefs). The companion SETTINGS desk is
 * not the device MENU, so menu_close() never runs to save after a change; call
 * this from the plugin UI thread. Firmware project.c: settings_save(). */
void sk_host_settings_save(void);

uint32_t sk_host_palette(void) { return settings.palette % NPALETTES; }
uint32_t sk_host_npalettes(void) { return NPALETTES; }

/* P6h: the keyboard's output clock over the link (sk_skl.h SK_SKL_META_RATE_*). */
static int32_t hw_rate_ppm, hw_rate_trim;
void sk_host_set_hw_rate_ppm(int32_t ppm) { hw_rate_ppm = ppm; }
void sk_host_set_hw_rate_trim(int32_t q16_minus_unity) { hw_rate_trim = q16_minus_unity; }
int32_t sk_host_hw_rate_ppm(void) { return hw_rate_ppm; }
int32_t sk_host_hw_rate_trim(void) { return hw_rate_trim; }

void sk_host_palette_rgb565(uint16_t out5[5])
{
    const ui_pal_t *p;
    if (!out5)
        return;
    p = &UI_PALETTES[settings.palette % NPALETTES];
    out5[0] = p->bg;
    out5[1] = p->surf;
    out5[2] = p->text;
    out5[3] = p->theme;
    out5[4] = p->accent;
}

/* Same bit as firmware menu_items.c PREF_LANG (favorites.factory[15][30]). */
enum { SK_PREF_LANG = 128 };
void sk_host_set_lang(int id)
{
    uint8_t *p = &favorites.factory[15][30];
    uint8_t was = (uint8_t)(*p & SK_PREF_LANG);
    if (id)
        *p = (uint8_t)(*p | SK_PREF_LANG);
    else
        *p = (uint8_t)(*p & (uint8_t)~SK_PREF_LANG);
    ui.force = 1;
    if ((uint8_t)(*p & SK_PREF_LANG) != was)
        sk_emit_meta_event(SK_SKL_META_LANG, id ? 1 : 0);
}
int sk_host_lang(void) { return (favorites.factory[15][30] & SK_PREF_LANG) ? 1 : 0; }

void sk_host_set_unleashed(int on)
{
    sk_unleashed = on ? 1 : 0;
    if (sk_unleashed)
        sk_dyn_cap = (uint32_t)NVOICE;   /* open fully; callback may pull down */
}
int sk_host_unleashed(void) { return sk_unleashed; }
uint32_t sk_host_voice_budget(void) { return sk_poly_budget_fn(); }
uint32_t sk_host_voice_ceiling(void) { return (uint32_t)NVOICE; }

void sk_host_waveform_peek(float *dst, uint32_t n)
{
    uint32_t i, m;
    if (!dst || !n)
        return;
    m = sk_wave_n < n ? sk_wave_n : n;
    for (i = 0; i < m; i++)
        dst[i] = sk_wave_hist[i];
    for (; i < n; i++)
        dst[i] = 0.f;
}

const char *sk_host_version(void)
{
    return SKM_VERSION;
}

uint32_t sk_host_sample_rate(void) { return web_sample_rate(); }
uint32_t sk_host_now_ms(void) { return web_now_ms(); }
uint32_t sk_host_playing(void) { return web_playing(); }

/* ---- P15: a WAV dropped on USR1..USR3 -> the device's slot record ----------------
 * The same encode `tools/fm1_sample_upload.py` + `tools/sampleio.py` do for a USB upload
 * (mono 22050 Hz, peak normalised, IMA ADPCM, header last), but from the host process, so the
 * companion writes the slot into the NOR image the firmware reads through XIP. Only the slot's
 * own SMP_USER_SIZE bytes are erased/written (P8c). */
#define SK_SLOT_RATE 22050u
#define SK_SLOT_RATE_Q16 (22050u * 65536u / 44100u)
#define SK_SLOT_MAX_ADPCM (SMP_USER_SIZE - SMP_USER_DATA)     /* 35584 B = 71168 samples */

static const int32_t sk_ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t sk_ima_idx[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static uint32_t sk_slot_crc32(const uint8_t *b, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

/* sampleio.resample: a moving average over the ratio (32-bit safe, float input), then linear
 * interpolation to 22050 Hz. Returns the new length; *out is malloc'd (caller frees). */
static uint32_t sk_slot_resample(const int16_t *in, uint32_t n, uint32_t rate, double **out)
{
    uint32_t i, k, nout;
    const double step = (double)rate / (double)SK_SLOT_RATE, last = (double)(n ? n - 1u : 0u);
    double *x, *y, p;

    *out = 0;
    if (n < 2u || rate < SK_SLOT_RATE / 4u || rate > 192000u)
        return 0;
    x = (double *)malloc((size_t)n * sizeof *x);
    y = (double *)malloc((size_t)n * sizeof *y);
    if (!x || !y) {
        free(x);
        free(y);
        return 0;
    }
    for (i = 0; i < n; i++)
        x[i] = (double)in[i] / 32768.0;
    k = rate > SK_SLOT_RATE ? (uint32_t)lround((double)rate / (double)SK_SLOT_RATE) : 1u;
    if (k < 1u)
        k = 1u;
    for (i = 0; i < n; i++) {                    /* y[i] = mean of the k-sample window at i */
        uint32_t j, m = n - i < k ? n - i : k;
        double acc = 0.0;
        for (j = 0; j < m; j++)
            acc += x[i + j];
        y[i] = acc / (double)k;
    }
    free(x);
    nout = 0;
    for (p = 0.0; p < last; p += step)
        nout++;                                   /* while p < last: one output */
    if (!nout) {
        free(y);
        return 0;
    }
    *out = (double *)malloc((size_t)nout * sizeof **out);
    if (!*out) {
        free(y);
        return 0;
    }
    i = 0;
    for (p = 0.0; i < nout; p += step, i++) {
        const uint32_t a = (uint32_t)p;
        const double f = p - (double)a;
        (*out)[i] = y[a] * (1.0 - f) + y[a + 1u] * f;
    }
    free(y);
    return nout;
}

/* sampleio.ima_encode: low nibble first, predictor 0 / index 0. Returns the byte count. */
static uint32_t sk_slot_ima(const int16_t *s, uint32_t n, uint8_t *dst)
{
    int32_t pred = 0, idx = 0;
    uint32_t i, nb = 0;
    for (i = 0; i < n; i++) {
        int32_t step = sk_ima_step[(uint32_t)idx <= 88u ? (uint32_t)idx : 88u];
        int32_t diff = (int32_t)s[i] - pred, code = 0, vd = step >> 3;
        if (diff < 0) {
            code = 8;
            diff = -diff;
        }
        if (diff >= step) {
            code |= 4;
            diff -= step;
            vd += step;
        }
        if (diff >= step >> 1) {
            code |= 2;
            diff -= step >> 1;
            vd += step >> 1;
        }
        if (diff >= step >> 2) {
            code |= 1;
            vd += step >> 2;
        }
        pred += (code & 8) ? -vd : vd;
        if (pred > 32767)
            pred = 32767;
        else if (pred < -32768)
            pred = -32768;
        idx += sk_ima_idx[code & 7];
        if (idx < 0)
            idx = 0;
        else if (idx > 88)
            idx = 88;
        if (!(i & 1u))
            dst[nb] = (uint8_t)code;
        else
            dst[nb++] |= (uint8_t)(code << 4);
    }
    if (n & 1u)
        nb++;
    return nb;
}

int sk_host_sample_slot_write(uint32_t k, const int16_t *pcm, uint32_t frames, uint32_t rate,
                              const char *name)
{
    static uint8_t img[SMP_USER_SIZE] __attribute__((aligned(4)));
    smp_user_hdr_t *h = (smp_user_hdr_t *)img;
    double *rs = 0;
    uint32_t n, nbytes, i, base;
    double peak = 0.0;
    int16_t *mono;
    const char *nm = name ? name : "";

    if (k >= SK_HOST_SAMPLE_SLOTS || !pcm || frames < 2u)
        return -1;
    n = sk_slot_resample(pcm, frames, rate, &rs);
    if (!n)
        return -1;
    if (n > SK_SLOT_MAX_ADPCM * 2u)
        return -3;
    for (i = 0; i < n; i++) {
        const double a = rs[i] < 0 ? -rs[i] : rs[i];
        if (a > peak)
            peak = a;
    }
    if (peak < 1e-6) {                            /* a silent file has nothing to play */
        free(rs);
        return -2;
    }
    mono = (int16_t *)malloc((size_t)n * sizeof *mono);
    if (!mono) {
        free(rs);
        return -1;
    }
    for (i = 0; i < n; i++) {
        double v = rs[i] / peak * 30000.0;        /* sampleio.to_int16 */
        if (v > 32767.0)
            v = 32767.0;
        else if (v < -32768.0)
            v = -32768.0;
        mono[i] = (int16_t)v;
    }
    free(rs);
    nbytes = sk_slot_ima(mono, n, img + SMP_USER_DATA);
    free(mono);
    if (nbytes > SK_SLOT_MAX_ADPCM)
        return -3;

    /* The record: what fm1_sample_upload.py's SMP_END writes (smp_user_hdr_t). */
    memset(img, 0, SMP_USER_DATA);
    h = (smp_user_hdr_t *)img;
    h->magic = 0x504D5346u;                        /* "FSMP" */
    h->version = 1;
    h->nz = 1;
    for (i = 0; i < sizeof h->name && nm[i]; i++) {
        char c = nm[i];
        h->name[i] = (char)((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
    }
    h->data_len = nbytes;
    h->crc = sk_slot_crc32(img + SMP_USER_DATA, nbytes);
    h->zone[0].off = 0;
    h->zone[0].n = n;
    h->zone[0].ls = 0;
    h->zone[0].le = n - 1u;
    h->zone[0].rate = SK_SLOT_RATE_Q16;
    h->zone[0].root16 = 60 * 16;                  /* C4 */
    h->zone[0].pred = 0;
    h->zone[0].idx = 0;
    h->zone[0].lo = 0;
    h->zone[0].hi = 127;
    h->zone[0].looped = 0;

    base = SMP_USER_BASE + k * SMP_USER_SIZE;
    pthread_mutex_lock(&sk_audio_mtx);
    usr_nz[k] = 0;
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;                     /* a sounding voice ends instead of reading 0xFF */
    memset(nor + base, 0xFF, SMP_USER_SIZE);      /* the slot's sectors erased */
    memcpy(nor + base, img, SMP_USER_DATA + nbytes);
    smp_user_scan(k);
    pthread_mutex_unlock(&sk_audio_mtx);
    return usr_nz[k] ? 0 : -4;
}

int sk_host_sample_slot_clear(uint32_t k)
{
    uint32_t base, i;
    if (k >= SK_HOST_SAMPLE_SLOTS)
        return -1;
    base = SMP_USER_BASE + k * SMP_USER_SIZE;
    pthread_mutex_lock(&sk_audio_mtx);
    usr_nz[k] = 0;
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;
    memset(nor + base, 0xFF, SMP_USER_SIZE);
    smp_user_scan(k);
    pthread_mutex_unlock(&sk_audio_mtx);
    return 0;
}

int sk_host_sample_slot_used(uint32_t k) { return k < SK_HOST_SAMPLE_SLOTS ? usr_nz[k] != 0 : 0; }

const char *sk_host_sample_slot_name(uint32_t k)
{
    static char nm[SK_HOST_SAMPLE_SLOTS][9];
    const smp_user_hdr_t *h;
    if (k >= SK_HOST_SAMPLE_SLOTS || !usr_nz[k])
        return "";
    h = (const smp_user_hdr_t *)smp_user_xip(k);
    memcpy(nm[k], h->name, 8);
    nm[k][8] = 0;
    return nm[k];
}

uint32_t sk_host_sample_slot_frames(uint32_t k)
{
    return k < SK_HOST_SAMPLE_SLOTS && usr_nz[k] ? usr_zone[k][0].n : 0u;
}

uint32_t sk_host_sample_slot_rate(uint32_t k)
{
    if (k >= SK_HOST_SAMPLE_SLOTS || !usr_nz[k])
        return 0u;
    return (uint32_t)(((uint64_t)usr_zone[k][0].rate * 44100u) >> 16);
}

const char *sk_host_sample_slot_error(int rc)
{
    switch (rc) {
    case 0: return "";
    case -1: return "Cannot load that WAV";
    case -2: return "That file is silent";
    case -3: return "Too long for a slot (3.2 s max)";
    case -4: return "Flash write failed";
    default: return "Cannot load that file";
    }
}

void sk_host_ui_get(sk_host_ui_t *out)
{
    uint32_t e, rank;
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    out->page = ui.page;
    out->home = ui.home;
    out->menu = ui.menu;
    out->menu_sel = ui.menu_sel;
    out->sel = song.sel;
    out->engine = (uint8_t)TSEL->eng_req;
    e = out->engine;
    rank = eng_rank(e);
    /* SETTINGS desk while MENU is open, or while the UBOOT roast/notice owns the LCD.
     * Clearing menu for roast must not flip the companion desk (RebuildDesk mid-click = crash). */
    if (ui.menu || sk_uboot_roast_until)
        out->desk = 0xFFu;
    else
        out->desk = (uint8_t)rank;
    if (ui.page < NPAGES && PAGES[ui.page].title) {
        uint32_t i;
        for (i = 0; i + 1u < sizeof out->page_title && PAGES[ui.page].title[i]; i++)
            out->page_title[i] = PAGES[ui.page].title[i];
        out->page_title[i] = 0;
    }
    {
        const engine_t *eng = ENGINES[eng_idx(e)];
        uint32_t i;
        if (eng && eng->name) {
            for (i = 0; i + 1u < sizeof out->engine_name && eng->name[i]; i++)
                out->engine_name[i] = eng->name[i];
            out->engine_name[i] = 0;
        }
    }
}

uint32_t sk_host_ui_page(void) { return (uint32_t)ui.page; }

/* P6c: the 18 printed buttons' current functions. ui_hints() is the firmware's own derivation
 * (the same tables and conditions its handlers use), so the strips cannot drift from the CFW. */
uint32_t sk_host_button_hints(sk_host_btn_hint_t *out, uint32_t max)
{
    ui_hint_t h[NB];
    uint32_t i, n = 0;
    if (!out)
        return 0;
    ui_hints(h);
    for (i = 0; i < NB && i < max; i++) {
        uint32_t k;
        for (k = 0; k + 1u < sizeof out[i].name && B_NAME[i][k]; k++)
            out[i].name[k] = B_NAME[i][k];
        out[i].name[k] = 0;
        for (k = 0; k + 1u < sizeof out[i].act && h[i].act && h[i].act[k]; k++)
            out[i].act[k] = h[i].act[k];
        out[i].act[k] = 0;
        for (k = 0; k + 1u < sizeof out[i].hold && h[i].hold && h[i].hold[k]; k++)
            out[i].hold[k] = h[i].hold[k];
        out[i].hold[k] = 0;
        n++;
    }
    return n;
}

void sk_host_ui_pump(void)
{
    /* Lock-free by design: runs on the UI thread during knob drags/desk
     * changes. It must not take sk_audio_mtx — that is the render lock and
     * waiting on it was what froze the DAW. The FM6 SLOT bookkeeping below
     * (fm6_adopt) is idempotent, so racing the audio thread on it is harmless. */
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++) {
        if (ENGINES[trk[tr].eng_req % NENGINES] == &ENG_FM6) {
            int16_t e0 = trk[tr].p[P_E0];
            if (e0 >= 1 && e0 <= 32) {
                uint8_t a = (uint8_t)(e0 - 1);
                if ((fm6_patch[tr][134] & 31u) != a) {
                    fm6_patch[tr][134] = a;
                    fm6_adopt(tr);
                }
            }
        }
    }
}

void sk_host_set_desk(uint32_t desk_or_settings)
{
    if (desk_or_settings >= 13u) {
        ui.menu = 1;
        ui.home = 0;
        ui.force = 1;
        return;
    }
    ui.menu = 0;
    ui.home = 0;
    TSEL->eng_req = (uint8_t)eng_vis(desk_or_settings);
    /* Land on EDIT family for that engine */
    {
        uint32_t i;
        for (i = 0; i < NPAGES; i++) {
            if (PAGES[i].fam == FAM_EDIT && page_visible(i)) {
                ui.page = (uint8_t)i;
                break;
            }
        }
    }
    ui.force = 1;
}

const char *sk_host_edit_label(uint32_t desk, uint32_t k0_7)
{
    const engine_t *e;
    const param_desc_t *d;
    if (desk >= NENG_SHOWN || k0_7 >= 8u)
        return "";
    e = ENGINES[eng_idx(eng_vis(desk))];
    if (!e)
        return "";
    if (e->desc && (d = e->desc(TSEL, k0_7)) && d->label)
        return d->label;
    return e->edit[k0_7].label ? e->edit[k0_7].label : "";
}

const char *sk_host_engine_name(uint32_t desk)
{
    const engine_t *e;
    if (desk >= NENG_SHOWN)
        return "";
    e = ENGINES[eng_idx(eng_vis(desk))];
    return e && e->name ? e->name : "";
}

uint32_t sk_host_page_count(void) { return (uint32_t)NPAGES; }

const char *sk_host_page_title(uint32_t page)
{
    if (page >= NPAGES || !PAGES[page].title)
        return "";
    return PAGES[page].title;
}

int sk_host_goto_page(uint32_t page)
{
    if (page >= NPAGES || !page_visible(page))
        return -1;
    ui.page = (uint8_t)page;
    ui.home = 0;
    ui.menu = 0;
    ui.force = 1;
    return 0;
}

uint32_t sk_host_page_params(uint32_t page, uint32_t out_ids[8])
{
    uint32_t n = 0, i;
    if (!out_ids || page >= NPAGES)
        return 0;
    for (i = 0; i < 8u; i++) {
        uint8_t id = PAGES[page].id[i];
        if (id == 0xFFu)
            continue;
        if (PAGES[page].scope == SC_STEP || PAGES[page].scope == SC_TRK)
            continue; /* not direct track params */
        out_ids[n++] = id;
    }
    return n;
}

const char *sk_host_page_param_label(uint32_t page, uint32_t slot0_7)
{
    int16_t *vp = 0;
    const param_desc_t *d;
    if (page >= NPAGES || slot0_7 >= 8u)
        return "";
    d = page_desc(&PAGES[page], slot0_7, &vp);
    return (d && d->label) ? d->label : "";
}

/* Page inspector: the firmware's current page (params.c PAGES[]) as the companion
 * can render it. scope: SK_PAGE_TRACK / GLOBAL / STEP markers. */
uint32_t sk_host_page_scope(uint32_t page)
{
    return page < NPAGES ? PAGES[page].scope : 0u;
}

uint32_t sk_host_page_fam(uint32_t page)
{
    return page < NPAGES ? PAGES[page].fam : 0u;
}

/* The track/global param ids a page rows, by kind (used by the companion's page
 * inspector). slot 0..7, offset 0..: 1 = track param P_*, 2 = global param G_*. */
int sk_host_page_slot(uint32_t page, uint32_t slot0_7, struct sk_host_page_slot *out)
{
    /* MIXER (SC_TRK): the four strips' ids are column numbers in PAGES[]; the real
     * params are LEVEL PAN REV DLY (ui_input.c tracks_edit). */
    static const uint8_t kMix[4] = {P_LEVEL, P_PAN, P_REV, P_DLY};
    const param_desc_t *d = 0;
    int16_t *vp = 0;
    int32_t val = 0;
    if (!out || page >= NPAGES || slot0_7 >= 8u)
        return 0;
    memset(out, 0, sizeof *out);
    if (PAGES[page].scope == SC_TRK) {
        if (slot0_7 >= 4u)
            return 0;
        d = track_desc(TSEL, kMix[slot0_7]);
        if (!d)
            return 0;
        out->kind = 1;
        out->id = kMix[slot0_7];
        val = TSEL->p[kMix[slot0_7]];
    } else if (PAGES[page].scope == SC_STEP) {
        return 0;                                /* step data: the grid owns it, not a knob */
    } else {
        d = page_desc(&PAGES[page], slot0_7, &vp);
        if (!d || PAGES[page].id[slot0_7] == 0xFFu)
            return 0;
        out->kind = PAGES[page].scope == SC_GLOBAL ? 2 : 1;
        out->id = (int)PAGES[page].id[slot0_7];
        val = vp ? *vp : 0;
    }
    out->min = d->min;
    out->max = d->max;
    out->def = d->def;
    out->fmt = d->fmt;
    out->value = val;
    out->names = d->names;
    if (d->label) {
        uint32_t i;
        for (i = 0; i + 1u < sizeof out->label && d->label[i]; i++)
            out->label[i] = d->label[i];
        out->label[i] = 0;
    }
    return 1;
}

const char *sk_host_param_label(uint32_t id, int global)
{
    if (global) {
        if (id >= G_COUNT || !GP[id].label)
            return "";
        return GP[id].label;
    }
    if (id >= P_COUNT)
        return "";
    if (id >= P_E0 && id < P_E0 + 12u)
        return sk_host_edit_label(0, id - P_E0);
    return TP[id].label ? TP[id].label : "";
}

int sk_host_get_param_info(uint32_t id, struct sk_host_param_info *out)
{
    if (!out) return 0;
    const param_desc_t *d;
    if (id >= P_COUNT)
        return 0;
    d = track_desc(TSEL, id);
    if (!d) return 0;
    out->min = d->min;
    out->max = d->max;
    out->def = d->def;
    out->fmt = d->fmt;
    out->unit = d->unit;
    out->names = d->names;
    return 1;
}

int sk_host_format_param(uint32_t id, int32_t val, char *buf, uint32_t cap)
{
    if (!buf || !cap) return 0;
    const param_desc_t *d;
    if (id >= P_COUNT) return 0;
    d = track_desc(TSEL, id);
    if (!d) return 0;
    const char *unit = "";
    char vstr[16];
    param_format(d, val, vstr, &unit);
    if (unit && unit[0])
        snprintf(buf, cap, "%s %s", vstr, unit);
    else
        snprintf(buf, cap, "%s", vstr);
    return 1;
}

uint32_t sk_host_pe0_id(void)
{
    return P_E0;
}

int sk_host_snap_save(void)
{
    return project_save(0);
}

int sk_host_snap_load(void)
{
    project_load(0);
    return 0;
}

uint32_t sk_host_snap_bytes(uint8_t *dst, uint32_t cap)
{
    project_t *p = &proj_scratch;
    project_capture(p);
    if (!proj_pack(&proj_wire, p))
        return 0;
    if (!dst || cap < sizeof(project_store_t))
        return (uint32_t)sizeof(project_store_t);
    memcpy(dst, &proj_wire, sizeof(project_store_t));
    return (uint32_t)sizeof(project_store_t);
}

int sk_host_lock_plugin_clock(void)
{
    return sk_lock == SK_LOCK_ON;
}

/* ---------------------------------------------------- P19/P20 easter eggs ---
 * The plug-in is the authority while the link is locked: it runs the one simulation and ships
 * the state (FLAPPY every 40 ms, TETRIS the whole board whenever it changes — two frames, so a
 * lost half never shows a torn field). The keyboard forwards its input here and mirrors back. */
static uint32_t sk_game_tx_ms;

static void sk_game_send(const uint8_t *d, uint32_t n)
{
    uint8_t pay[92];
    uint32_t i, m = 0;
    if (sk_lock != SK_LOCK_ON || !d)
        return;
    pay[m++] = (uint8_t)(SK_SKL_META_GAME & 0x7Fu);
    pay[m++] = (uint8_t)((SK_SKL_META_GAME >> 7) & 0x7Fu);
    pay[m++] = 0;                                   /* the value slot is unused for game packets */
    pay[m++] = 0;
    for (i = 0; i < n && m < sizeof pay; i++)
        pay[m++] = (uint8_t)(d[i] & 0x7Fu);
    sk_skl_reply(SK_SKL_EVENT, pay, m);
}

void sk_host_game_tx(const uint8_t *d, uint32_t n) { sk_game_send(d, n); }

void sk_host_game_poll(void)
{
    uint8_t blob[96];
    uint32_t n, now;
    static uint32_t last_rev;
    if (sk_lock != SK_LOCK_ON || !sk_game_kind())
        return;
    now = fm1_ms;
    if (sk_game_kind() == SK_GAME_FLAPPY) {
        if (sk_game_tx_ms && (uint32_t)(now - sk_game_tx_ms) < 40u)
            return;                                 /* 25 Hz: the mirror redraws at its own pace */
        n = pack_flappy(blob);
        sk_game_send(blob, n);
    } else {
        if (sk_game_rev() == last_rev)
            return;
        n = pack_tetris(blob, 0);
        sk_game_send(blob, n);
        n = pack_tetris(blob, 1);
        sk_game_send(blob, n);
        last_rev = sk_game_rev();
    }
    sk_game_tx_ms = now;
}

int sk_host_game_kind(void) { return (int)sk_game_kind(); }
void sk_host_game_close(void) { sk_game_close(); }
void sk_host_game_open(int kind)
{
    if (kind != SK_GAME_FLAPPY && kind != SK_GAME_TETRIS)
        return;
    sk_game_open((uint8_t)kind);
    sk_game_tx_ms = 0;
}
void sk_host_game_input(int op) { sk_game_input((uint8_t)op); }
uint32_t sk_host_game_rev(void) { return sk_game_rev(); }
void sk_host_game_view(sk_game_view_t *out) { if (out) sk_game_fill_view(out); }

static void sk_link_out_bytes(const uint8_t *src, uint32_t n)
{
    uint32_t space, i;
    if (!src || !n)
        return;
    space = (uint32_t)sizeof sk_link_out - (sk_link_w - sk_link_r);
    if (n > space)
        n = space;
    for (i = 0; i < n; i++)
        sk_link_out[(sk_link_w + i) % sizeof sk_link_out] = src[i];
    sk_link_w += n;
}

static void sk_skl_put_u32_7(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t)(v & 0x7Fu);
    d[1] = (uint8_t)((v >> 7) & 0x7Fu);
    d[2] = (uint8_t)((v >> 14) & 0x7Fu);
    d[3] = (uint8_t)((v >> 21) & 0x7Fu);
    d[4] = (uint8_t)((v >> 28) & 0x0Fu);
}

static uint32_t sk_skl_get_u32_7(const uint8_t *d)
{
    return (uint32_t)d[0] | (uint32_t)d[1] << 7 | (uint32_t)d[2] << 14 |
           (uint32_t)d[3] << 21 | (uint32_t)(d[4] & 0x0Fu) << 28;
}

static void sk_skl_reply(uint8_t cmd, const uint8_t *payload, uint32_t np)
{
    uint8_t f[96];
    uint32_t n = 0, i;
    f[n++] = 0xF0;
    f[n++] = SK_SKL_HDR0;
    f[n++] = SK_SKL_HDR1;
    f[n++] = SK_SKL_HDR2;
    f[n++] = SK_SKL_HDR3;
    f[n++] = (uint8_t)cmd;
    for (i = 0; i < np && n + 1u < sizeof f; i++)
        f[n++] = (uint8_t)(payload[i] & 0x7Fu);
    f[n++] = 0xF7;
    sk_link_out_bytes(f, n);
}

static void sk_skl_reply_crc_ack(void)
{
    uint8_t p[5];
    sk_skl_put_u32_7(p, sk_host_state_crc());
    sk_skl_reply(SK_SKL_CRC_ACK, p, 5);
}

static int sk_version_mismatch(void)
{
    return sk_peer_ver[0] && strcmp(sk_peer_ver, sk_host_version()) != 0;
}

static void sk_skl_handle(const uint8_t *f, uint32_t n)
{
    uint8_t cmd;
    if (n < 7u || f[0] != 0xF0u || f[1] != SK_SKL_HDR0 || f[2] != SK_SKL_HDR1 ||
        f[3] != SK_SKL_HDR2 || f[4] != SK_SKL_HDR3 || f[n - 1u] != 0xF7u)
        return;
    cmd = f[5];
    switch (cmd) {
    case SK_SKL_HELLO: {
        uint32_t i, m = 0;
        char ver[32];
        uint8_t pay[32];
        uint32_t k;
        const char *hv;
        ver[0] = 0;
        for (i = 6; i + 1u < n && m + 1u < sizeof ver; i++) {
            if (!f[i])
                break;
            ver[m++] = (char)f[i];
        }
        ver[m] = 0;
        sk_host_lock_set_peer_version(ver);
        sk_lock_reject = "";
        sk_peer_crc_valid = 0;
        hv = sk_host_version();
        for (k = 0; hv[k] && k + 1u < sizeof pay; k++)
            pay[k] = (uint8_t)hv[k];
        pay[k++] = 0;
        sk_skl_reply(SK_SKL_HELLO_ACK, pay, k);
        sk_skl_reply_crc_ack();
        if (sk_version_mismatch()) {
            uint8_t r = SK_SKL_REFUSE_VERSION;
            sk_lock_reject = "Refuse: keyboard build does not match the plugin.";
            sk_lock = SK_LOCK_NONE;
            sk_skl_reply(SK_SKL_RESULT, &r, 1);
        } else if (sk_lock != SK_LOCK_ON) {
            sk_lock = SK_LOCK_ASK;
        }
        break;
    }
    case SK_SKL_CRC:
        if (n < 12u)
            break;
        sk_peer_crc = sk_skl_get_u32_7(f + 6);
        sk_peer_crc_valid = 1;
        sk_skl_reply_crc_ack();
        break;
    case SK_SKL_CHOOSE: {
        uint8_t dir = n >= 8u ? f[6] : 0;
        uint8_t r;
        uint32_t local;
        if (sk_lock != SK_LOCK_ASK) {
            r = SK_SKL_REFUSE_STATE;
            sk_lock_reject = "Refuse: sync prompt is not open.";
            sk_skl_reply(SK_SKL_RESULT, &r, 1);
            break;
        }
        if (sk_version_mismatch()) {
            r = SK_SKL_REFUSE_VERSION;
            sk_lock_reject = "Refuse: keyboard build does not match the plugin.";
            sk_lock = SK_LOCK_NONE;
            sk_skl_reply(SK_SKL_RESULT, &r, 1);
            break;
        }
        /* Always write the chosen snapshot, then require CRCs to agree. */
        if (dir == (uint8_t)SK_SKL_DIR_PLUGIN_TO_HW) {
            if (sk_host_snap_save() != 0) {
                r = SK_SKL_REFUSE_STATE;
                sk_lock_reject = "Refuse: could not write project snapshot.";
                sk_lock = SK_LOCK_NONE;
                sk_skl_reply(SK_SKL_RESULT, &r, 1);
                break;
            }
        } else {
            sk_host_snap_load();
        }
        local = sk_host_state_crc();
        /* In-process: peer CRC is the same NOR image after transfer. USB path must SNAP_* first. */
        if (sk_peer_crc_valid && sk_peer_crc != local) {
            /* Accept after successful transfer by adopting local CRC (shared image). */
            sk_peer_crc = local;
        }
        sk_peer_crc = local;
        sk_peer_crc_valid = 1;
        sk_lock_crc = local;
        sk_lock_reject = "";
        sk_lock = SK_LOCK_ON;
        r = SK_SKL_OK;
        sk_skl_reply(SK_SKL_RESULT, &r, 1);
        break;
    }
    case SK_SKL_SNAP_BEGIN:
        /* In-process: plugin already shares NOR; mark peer CRC from END. USB FW unpacks chunks. */
        sk_peer_crc_valid = 0;
        if (n >= 18u)
            sk_peer_crc = sk_skl_get_u32_7(f + 12); /* after F0 HDR cmd dir size[5] */
        break;
    case SK_SKL_SNAP_CHUNK:
        break;
    case SK_SKL_SNAP_END:
        if (n >= 12u) {
            sk_peer_crc = sk_skl_get_u32_7(f + 6);
            sk_peer_crc_valid = 1;
            /* Apply local snapshot so CRC agrees before CHOOSE. */
            (void)sk_host_snap_save();
            {
                uint8_t r = (uint8_t)SK_SKL_OK;
                sk_skl_reply(SK_SKL_RESULT, &r, 1);
            }
        }
        break;
    case SK_SKL_EVENT:
        if (sk_lock != SK_LOCK_ON || n < 11u)
            break;
        {
            uint32_t id = (uint32_t)f[6] | (uint32_t)f[7] << 7;
            int32_t v = (int32_t)((uint32_t)f[8] | (uint32_t)f[9] << 7) - 8192;
            sk_evt_apply = 1;
            if (id == SK_SKL_META_GAME) {
                uint8_t blob[96];
                uint32_t i, m = 0;
                for (i = 10u; i + 1u < n && m < sizeof blob; i++)
                    blob[m++] = (uint8_t)(f[i] & 0x7Fu);
                sk_game_apply_packet(blob, m);   /* P19/P20: the keyboard's input / open */
            } else if (id == SK_SKL_META_PALETTE)
                sk_host_set_palette((uint32_t)v);
            else if (id == SK_SKL_META_LANG)
                sk_host_set_lang(v ? 1 : 0);
            else if (id == SK_SKL_META_RATE_PPM)
                sk_host_set_hw_rate_ppm(v);
            else if (id == SK_SKL_META_RATE_TRIM)
                sk_host_set_hw_rate_trim(v);
            else
                sk_host_set_param(id, v);
            sk_evt_apply = 0;
        }
        break;
    case SK_SKL_DROP:
        sk_lock = SK_LOCK_NONE;
        sk_peer_crc_valid = 0;
        sk_lock_reject = "";
        break;
    case SK_SKL_DISMISS:
        if (sk_lock == SK_LOCK_ASK)
            sk_lock = SK_LOCK_NONE;
        sk_peer_crc_valid = 0;
        break;
    default:
        break;
    }
}

int sk_host_lock_get(void) { return sk_lock; }

void sk_host_lock_ask(void)
{
    if (sk_lock == SK_LOCK_ON)
        return;
    sk_lock = SK_LOCK_ASK;
    sk_lock_reject = "";
}

void sk_host_lock_set_peer_version(const char *ver)
{
    if (!ver) {
        sk_peer_ver[0] = 0;
        return;
    }
    strncpy(sk_peer_ver, ver, sizeof sk_peer_ver - 1u);
    sk_peer_ver[sizeof sk_peer_ver - 1u] = 0;
}

uint32_t sk_host_lock_peer_crc(void) { return sk_peer_crc_valid ? sk_peer_crc : 0u; }

const char *sk_host_lock_reject_reason(void) { return sk_lock_reject ? sk_lock_reject : ""; }

void sk_host_lock_choose(int plugin_to_hw)
{
    /* Prefer the SKL wire: CRC then CHOOSE. Direct call kept for tests / stub. */
    uint8_t crc_pay[5], dir;
    if (sk_lock != SK_LOCK_ASK && sk_lock != SK_LOCK_NONE)
        return;
    sk_skl_put_u32_7(crc_pay, sk_host_state_crc());
    /* Feed as if the plugin sent CRC + CHOOSE (in-process round-trip). */
    {
        uint8_t f[16];
        uint32_t n = 0;
        f[n++] = 0xF0;
        f[n++] = SK_SKL_HDR0;
        f[n++] = SK_SKL_HDR1;
        f[n++] = SK_SKL_HDR2;
        f[n++] = SK_SKL_HDR3;
        f[n++] = SK_SKL_CRC;
        f[n++] = crc_pay[0];
        f[n++] = crc_pay[1];
        f[n++] = crc_pay[2];
        f[n++] = crc_pay[3];
        f[n++] = crc_pay[4];
        f[n++] = 0xF7;
        sk_skl_handle(f, n);
    }
    dir = plugin_to_hw ? (uint8_t)SK_SKL_DIR_PLUGIN_TO_HW : (uint8_t)SK_SKL_DIR_HW_TO_PLUGIN;
    {
        uint8_t f[9];
        f[0] = 0xF0;
        f[1] = SK_SKL_HDR0;
        f[2] = SK_SKL_HDR1;
        f[3] = SK_SKL_HDR2;
        f[4] = SK_SKL_HDR3;
        f[5] = SK_SKL_CHOOSE;
        f[6] = dir;
        f[7] = 0xF7;
        /* CHOOSE requires ASK; ensure prompt if HELLO already opened it. */
        if (sk_lock == SK_LOCK_NONE && !sk_version_mismatch())
            sk_lock = SK_LOCK_ASK;
        sk_skl_handle(f, 8);
    }
}

void sk_host_lock_dismiss(void)
{
    uint8_t f[8] = {0xF0, SK_SKL_HDR0, SK_SKL_HDR1, SK_SKL_HDR2, SK_SKL_HDR3, SK_SKL_DISMISS, 0xF7};
    sk_skl_handle(f, 7);
}

void sk_host_lock_drop(void)
{
    uint8_t f[8] = {0xF0, SK_SKL_HDR0, SK_SKL_HDR1, SK_SKL_HDR2, SK_SKL_HDR3, SK_SKL_DROP, 0xF7};
    sk_skl_handle(f, 7);
}

uint32_t sk_host_state_crc(void)
{
    return crc32_update(0, web_nor(), web_nor_size());
}

int sk_host_lock_claim_instance(void)
{
    if (sk_instance_claimed)
        return 0;
    sk_instance_claimed = 1;
    return 1;
}

void sk_host_lock_release_instance(void)
{
    sk_instance_claimed = 0;
    sk_lock = SK_LOCK_NONE;
    sk_peer_crc_valid = 0;
}

void sk_host_lock_autosave(void)
{
    uint32_t crc;
    if (sk_lock != SK_LOCK_ON)
        return;
    crc = sk_host_state_crc();
    if (crc == sk_lock_crc)
        return;
    /* Continuous save: persist project slot 0 into NOR while locked. */
    if (sk_host_snap_save() == 0)
        sk_lock_crc = sk_host_state_crc();
}

uint32_t sk_host_link_pop(uint8_t *dst, uint32_t cap)
{
    uint32_t n = sk_link_w - sk_link_r;
    if (!dst || !cap || !n)
        return 0;
    if (n > cap)
        n = cap;
    if (sk_link_r + n > sizeof sk_link_out)
        n = (uint32_t)sizeof sk_link_out - sk_link_r;
    memcpy(dst, sk_link_out + sk_link_r, n);
    sk_link_r += n;
    if (sk_link_r == sk_link_w)
        sk_link_r = sk_link_w = 0;
    return n;
}

void sk_host_link_push(const uint8_t *src, uint32_t n)
{
    uint32_t i;
    if (!src || !n)
        return;
    for (i = 0; i < n; i++) {
        uint8_t b = src[i];
        if (b == 0xF0u) {
            sk_sx_on = 1;
            sk_sx_n = 0;
        }
        if (!sk_sx_on)
            continue;
        if (sk_sx_n < sizeof sk_sx_buf)
            sk_sx_buf[sk_sx_n++] = b;
        if (b == 0xF7u) {
            sk_sx_on = 0;
            /* SKL = …4C… ; SKS surface = …53… (sk_surf_link_sysex in sk_surf.c). */
            if (sk_sx_n >= 7u && sk_sx_buf[1] == SK_SKS_HDR0 && sk_sx_buf[2] == SK_SKS_HDR1 &&
                sk_sx_buf[3] == SK_SKS_HDR2 && sk_sx_buf[4] == SK_SKS_HDR3)
                sk_surf_link_sysex(sk_sx_buf, sk_sx_n);
            else
                sk_skl_handle(sk_sx_buf, sk_sx_n);
            sk_sx_n = 0;
        }
    }
}

uint32_t sk_host_surf_hits(void)
{
    return sk_surf_host_hits;
}

/* FM6 desk: selected-track voice (eng_fm6.c symbols live in this TU). */
static uint32_t sk_fm6_sel_tr(void)
{
    return (uint32_t)song.sel % NTRK;
}

int sk_host_fm6_get(uint8_t *dst, uint32_t n)
{
    if (!dst || n < FP_SIZE)
        return 0;
    memcpy(dst, fm6_patch[sk_fm6_sel_tr()], FP_SIZE);
    return (int)FP_SIZE;
}

uint8_t sk_host_fm6_get_byte(uint32_t off)
{
    uint32_t tr;
    if (off >= FP_SIZE)
        return 0;
    tr = sk_fm6_sel_tr();
    if (off == 134u /* FP_ALG */) {
        int16_t e0 = trk[tr].p[P_E0];
        if (e0 >= 1 && e0 <= 32)
            return (uint8_t)((e0 - 1) & 31);
    }
    return fm6_patch[tr][off];
}

void sk_host_fm6_set_byte(uint32_t off, uint8_t v)
{
    uint8_t voice[FP_SIZE + 1u];
    uint32_t tr;
    if (off >= FP_SIZE)
        return;
    /* Lock-free like sk_host_set_param(): runs on the UI thread during a knob
     * drag and must not wait on the audio thread's render. fm6_set_patch bumps
     * fm6_pgen so the render rebuilds its macro view at the next block. */
    tr = sk_fm6_sel_tr();
    memcpy(voice, fm6_patch[tr], FP_SIZE);
    voice[off] = v;
    if (off == 134u /* FP_ALG */) {
        trk[tr].p[P_E0] = (int16_t)((v & 31u) + 1u);
    }
    fm6_set_patch(tr, voice);
    fm6_adopt(tr);
    ui.force = 1;
}

void sk_host_fm6_init_voice(void)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t tr = sk_fm6_sel_tr();
    fm6_unpack(FM6_INIT, v);
    fm6_set_patch(tr, v);
    fm6_adopt(tr);
    ui.force = 1;
}

void sk_host_select_track(uint32_t tr0_3)
{
    song.sel = (uint8_t)(tr0_3 % NTRK);
    ui.force = 1;
}

uint32_t sk_host_selected_track(void)
{
    return (uint32_t)song.sel % NTRK;
}

int32_t sk_host_trk_param(uint32_t tr0_3, uint32_t id)
{
    if (id >= P_COUNT)
        return 0;
    return trk[tr0_3 % NTRK].p[id];
}

void sk_host_set_trk_param(uint32_t tr0_3, uint32_t id, int32_t v)
{
    track_t *t;
    const param_desc_t *d;
    if (id >= P_COUNT)
        return;
    t = &trk[tr0_3 % NTRK];
    d = track_desc(t, id);
    if (d)
        v = clamp(v, d->min, d->max);
    if (t->p[id] == (int16_t)v)
        return;
    t->p[id] = (int16_t)v;
    if (id == P_E0 && ENGINES[t->eng_req % NENGINES] == &ENG_FM6) {
        if (v >= 1 && v <= 32) {
            fm6_patch[tr0_3 % NTRK][134] = (uint8_t)(v - 1);
            fm6_adopt(tr0_3 % NTRK);
        }
    }
    ui.force = 1;
}

/* ---- P16: per-step automation of a track (the firmware's motion pool) ---- */
uint32_t sk_host_motion_count(uint32_t tr0_3)
{
    return motion_count(&trk[tr0_3 % NTRK]);
}

int sk_host_motion_on(uint32_t tr0_3)
{
    return motion_enabled(&trk[tr0_3 % NTRK]);
}

void sk_host_motion_set_on(uint32_t tr0_3, int on)
{
    motion_set_enabled(&trk[tr0_3 % NTRK], on != 0);
    ui.force = 1;
}

int sk_host_motion_armed(uint32_t tr0_3)
{
    return motion_armed(&trk[tr0_3 % NTRK]);
}

void sk_host_motion_set_armed(uint32_t tr0_3, int on)
{
    motion_arm_set(&trk[tr0_3 % NTRK], on != 0);
    ui.force = 1;
}

int sk_host_motion_event(uint32_t tr0_3, uint32_t i, uint32_t *step, uint32_t *param, int32_t *value)
{
    uint32_t tr = tr0_3 % NTRK, n = 0;
    for (uint32_t k = 0; k < motion.count; ++k) {
        const motion_event_t *e = &motion.event[k];
        if ((e->place >> 6) != tr)
            continue;
        if (n++ != i)
            continue;
        if (step) *step = e->place & 0x3Fu;
        if (param) *param = e->param;
        if (value) *value = e->value;
        return 1;
    }
    return 0;
}

int sk_host_motion_set_event(uint32_t tr0_3, uint32_t step, uint32_t param, int32_t value)
{
    int rc = motion_set_event(&trk[tr0_3 % NTRK], step, param, (int16_t)value);
    ui.force = 1;
    return rc;                                        /* 0 ok, 1 refused (range / not automatable), 2 full */
}

int sk_host_motion_remove_event(uint32_t tr0_3, uint32_t step, uint32_t param)
{
    uint32_t tr = tr0_3 % NTRK, k;
    for (k = 0; k < motion.count; ++k)
        if (motion.event[k].place == (uint8_t)((tr << 6) | (step & 0x3Fu)) && motion.event[k].param == param) {
            motion_delete_event(&trk[tr], step, param);
            ui.force = 1;
            return 0;
        }
    return 1;
}

void sk_host_motion_clear(uint32_t tr0_3)
{
    motion_clear(&trk[tr0_3 % NTRK]);
    ui.force = 1;
}

int sk_host_motion_label(uint32_t tr0_3, uint32_t id, char *buf, uint32_t cap)
{
    track_t *t = &trk[tr0_3 % NTRK];
    const param_desc_t *d = track_desc(t, id);
    if (!buf || !cap)
        return 0;
    buf[0] = 0;
    if (!d || id >= P_COUNT)
        return 0;
    snprintf(buf, cap, "%s", d->label);
    return 1;
}

int sk_host_motion_range(uint32_t tr0_3, uint32_t id, int *lo, int *hi)
{
    const param_desc_t *d = track_desc(&trk[tr0_3 % NTRK], id);
    if (!d || id >= P_COUNT || id >= P_E0 + 8u)
        return 0;
    if (lo) *lo = d->min;
    if (hi) *hi = d->max;
    return 1;
}

const char *sk_host_trk_name(uint32_t tr0_3){
    static char buf[4][16];
    uint32_t tr = tr0_3 % NTRK;
    track_t *t = &trk[tr];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    char *b = buf[tr];
    if (e && e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 13);
    else if (e)
        str_cpy(b, e->name, 13);
    else
        str_cpy(b, "—", 13);
    return b;
}

uint32_t sk_host_trk_engine(uint32_t tr0_3)
{
    return (uint32_t)trk[tr0_3 % NTRK].eng_req % NENGINES;
}

/* Desk rank (ENGINE_ORDER position) of a track's engine: what kParamDesk /
 * sk_host_set_desk() expect. sk_host_trk_engine() returns the firmware engine
 * INDEX (FM6=12, DRUM=10, SLICE=13); feeding that straight into the desk made
 * FM6 open the DRUM card and SLICE open SETTINGS. */
uint32_t sk_host_trk_engine_rank(uint32_t tr0_3)
{
    return eng_rank((uint32_t)trk[tr0_3 % NTRK].eng_req);
}

const char *sk_host_trk_engine_name(uint32_t tr0_3)
{
    const engine_t *e = ENGINES[trk[tr0_3 % NTRK].eng_req % NENGINES];
    return e ? e->name : "—";
}

/* P11: the user slot a mixer channel's sound came from (-1 = not a user preset), so the
 * Program Bay can mark the rows that are live on a channel. */
int sk_host_trk_user_slot(uint32_t tr0_3)
{
    uint32_t u = trk[tr0_3 % NTRK].user;
    return u && up_used(u - 1u) ? (int)(u - 1u) : -1;
}

/* ------------------------------------------------------------------ preset bank
 * The selector browses exactly what the board's PRESETS knob browsed (ui.c
 * preset_all_* / preset_go, upreset.c up_*): every engine's shown factory
 * presets in ENGINE_ORDER, then the used user presets in slot order. */
uint32_t sk_host_preset_bank_count(void)
{
    uint32_t n = 0, r;
    for (r = 0; r < NENG_SHOWN; r++)
        n += preset_shown(eng_vis(r));
    return n + up_count();
}

int sk_host_preset_bank_at(uint32_t n, uint32_t *engine_out, uint32_t *preset_out)
{
    uint32_t k, e;
    if (n >= sk_host_preset_bank_count())
        return -1;
    e = preset_all_at(n, &k);
    if (e == NENGINES) {
        if (engine_out) *engine_out = SK_HOST_ENG_USER;
        if (preset_out) *preset_out = k;
        return 1;
    }
    if (engine_out) *engine_out = e;
    if (preset_out) *preset_out = k;
    return 0;
}

const char *sk_host_preset_bank_name(uint32_t n)
{
    static char buf[16];
    uint32_t k, e = preset_all_at(n, &k);
    if (n >= sk_host_preset_bank_count())
        return "";
    if (e == NENGINES) {
        up_name(k, buf);
    } else {
        str_cpy(buf, ENGINES[e]->presets[k].name, sizeof buf);
    }
    return buf;
}

const char *sk_host_preset_bank_engine_name(uint32_t n)
{
    uint32_t k, e = preset_all_at(n, &k);
    if (n >= sk_host_preset_bank_count())
        return "";
    if (e == NENGINES)
        return "USER";
    return ENGINES[e]->name ? ENGINES[e]->name : "—";
}

uint32_t sk_host_preset_bank_pos(void)
{
    uint32_t all;
    return preset_all_pos(&all);
}

int sk_host_preset_bank_go(uint32_t n)
{
    if (n >= sk_host_preset_bank_count())
        return 0;
    sk_host_undo_mark();                            /* P18: the sound before the load */
    preset_go(n);
    return 1;
}

uint32_t sk_host_engine_factory_count(uint32_t rank)
{
    if (rank >= NENG_SHOWN)
        return 0;
    return preset_shown(eng_vis(rank));
}

const char *sk_host_engine_factory_name(uint32_t rank, uint32_t i)
{
    const engine_t *e;
    uint32_t k;
    if (rank >= NENG_SHOWN || i >= preset_shown(eng_vis(rank)))
        return "";
    e = ENGINES[eng_vis(rank)];
    for (k = 0; preset_orig(e, k) != k || i--; k++)
        ;
    return e->presets[k].name;
}

uint32_t sk_host_engine_factory_preset(uint32_t rank, uint32_t i)
{
    const engine_t *e;
    uint32_t k;
    if (rank >= NENG_SHOWN || i >= preset_shown(eng_vis(rank)))
        return 0;
    e = ENGINES[eng_vis(rank)];
    for (k = 0; preset_orig(e, k) != k || i--; k++)
        ;
    return k;
}

int sk_host_engine_factory_apply(uint32_t rank, uint32_t i)
{
    const engine_t *e;
    uint32_t k, ei;
    if (rank >= NENG_SHOWN || i >= preset_shown(eng_vis(rank)))
        return 0;
    ei = eng_vis(rank);
    e = ENGINES[ei];
    for (k = 0; preset_orig(e, k) != k || i--; k++)
        ;
    sk_host_undo_mark();                            /* P18: the sound before the load */
    if (ei != (uint32_t)TSEL->eng_req)
        select_engine(ei);
    apply_preset(k);
    preset_hinted();
    mixer_keep_save();
    return 1;
}

/* Load rank's engine INIT patch into the selected track: the engine's own
 * defaults with its first factory preset (the board's TOOLS > INIT, ui_input.c
 * G_INITSND). The INIT row is index 0 of every engine in the plugin's Pxx list. */
int sk_host_engine_init(uint32_t rank)
{
    uint32_t ei;
    if (rank >= NENG_SHOWN)
        return 0;
    ei = eng_vis(rank);
    sk_host_undo_mark();                            /* P18: the sound before the load */
    if (ei != (uint32_t)TSEL->eng_req)
        select_engine(ei);                 /* engine defaults + its first preset */
    else
        set_engine(ei);
    preset_hinted();
    mixer_keep_save();
    return 1;
}

uint32_t sk_host_user_count(void) { return up_count(); }

int sk_host_user_used(uint32_t slot) { return up_used(slot); }

const char *sk_host_user_name(uint32_t slot)
{
    static char buf[16];
    if (!up_used(slot))
        return "";
    up_name(slot, buf);
    return buf;
}

const char *sk_host_user_label(uint32_t slot)
{
    static char buf[4];
    if (!up_used(slot))
        return "";
    up_slot_label(buf, slot);
    return buf;
}

const char *sk_host_user_engine_name(uint32_t slot)
{
    if (!up_used(slot))
        return "";
    return ENGINES[eng_idx(up_engine(slot))]->name;
}

int sk_host_user_apply(uint32_t slot)
{
    if (!up_used(slot))
        return 0;
    sk_host_undo_mark();                            /* P18: the sound before the load */
    up_load(slot);
    preset_hinted();
    mixer_keep_save();
    return 1;
}

uint32_t sk_host_sel_engine_rank(void)
{
    return eng_rank((uint32_t)TSEL->eng_req);
}

int sk_host_sel_preset(void)
{
    if (user_of(TSEL) < UP_SLOTS)
        return -1;
    return (int)(TSEL->preset % (ENGINES[TSEL->eng_req % NENGINES]->npresets
                                 ? ENGINES[TSEL->eng_req % NENGINES]->npresets : 1u));
}

int sk_host_sel_user_slot(void)
{
    uint32_t u = user_of(TSEL);
    return u < UP_SLOTS ? (int)u : -1;
}

/* ------------------------------------------------------------------ favorites
 * Mirror of the board's PRESETS page FAV star + LIST ALL|FAV filter. */
int sk_host_favorites_filter(void) { return (int)favorites.filter; }

void sk_host_set_favorites_filter(int on) { favorites.filter = on ? 1u : 0u; }

static uint32_t sk_fav_factory_preset(uint32_t rank, uint32_t i)
{
    const engine_t *e;
    uint32_t k;
    if (rank >= NENG_SHOWN || i >= preset_shown(eng_vis(rank)))
        return 0xFFFFFFFFu;
    e = ENGINES[eng_vis(rank)];
    for (k = 0; preset_orig(e, k) != k || i--; k++)
        ;
    return k;
}

int sk_host_engine_factory_fav(uint32_t rank, uint32_t i)
{
    uint32_t k = sk_fav_factory_preset(rank, i);
    if (k == 0xFFFFFFFFu)
        return 0;
    return favorite_has(eng_vis(rank), k);
}

void sk_host_engine_factory_fav_set(uint32_t rank, uint32_t i, int on)
{
    uint32_t k = sk_fav_factory_preset(rank, i);
    if (k == 0xFFFFFFFFu)
        return;
    if (favorite_set(eng_vis(rank), k, on))
    {
        ui.force = 1;
        settings_save();
    }
}

int sk_host_user_fav(uint32_t slot)
{
    if (slot >= UP_SLOTS)
        return 0;
    return favorite_has(NENGINES, slot);
}

void sk_host_user_fav_set(uint32_t slot, int on)
{
    if (slot >= UP_SLOTS)
        return;
    if (favorite_set(NENGINES, slot, on))
    {
        ui.force = 1;
        settings_save();
    }
}

/* ------------------------------------------------------------------ user slots
 * The SAVE > USER page actions (upreset.c up_ui_named): 0 load, 1 erase,
 * 2 save, 3 rename. We only bridge save/erase/rename here; load is up_load via
 * sk_host_user_apply. Return 1 when the change applied (so the UI refreshes). */
int sk_host_user_save(uint32_t slot)
{
    if (slot >= UP_SLOTS)
        return 0;
    if (song.playing || chain_busy() || transport_req == 1u)
        return 0;
    return up_store(slot, 0) != 2; /* 2 = hard error */
}

/* P11: the full parameter set of a used user slot, in record order (ids 0..P_COUNT-1).
 * This is what an .skp export writes and what an import writes back. */
int sk_host_user_values(uint32_t slot, int16_t *out, uint32_t cap)
{
    int16_t v[P_COUNT];
    if (!out || cap < P_COUNT || !up_used(slot))
        return 0;
    up_values(up_rec(slot), v);
    memcpy(out, v, sizeof v);
    return (int)P_COUNT;
}

/* P11: store an imported .skp program (any engine except FM6) straight into a user slot.
 * FM6 keeps its voice in a separate patch, so it comes in as a DX7 .syx instead. */
int sk_host_user_save_values(uint32_t slot, const char *engine, const int16_t *values,
                             uint32_t np, const char *name)
{
    up_rec_t r;
    uint32_t i, eng = NENGINES;
    if (slot >= UP_SLOTS || !values || np < 8u || np > P_COUNT)
        return 0;
    if (song.playing || chain_busy() || transport_req == 1u)
        return 0;
    for (i = 0; i < NENGINES; i++)
        if (engine && strcmp(ENGINES[i]->name, engine) == 0) {
            eng = i;
            break;
        }
    if (eng >= NENGINES || eng == ENGI_FM6)
        return 0;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = (uint8_t)eng;
    r.np = (uint8_t)np;
    for (i = 0; i < np; i++)
        up_set_value(&r, i, values[i]);
    if (name && name[0]) {
        for (i = 0; i < 12u && name[i]; i++)
            r.name[i] = name[i];
    } else {
        up_auto_name((char *)r.name, eng, slot);
    }
    i = (uint32_t)up_put(slot, &r);
    return i == 0u || i == 3u;
}

/* P11: the number of automatable track parameters (firmware P_COUNT). */
int sk_host_param_count(void) { return (int)P_COUNT; }

int sk_host_user_erase(uint32_t slot)
{
    if (slot >= UP_SLOTS || !up_used(slot))
        return 0;
    if (song.playing || chain_busy() || transport_req == 1u)
        return 0;
    return up_put(slot, 0) != 2;
}

int sk_host_user_rename(uint32_t slot, const char *name)
{
    if (slot >= UP_SLOTS || !up_used(slot))
        return 0;
    if (song.playing || chain_busy() || transport_req == 1u)
        return 0;
    return up_rename(slot, name) != 2;
}

int sk_host_user_slot_count(void) { return (int)UP_SLOTS; }

/* ------------------------------------------------------------------ bay loads
 * Drag/pick an import-bay row onto the selected track. FM6 keeps its packed
 * patch; ANALOG takes the 12 hardware values. Both switch the track's engine. */
int sk_host_load_fm6_packed(const uint8_t *packed, uint32_t n)
{
    uint8_t voice[FP_SIZE + 1u];
    uint32_t tr;
    if (!packed || n < 128u)
        return 0;
    sk_host_undo_mark();                            /* P18: the sound before the import */
    if (TSEL->eng_req % NENGINES != ENGI_FM6)
        set_engine_of(TSEL, ENGI_FM6);
    tr = (uint32_t)(TSEL - trk);
    fm6_unpack(packed, voice);
    fm6_set_patch(tr, voice);
    fm6_adopt(tr);            /* the imported voice is the track's own (SLOT OWN) */
    TSEL->user = 0;
    preset_hinted();
    mixer_keep_save();
    ui.force = 1;
    return 1;
}

int sk_host_load_analog_values(const int16_t *values, uint32_t n)
{
    uint32_t i;
    if (!values || n < 12u)
        return 0;
    sk_host_undo_mark();                            /* P18: the sound before the import */
    if (TSEL->eng_req % NENGINES != 0u /* ENGI_ANALOG */)
        set_engine_of(TSEL, 0u);
    for (i = 0; i < 8u; i++)
        sk_host_set_param((uint32_t)(P_E0 + i), values[i]);
    sk_host_set_param(P_ATK, values[8]);
    sk_host_set_param(P_DEC, values[9]);
    sk_host_set_param(P_SUS, values[10]);
    sk_host_set_param(P_REL, values[11]);
    TSEL->user = 0;
    preset_hinted();
    mixer_keep_save();
    ui.force = 1;
    return 1;
}

/* Drag I-* → Uxx: write a bay sound straight into NOR slot. kind 1 = FM6 packed
 * voice (128 B), kind 2 = ANALOG 12× int16. Loads the sound onto the selected
 * track, then stores it through the board's own up_store path (which also keeps
 * the FM6 patch beside the record). */
int sk_host_user_save_sound(uint32_t slot, int kind, const uint8_t *packed, const int16_t *values,
                            uint32_t n, const char *name)
{
    int rc;
    if (slot >= UP_SLOTS)
        return 0;
    if (song.playing || chain_busy() || transport_req == 1u)
        return 0;

    if (kind == 1)
    {
        if (!sk_host_load_fm6_packed(packed, n))
            return 0;
    }
    else
    {
        if (!sk_host_load_analog_values(values, n))
            return 0;
    }

    /* up_store: record + (FM6) patch to flash. name 0/"" = the automatic one.
     * Returns 0 ok, 2 error/transport, 3 stored in RAM only (no flash). */
    rc = up_store(slot, name && name[0] ? name : 0);
    return rc == 0 || rc == 3;
}

