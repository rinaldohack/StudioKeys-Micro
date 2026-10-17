/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * Cable-3 SKL lock SysEx: F0 7D 53 4B 4C … F7
 * HELLO/CRC/CHOOSE/EVENT + project SNAP_* chunks. See host/README.md / sk_skl.h. */
#if SKM_OTA

enum {
    SKL_H0 = 0x7D,
    SKL_H1 = 0x53,
    SKL_H2 = 0x4B,
    SKL_H3 = 0x4C,
    SKL_HELLO = 0x01,
    SKL_HELLO_ACK = 0x02,
    SKL_CRC = 0x03,
    SKL_CRC_ACK = 0x04,
    SKL_CHOOSE = 0x05,
    SKL_RESULT = 0x06,
    SKL_EVENT = 0x07,
    SKL_DROP = 0x08,
    SKL_DISMISS = 0x09,
    SKL_SNAP_BEGIN = 0x0A,
    SKL_SNAP_CHUNK = 0x0B,
    SKL_SNAP_END = 0x0C,
    SKL_DIR_HW_TO_PLUGIN = 0,
    SKL_DIR_PLUGIN_TO_HW = 1,
    SKL_OK = 0,
    SKL_REFUSE_VERSION = 1,
    SKL_REFUSE_CRC = 2,
    SKL_REFUSE_STATE = 3,
    SKL_LOCK_NONE = 0,
    SKL_LOCK_ASK = 1,
    SKL_LOCK_ON = 2,
    /* P19/P20: above P_COUNT, as host/sk_skl.h (the game blob follows the value slot) */
    SKL_META_PALETTE = 0x3FFE,
    SKL_META_LANG = 0x3FFD,
    SKL_META_GAME = 0x3FFC
};

/* 1 while locked: plugin owns musical time (seq must not free-run). */
uint8_t sk_lock_plugin_clock;
static uint8_t skl_lock_state;
static uint8_t skl_snap_buf[3584];
static uint32_t skl_snap_len, skl_snap_got, skl_snap_crc;
static uint8_t skl_snap_active;

static uint32_t skl_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, i, b;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (b = 0; b < 8u; b++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
}

static uint32_t skl_nor_crc(void)
{
#if SKM_FLASH
    /* Prefer project slot 0 payload CRC when present; else version string. */
    uint8_t tmp[64];
    int n = st_load(OBJ_PROJECT0, tmp, sizeof tmp);
    if (n > 0)
        return skl_crc32(tmp, (uint32_t)n);
#endif
    {
        const char *v = SKM_VERSION;
        uint32_t n = 0;
        while (v[n])
            n++;
        return skl_crc32((const uint8_t *)v, n);
    }
}

static void skl_put_u32_7(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t)(v & 0x7Fu);
    d[1] = (uint8_t)((v >> 7) & 0x7Fu);
    d[2] = (uint8_t)((v >> 14) & 0x7Fu);
    d[3] = (uint8_t)((v >> 21) & 0x7Fu);
    d[4] = (uint8_t)((v >> 28) & 0x0Fu);
}

static uint32_t skl_get_u32_7(const uint8_t *d)
{
    return (uint32_t)d[0] | (uint32_t)d[1] << 7 | (uint32_t)d[2] << 14 |
           (uint32_t)d[3] << 21 | (uint32_t)(d[4] & 0x0Fu) << 28;
}

