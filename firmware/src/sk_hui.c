/* SPDX-License-Identifier: GPL-3.0-only
 * StudioKEYS Mackie HUI surface (USB MIDI cable 2).
 * Real HUI notes/CCs/SysEx. Four faders bank by 4 inside the eight-strip
 * window; bank L/R move that window in the DAW. */
static void sk_surf_dispatch(void); /* sk_surf.c (included after this file) */
enum {
    HUI_N_REC0 = 0x00,
    HUI_N_SOLO0 = 0x08,
    HUI_N_MUTE0 = 0x10,
    HUI_N_SEL0 = 0x18,
    HUI_N_VPOT0 = 0x20,
    HUI_N_BANK_L = 0x2E,
    HUI_N_BANK_R = 0x2F,
    HUI_N_CH_L = 0x30,
    HUI_N_CH_R = 0x31,
    HUI_N_REW = 0x4B,
    HUI_N_FF = 0x4C,
    HUI_N_STOP = 0x4D,
    HUI_N_PLAY = 0x4E,
    HUI_N_RECORD = 0x4F
};
enum { HUI_CC_VPOT0 = 0x10, HUI_CC_JOG = 0x0C };
enum { HUI_X_LEFT = 0, HUI_X_RIGHT = 1, HUI_X_FADER = 2, HUI_X_CHORD = 3, HUI_X_REPEAT = 4 };

typedef struct {
    uint8_t nibble;              /* 0: strips 0-3 on faders, 1: strips 4-7 */
    uint8_t bank;                /* DAW window offset display (steps of 8 via bank L/R) */
    uint8_t mute[8], solo[8], rec[8], sel[8];
    uint8_t fader[8];
    char name[8][5];
    char tc[12];
    uint8_t playing, recording;
    uint8_t dirty;
} sk_hui_t;

static sk_hui_t sk_hui;

static uint32_t skh_pkt(uint32_t st, uint32_t d1, uint32_t d2)
{
    return (2u << 4) | (st >> 4) | (st << 8) | ((d1 & 127u) << 16) | ((d2 & 127u) << 24);
}

static void skh_note(uint32_t note, uint32_t vel)
{
    midi_out_event(skh_pkt(vel ? 0x90u : 0x80u, note, vel));
}

static void skh_cc(uint32_t cc, uint32_t val)
{
    midi_out_event(skh_pkt(0xB0u, cc, val));
}

static void skh_pb(uint32_t ch, uint32_t v14)
{
    uint32_t st = 0xE0u | (ch & 7u);
    midi_out_event(skh_pkt(st, v14 & 127u, (v14 >> 7) & 127u));
}

static void skh_sysex_raw(const uint8_t *d, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t left = n - i, cin, pkt;
        uint8_t b0 = 0, b1 = 0, b2 = 0;
        if (left >= 3u) {
            cin = (i + 3u >= n && d[n - 1u] == 0xF7u && left == 3u) ? 7u : 4u;
            b0 = d[i];
            b1 = d[i + 1];
            b2 = d[i + 2];
            i += 3u;
            if (b2 == 0xF7u && cin == 4u)
                cin = 7u;
        } else if (left == 2u) {
            cin = 6u;
            b0 = d[i];
            b1 = d[i + 1];
            i += 2u;
        } else {
            cin = 5u;
            b0 = d[i++];
        }
        pkt = (2u << 4) | cin | (uint32_t)b0 << 8 | (uint32_t)b1 << 16 | (uint32_t)b2 << 24;
        midi_out_event(pkt);
    }
}

static void skh_ping_reply(void)
{
    static const uint8_t r[] = {0xF0u, 0x00u, 0x00u, 0x66u, 0x05u, 0x00u, 0x00u, 0x01u, 0xF7u};
    skh_sysex_raw(r, sizeof r);
}

static void skh_hold(uint32_t note, int down, int *was)
{
    if (down && !*was)
        skh_note(note, 127u);
    else if (!down && *was)
        skh_note(note, 0);
    *was = down;
}

