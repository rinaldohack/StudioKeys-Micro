/* SPDX-License-Identifier: GPL-3.0-only
 * Native host: same method as web/emu/felucca_web.c / native_check.c. */
#include <string.h>
#include <stdlib.h>

#ifndef NVOICE
/* Host array ceiling. Unleashed opens this fully; Limited soft-caps to 8.
 * Not a product “voice limit” — plan: Unleashed spends what the callback can hold. */
#define NVOICE 128
#endif

#include "sk_host.h"
#include "sk_skl.h"

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

/* Match the device package string (felucca_web defaults to "WEB"). */
#ifdef FELUCCA_VERSION
#undef FELUCCA_VERSION
#endif
#define FELUCCA_VERSION "v1.0.0"

/* Soft-cap must be visible before voice.c is pulled in via felucca_web.c. */
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

/* Bring up the full firmware host (static TU). Paths relative to this file. */
#include "../web/emu/felucca_web.c"

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
}

void sk_host_boot(void)
{
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

void sk_host_set_param(uint32_t id, int32_t v)
{
    const param_desc_t *d;
    if (id >= P_COUNT)
        return;
    d = track_desc(TSEL, id);
    if (d)
        v = clamp(v, d->min, d->max);
    if (TSEL->p[id] == (int16_t)v)
        return;
    TSEL->p[id] = (int16_t)v;
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

void sk_host_set_palette(uint32_t idx)
{
    idx %= NPALETTES;
    if (settings.palette == idx)
        return;
    settings.palette = idx;
    palette_set(idx);
    ui.force = 1;
}

uint32_t sk_host_palette(void) { return settings.palette % NPALETTES; }
uint32_t sk_host_npalettes(void) { return NPALETTES; }

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
    return FELUCCA_VERSION;
}

uint32_t sk_host_sample_rate(void) { return web_sample_rate(); }
uint32_t sk_host_now_ms(void) { return web_now_ms(); }
uint32_t sk_host_playing(void) { return web_playing(); }

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
        if (!sk_peer_crc_valid || sk_peer_crc != sk_host_state_crc()) {
            r = SK_SKL_REFUSE_CRC;
            sk_lock_reject = "Refuse: snapshot CRC does not match.";
            sk_lock = SK_LOCK_NONE;
            sk_skl_reply(SK_SKL_RESULT, &r, 1);
            break;
        }
        (void)dir; /* direction reserved for snapshot transfer; in-process NOR is shared */
        sk_lock_crc = sk_host_state_crc();
        sk_lock_reject = "";
        sk_lock = SK_LOCK_ON;
        r = SK_SKL_OK;
        sk_skl_reply(SK_SKL_RESULT, &r, 1);
        break;
    }
    case SK_SKL_EVENT:
        if (sk_lock != SK_LOCK_ON || n < 11u)
            break;
        {
            uint32_t id = (uint32_t)f[6] | (uint32_t)f[7] << 7;
            int32_t v = (int32_t)((uint32_t)f[8] | (uint32_t)f[9] << 7) - 8192;
            sk_evt_apply = 1;
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
    if (sk_lock != SK_LOCK_ON)
        return;
    /* Touch NOR so a continuous-save indicator and DAW chunk see activity.
     * Real USB path will push the project blob; here the image is already shared. */
    if (sk_host_state_crc() != sk_lock_crc)
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
            sk_skl_handle(sk_sx_buf, sk_sx_n);
            sk_sx_n = 0;
        }
    }
}