static void skl_send(uint8_t cmd, const uint8_t *pay, uint32_t np)
{
    uint8_t f[96];
    uint32_t n = 0, i;
    f[n++] = 0xF0;
    f[n++] = SKL_H0;
    f[n++] = SKL_H1;
    f[n++] = SKL_H2;
    f[n++] = SKL_H3;
    f[n++] = cmd;
    for (i = 0; i < np && n + 1u < sizeof f; i++)
        f[n++] = (uint8_t)(pay[i] & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}

/* P19/P20: a game packet to the plug-in (the authority while locked). blob bytes are 7-bit safe. */
static void skl_game_tx(const uint8_t *d, uint32_t n)
{
    uint8_t pay[92];
    uint32_t i, m = 0;
    if (skl_lock_state != SKL_LOCK_ON || !d)
        return;
    pay[m++] = (uint8_t)(SKL_META_GAME & 0x7Fu);
    pay[m++] = (uint8_t)((SKL_META_GAME >> 7) & 0x7Fu);
    pay[m++] = 0;
    pay[m++] = 0;
    for (i = 0; i < n && m < sizeof pay; i++)
        pay[m++] = (uint8_t)(d[i] & 0x7Fu);
    skl_send(SKL_EVENT, pay, m);
}

static void skl_send_crc_ack(void)
{
    uint8_t p[5];
    skl_put_u32_7(p, skl_nor_crc());
    skl_send(SKL_CRC_ACK, p, 5);
}

static void skl_result(uint8_t r) { skl_send(SKL_RESULT, &r, 1); }

/* main loop: SKL before editor so unknown FL frames are not held. */
static int sk_link_service(void)
{
    const uint8_t *p;
    uint32_t n, k;
    uint8_t pay[32];
    const char *hv;
    uint8_t cmd;
    if (!ota_frame_get(&p, &n) || n < 4u)
        return 0;
    if (p[0] != SKL_H0 || p[1] != SKL_H1 || p[2] != SKL_H2 || p[3] != SKL_H3)
        return 0;
    cmd = n >= 5u ? p[4] : 0;
    switch (cmd) {
    case SKL_HELLO: {
        /* Peer version: NUL-terminated ASCII after HDR+cmd (same as HELLO_ACK). */
        char peer[32];
        uint32_t i, m = 0;
        int mismatch = 0;
        peer[0] = 0;
        for (i = 5; i < n && m + 1u < sizeof peer; i++) {
            if (!p[i])
                break;
            peer[m++] = (char)p[i];
        }
        peer[m] = 0;
        hv = SKM_VERSION;
        /* Empty peer version is not a mismatch (host sk_version_mismatch). */
        if (peer[0]) {
            for (i = 0; peer[i] || hv[i]; i++) {
                if (peer[i] != hv[i]) {
                    mismatch = 1;
                    break;
                }
            }
        }
        for (k = 0; hv[k] && k + 1u < sizeof pay; k++)
            pay[k] = (uint8_t)hv[k];
        pay[k++] = 0;
        ota_frame_done();
        skl_send(SKL_HELLO_ACK, pay, k);
        skl_send_crc_ack();
        if (mismatch) {
            skl_result(SKL_REFUSE_VERSION);
            skl_lock_state = SKL_LOCK_NONE;
        } else if (skl_lock_state != SKL_LOCK_ON) {
            skl_lock_state = SKL_LOCK_ASK;
        }
        sk_lock_plugin_clock = 0;
        return 1;
    }
    case SKL_CRC:
        ota_frame_done();
        skl_send_crc_ack();
        return 1;
    case SKL_CHOOSE: {
        uint8_t dir = n >= 6u ? p[5] : 0;
        ota_frame_done();
        if (skl_lock_state != SKL_LOCK_ASK) {
            skl_result(SKL_REFUSE_STATE);
            return 1;
        }
        if (dir == SKL_DIR_PLUGIN_TO_HW) {
            /* Prefer buffered SNAP payload into project slot 0. */
            if (skl_snap_got && skl_snap_got == skl_snap_len &&
                skl_crc32(skl_snap_buf, skl_snap_got) == skl_snap_crc) {
#if SKM_FLASH
                if (st_save(OBJ_PROJECT0, skl_snap_buf, skl_snap_got)) {
                    skl_result(SKL_REFUSE_STATE);
                    skl_lock_state = SKL_LOCK_NONE;
                    return 1;
                }
#endif
                project_load(0);
            } else if (project_save(0) != 0) {
                /* No snap buffer: keep current RAM as the keyboard snapshot. */
                skl_result(SKL_REFUSE_STATE);
                skl_lock_state = SKL_LOCK_NONE;
                return 1;
            }
        } else {
            project_load(0);
        }
        skl_lock_state = SKL_LOCK_ON;
        sk_lock_plugin_clock = 1;
        sk_game_set_mirror(1);                       /* P19/P20: the plug-in owns the game */
        skl_snap_active = 0;
        skl_result(SKL_OK);
        return 1;
    }
    case SKL_EVENT:
        if (skl_lock_state == SKL_LOCK_ON && n >= 10u) {
            uint32_t id = (uint32_t)p[5] | (uint32_t)p[6] << 7;
            int32_t v = (int32_t)((uint32_t)p[7] | (uint32_t)p[8] << 7) - 8192;
            if (id == SKL_META_GAME) {                /* P19/P20: the plug-in owns the game */
                uint8_t blob[96];
                uint32_t i, m = 0;
                for (i = 9u; i < n && m < sizeof blob; i++)
                    blob[m++] = (uint8_t)(p[i] & 0x7Fu);
                sk_game_apply_packet(blob, m);
            } else if (id == SKL_META_PALETTE) {
                settings.palette = (uint32_t)v % NPALETTES;
                palette_set(settings.palette);
                ui.force = 1;
            } else if (id == SKL_META_LANG) {                  /* SK_SKL_META_LANG */
                if (v)
                    ui_prefs = (uint8_t)(ui_prefs | 128u);
                else
                    ui_prefs = (uint8_t)(ui_prefs & (uint8_t)~128u);
                ui.force = 1;
            } else if (id < P_COUNT) {
                const param_desc_t *d = track_desc(TSEL, id);
                if (d)
                    v = clamp(v, d->min, d->max);
                TSEL->p[id] = (int16_t)v;
                ui.force = 1;
            }
        }
        ota_frame_done();
        return 1;
    case SKL_DROP:
        skl_lock_state = SKL_LOCK_NONE;
        sk_lock_plugin_clock = 0;
        sk_game_set_mirror(0);
        skl_snap_active = 0;
        ota_frame_done();
        return 1;
    case SKL_DISMISS:
        if (skl_lock_state == SKL_LOCK_ASK)
            skl_lock_state = SKL_LOCK_NONE;
        sk_lock_plugin_clock = 0;
        sk_game_set_mirror(0);
        skl_snap_active = 0;
        ota_frame_done();
        return 1;
    case SKL_SNAP_BEGIN:
        /* payload: dir(1) + size(5×7) + crc(5×7) — indices after HDR0..3 cmd */
        if (n >= 16u) {
            skl_snap_len = skl_get_u32_7(p + 6);
            skl_snap_crc = skl_get_u32_7(p + 11);
            if (skl_snap_len > sizeof skl_snap_buf)
                skl_snap_len = sizeof skl_snap_buf;
            skl_snap_got = 0;
            skl_snap_active = 1;
        }
        ota_frame_done();
        return 1;
    case SKL_SNAP_CHUNK:
        /* seq(2×7) + nybble pairs (hi,lo) → packed bytes */
        if (skl_snap_active && n >= 8u) {
            uint32_t seq = (uint32_t)p[5] | (uint32_t)p[6] << 7;
            uint32_t off = seq * 24u;
            uint32_t i;
            for (i = 7; i + 2u <= n && off < skl_snap_len && off < sizeof skl_snap_buf; i += 2u, off++)
                skl_snap_buf[off] = (uint8_t)(((p[i] & 0x0Fu) << 4) | (p[i + 1u] & 0x0Fu));
            if (off > skl_snap_got)
                skl_snap_got = off;
        }
        ota_frame_done();
        return 1;
    case SKL_SNAP_END:
        ota_frame_done();
        if (skl_snap_active && skl_snap_got == skl_snap_len &&
            skl_crc32(skl_snap_buf, skl_snap_got) == skl_snap_crc)
            skl_result(SKL_OK);
        else {
            skl_snap_active = 0;
            skl_result(SKL_REFUSE_CRC);
        }
        return 1;
    default:
        ota_frame_done();
        return 1;
    }
}

static void skl_emit_meta(uint32_t id, int32_t v)
{
    uint8_t pay[4];
    uint32_t u;
    if (skl_lock_state != SKL_LOCK_ON)
        return;
    u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    pay[0] = (uint8_t)(id & 0x7Fu);
    pay[1] = (uint8_t)((id >> 7) & 0x7Fu);
    pay[2] = (uint8_t)(u & 0x7Fu);
    pay[3] = (uint8_t)((u >> 7) & 0x7Fu);
    skl_send(SKL_EVENT, pay, 4);
}

void skl_notify_palette(uint32_t v)
{
    skl_emit_meta(0x3FFEu, (int32_t)(v % NPALETTES));
}

void skl_notify_lang(uint32_t v)
{
    skl_emit_meta(0x3FFDu, v ? 1 : 0);
}

#endif /* SKM_OTA */