static void sk_hui_reset(void)
{
    uint32_t i;
    memset(&sk_hui, 0, sizeof sk_hui);
    for (i = 0; i < 8u; i++) {
        sk_hui.fader[i] = 100;
        str_cpy(sk_hui.name[i], "----", sizeof sk_hui.name[i]);
    }
    str_cpy(sk_hui.tc, "--:--:--:--", sizeof sk_hui.tc);
    sk_hui.dirty = 1;
}

static void sk_hui_enter(void) { sk_hui_reset(); }

static void sk_hui_sysex(const uint8_t *d, uint32_t n)
{
    if (n < 6u || d[0] != 0xF0u || d[1] || d[2] || d[3] != 0x66u || d[4] != 0x05u)
        return;
    if (n >= 8u && d[5] == 0x00u && d[6] == 0x00u && d[7] == 0x00u) {
        skh_ping_reply();
        return;
    }
    if (n >= 9u && d[5] == 0x12u) {
        uint32_t off = d[6], i;
        for (i = 7; i + 1u < n && d[i] != 0xF7u; i++) {
            uint32_t at = off + (i - 7u), strip = at / 4u, col = at % 4u;
            char c = (char)d[i];
            if (c < 32 || c >= 127)
                c = ' ';
            if (strip < 8u) {
                sk_hui.name[strip][col] = c;
                sk_hui.name[strip][4] = 0;
            }
        }
        sk_hui.dirty = 1;
        return;
    }
    if (n >= 8u && d[5] == 0x11u) {
        uint32_t i, o = 0;
        for (i = 6; i + 1u < n && d[i] != 0xF7u && o + 1u < sizeof sk_hui.tc; i++) {
            char c = (char)d[i];
            if (c >= 32 && c < 127)
                sk_hui.tc[o++] = c;
        }
        sk_hui.tc[o] = 0;
        sk_hui.dirty = 1;
    }
}

static void sk_hui_usb_msg(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    static uint8_t sx[96];
    static uint32_t sxn;
    if (cin >= 4u && cin <= 7u) {
        uint32_t k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (k = 0; k < nb; k++) {
            uint8_t b = (uint8_t)(pkt >> (8u * (k + 1u)));
            if (b == 0xF0u) {
                sxn = 0;
                sx[sxn++] = b;
            } else if (sxn && sxn < sizeof sx) {
                sx[sxn++] = b;
                if (b == 0xF7u) {
                    sk_hui_sysex(sx, sxn);
                    sxn = 0;
                }
            }
        }
        return;
    }
    if ((st & 0xF0u) == 0xE0u) {
        uint32_t ch = st & 7u;
        sk_hui.fader[ch] = (uint8_t)((d1 | (d2 << 7)) >> 7);
        sk_hui.dirty = 1;
        return;
    }
    if ((st & 0xF0u) == 0x90u || (st & 0xF0u) == 0x80u) {
        uint32_t on = (st & 0xF0u) == 0x90u && d2;
        if (d1 < 8u) {
            sk_hui.rec[d1] = (uint8_t)on;
            sk_hui.dirty = 1;
        } else if (d1 < 0x10u) {
            sk_hui.solo[d1 - 8u] = (uint8_t)on;
            sk_hui.dirty = 1;
        } else if (d1 < 0x18u) {
            sk_hui.mute[d1 - 0x10u] = (uint8_t)on;
            sk_hui.dirty = 1;
        } else if (d1 < 0x20u) {
            sk_hui.sel[d1 - 0x18u] = (uint8_t)on;
            sk_hui.dirty = 1;
        } else if (d1 == HUI_N_PLAY) {
            sk_hui.playing = (uint8_t)on;
            sk_hui.dirty = 1;
        } else if (d1 == HUI_N_RECORD) {
            sk_hui.recording = (uint8_t)on;
            sk_hui.dirty = 1;
        } else if (d1 == HUI_N_BANK_L && on) {
            if (sk_hui.bank >= 8u)
                sk_hui.bank = (uint8_t)(sk_hui.bank - 8u);
            sk_hui.dirty = 1;
        } else if (d1 == HUI_N_BANK_R && on) {
            if (sk_hui.bank < 120u)
                sk_hui.bank = (uint8_t)(sk_hui.bank + 8u);
            sk_hui.dirty = 1;
        }
    }
}

static void sk_hui_poll(uint32_t pressed)
{
    static uint8_t fader_was[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    static int was_play, was_stop, was_rec, was_rew, was_ff;
    static int was_bank_l, was_bank_r;
    static int was_mute[4], was_solo[4], was_recb[4], was_sel[4];
    static const uint8_t faddr[4] = {3, 0, 1, 2};
    uint32_t i, e, base, down, xe;

    sk_surf_dispatch();
    base = sk_hui.nibble ? 4u : 0u;

    skh_hold(HUI_N_PLAY, (fm1_in.buttons >> panel.btn[B_PLAY]) & 1u, &was_play);
    skh_hold(HUI_N_STOP, (fm1_in.buttons >> panel.btn[B_SAVE]) & 1u, &was_stop);
    skh_hold(HUI_N_RECORD, (fm1_in.buttons >> panel.btn[B_REC]) & 1u, &was_rec);
    skh_hold(HUI_N_REW, (fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u, &was_rew);
    skh_hold(HUI_N_FF, (fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u, &was_ff);

    down = smk_extra_down();
    xe = smk_extra_edges();
    skh_hold(HUI_N_BANK_L, (down & (1u << HUI_X_LEFT)) != 0, &was_bank_l);
    skh_hold(HUI_N_BANK_R, (down & (1u << HUI_X_RIGHT)) != 0, &was_bank_r);

    if (xe & (1u << HUI_X_FADER)) {
        sk_hui.nibble ^= 1u;
        sk_hui.dirty = 1;
        ui_message(sk_hui.nibble ? "STRIPS 5-8" : "STRIPS 1-4");
    }
    if (xe & (1u << HUI_X_CHORD)) {
        skh_note(HUI_N_CH_L, 127u);
        skh_note(HUI_N_CH_L, 0);
    }
    if (xe & (1u << HUI_X_REPEAT)) {
        skh_note(HUI_N_CH_R, 127u);
        skh_note(HUI_N_CH_R, 0);
    }
    (void)pressed;

    for (i = 0; i < 4u; i++) {
        uint32_t s = base + i;
        skh_hold(HUI_N_SEL0 + s, smk_pad_down[i], &was_sel[i]);
        skh_hold(HUI_N_MUTE0 + s, smk_pad_down[i + 4u], &was_mute[i]);
        skh_hold(HUI_N_SOLO0 + s, smk_pad_down[i + 8u], &was_solo[i]);
        skh_hold(HUI_N_REC0 + s, smk_pad_down[i + 12u], &was_recb[i]);
    }

    for (e = 0; e < 8u; e++) {
        int32_t s = fm1_enc_take(e);
        uint32_t v;
        if (!s)
            continue;
        if (s > 0) {
            v = (uint32_t)(s > 63 ? 63 : s);
        } else {
            v = (uint32_t)(-s);
            if (v > 63u)
                v = 63u;
            v |= 0x40u;
        }
        skh_cc(HUI_CC_VPOT0 + e, v);
    }

    {
        static int16_t jog_prev = -1;
        int raw = smk_an[4];
        if (raw >= 0) {
            int d;
            if (jog_prev < 0)
                jog_prev = (int16_t)raw;
            d = raw - jog_prev;
            if (d > 8 || d < -8) {
                int step = d >> 3;
                uint32_t v;
                if (step > 63)
                    step = 63;
                if (step < -63)
                    step = -63;
                v = step >= 0 ? (uint32_t)step : (uint32_t)((-step) | 0x40);
                skh_cc(HUI_CC_JOG, v);
                jog_prev = (int16_t)raw;
            }
        }
    }

    for (i = 0; i < 4u; i++) {
        int raw = smk_an[faddr[i]];
        uint32_t v7, strip;
        if (raw < 0)
            continue;
        v7 = (uint32_t)raw >> 3;
        if (v7 > 127u)
            v7 = 127u;
        if (v7 == fader_was[i])
            continue;
        fader_was[i] = (uint8_t)v7;
        strip = base + i;
        sk_hui.fader[strip] = (uint8_t)v7;
        skh_pb(strip, v7 << 7);
        sk_hui.dirty = 1;
    }
}

static const sk_hui_t *sk_hui_state(void) { return &sk_hui; }
static void sk_hui_ack_dirty(void) { sk_hui.dirty = 0; }
