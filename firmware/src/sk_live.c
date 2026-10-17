/* SPDX-License-Identifier: GPL-3.0-only
 * StudioKEYS Ableton Live surface (USB MIDI cable 1).
 * Custom map for this panel — not FANTOM or Launchkey. Matched by
 * plugin/ableton/StudioKEYS/. See that folder's PROTOCOL.md. */
#define SK_LIVE_SURFACE 1
#ifndef SKM_ON_DEVICE
static volatile int16_t smk_an[8];
static uint8_t skl_pad_down[16], skl_pad_p[16];
#define smk_pad_down skl_pad_down
#define smk_pad_p    skl_pad_p
static uint32_t smk_extra_edges(void) { return 0; }
static uint32_t smk_extra_down(void) { return 0; }
#endif
static void sk_surf_dispatch(void); /* sk_surf.c (included after this file) */

enum {
    SKL_FACE_SESSION = 0,
    SKL_FACE_MIXER = 1,
    SKL_PAD_CLIPS = 0,
    SKL_PAD_DRUMS = 1
};

enum {
    SKL_N_PAD0 = 36,
    SKL_N_STOP_ALL = 44,
    SKL_N_BANK_M = 46,
    SKL_N_BANK_P = 47,
    SKL_N_MUTE0 = 48,
    SKL_N_SOLO0 = 52,
    SKL_N_ARM0 = 56,
    SKL_N_SEL0 = 60,
    SKL_N_FACE = 70,       /* PARA: Session <-> Mixer */
    SKL_N_PADFACE = 71,    /* SCALE: Clips <-> Drums */
    SKL_N_KEYS = 72,       /* GLOBE: keys -> synth or armed Live track */
    SKL_N_PLAY = 80,
    SKL_N_STOP = 81,
    SKL_N_REC = 82,
    SKL_N_METRO = 83,
    SKL_N_CAPTURE = 84,
    SKL_N_SCENE_M = 85,
    SKL_N_SCENE_P = 86
};

enum { SKL_CC_KNOB0 = 21, SKL_CC_FADER0 = 41 };
enum { SKL_X_LEFT = 0, SKL_X_RIGHT = 1, SKL_X_FADER = 2, SKL_X_CHORD = 3 };

typedef struct {
    uint8_t face;
    uint8_t pad_face;
    uint8_t keys_to_daw;
    uint8_t bank;
    uint8_t playing, recording, metro;
    uint8_t clip_led[8];
    uint8_t mute[4], solo[4], arm[4], sel[4];
    uint8_t fader[4];
    char name[4][9];
    uint8_t dirty;
} sk_live_t;

static sk_live_t sk_live;
uint8_t sk_live_keys_daw;                               /* smk_panel.c: performance-port key routing */

static uint32_t skl_pkt(uint32_t st, uint32_t d1, uint32_t d2)
{
    return (1u << 4) | (st >> 4) | (st << 8) | ((d1 & 127u) << 16) | ((d2 & 127u) << 24);
}

static void skl_note(uint32_t note, uint32_t vel)
{
    midi_out_event(skl_pkt(vel ? 0x90u : 0x80u, note, vel));
}

static void skl_cc(uint32_t cc, uint32_t val)
{
    midi_out_event(skl_pkt(0xB0u, cc, val));
}

static void sk_live_reset(void)
{
    uint32_t i;
    memset(&sk_live, 0, sizeof sk_live);
    sk_live_keys_daw = 0;
    for (i = 0; i < 4u; i++) {
        sk_live.fader[i] = 100;
        str_cpy(sk_live.name[i], "----", sizeof sk_live.name[i]);
    }
    sk_live.dirty = 1;
}

static void sk_live_enter(void) { sk_live_reset(); }

static void sk_live_sysex(const uint8_t *d, uint32_t n)
{
    uint32_t i;
    if (n < 7u || d[0] != 0xF0u || d[1] != 0x7Du || d[2] != 0x53u || d[3] != 0x4Bu || d[4] != 0x4Du)
        return;
    if (d[5] == 0x01u && n >= 13u) {
        sk_live.face = d[6] & 1u;
        sk_live.pad_face = d[7] & 1u;
        sk_live.bank = d[8];
        sk_live.keys_to_daw = d[9] & 1u;
        sk_live_keys_daw = sk_live.keys_to_daw;
        sk_live.playing = d[10] & 1u;
        sk_live.recording = d[11] & 1u;
        sk_live.metro = d[12] & 1u;
        sk_live.dirty = 1;
    } else if (d[5] == 0x02u && n >= 15u) {
        uint32_t s = d[6] & 3u;
        for (i = 0; i < 8u && 7u + i < n - 1u; i++) {
            char c = (char)d[7u + i];
            sk_live.name[s][i] = (c >= 32 && c < 127) ? c : ' ';
        }
        sk_live.name[s][8] = 0;
        sk_live.dirty = 1;
    }
}

static void sk_live_usb_msg(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    static uint8_t sx[64];
    static uint32_t sxn;
    if (cin >= 4u && cin <= 7u) {
        uint32_t k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (k = 0; k < nb; k++) {
            uint8_t b = (uint8_t)(pkt >> (8u * (k + 1u)));
            if (b == 0xF0u) {
                sxn = 0;
                if (sxn < sizeof sx)
                    sx[sxn++] = b;
            } else if (sxn && sxn < sizeof sx) {
                sx[sxn++] = b;
                if (b == 0xF7u) {
                    sk_live_sysex(sx, sxn);
                    sxn = 0;
                }
            }
        }
        return;
    }
    if ((st & 0xF0u) == 0x90u || (st & 0xF0u) == 0x80u) {
        uint32_t on = (st & 0xF0u) == 0x90u && d2;
        if (d1 >= SKL_N_PAD0 && d1 < SKL_N_PAD0 + 8u) {
            sk_live.clip_led[d1 - SKL_N_PAD0] = on ? (uint8_t)(d2 ? d2 : 127u) : 0;
            sk_live.dirty = 1;
        } else if (d1 >= SKL_N_MUTE0 && d1 < SKL_N_MUTE0 + 4u) {
            sk_live.mute[d1 - SKL_N_MUTE0] = (uint8_t)on;
            sk_live.dirty = 1;
        } else if (d1 >= SKL_N_SOLO0 && d1 < SKL_N_SOLO0 + 4u) {
            sk_live.solo[d1 - SKL_N_SOLO0] = (uint8_t)on;
            sk_live.dirty = 1;
        } else if (d1 >= SKL_N_ARM0 && d1 < SKL_N_ARM0 + 4u) {
            sk_live.arm[d1 - SKL_N_ARM0] = (uint8_t)on;
            sk_live.dirty = 1;
        } else if (d1 >= SKL_N_SEL0 && d1 < SKL_N_SEL0 + 4u) {
            sk_live.sel[d1 - SKL_N_SEL0] = (uint8_t)on;
            sk_live.dirty = 1;
        } else if (d1 == SKL_N_PLAY) {
            sk_live.playing = (uint8_t)on;
            sk_live.dirty = 1;
        } else if (d1 == SKL_N_REC) {
            sk_live.recording = (uint8_t)on;
            sk_live.dirty = 1;
        } else if (d1 == SKL_N_METRO) {
            sk_live.metro = (uint8_t)on;
            sk_live.dirty = 1;
        } else if (d1 == SKL_N_BANK_M && on) {
            if (sk_live.bank >= 4u)
                sk_live.bank = (uint8_t)(sk_live.bank - 4u);
            sk_live.dirty = 1;
        } else if (d1 == SKL_N_BANK_P && on) {
            if (sk_live.bank < 124u)
                sk_live.bank = (uint8_t)(sk_live.bank + 4u);
            sk_live.dirty = 1;
        }
    } else if ((st & 0xF0u) == 0xB0u && d1 >= SKL_CC_FADER0 && d1 < SKL_CC_FADER0 + 4u) {
        sk_live.fader[d1 - SKL_CC_FADER0] = (uint8_t)d2;
        sk_live.dirty = 1;
    }
}

static void skl_hold(uint32_t note, int down, int *was)
{
    if (down && !*was)
        skl_note(note, 127u);
    else if (!down && *was)
        skl_note(note, 0);
    *was = down;
}

static void sk_live_poll(uint32_t pressed)
{
    static uint8_t pad_was[8], fader_cc[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    static int was_play, was_stop, was_rec, was_metro, was_cap;
    static int was_face, was_padf, was_keys, was_stopall, was_sc_m, was_sc_p;
    static int was_bank_m, was_bank_p;
    static int was_mute[4], was_solo[4], was_arm[4], was_sel[4];
    static const uint8_t faddr[4] = {3, 0, 1, 2};       /* SMK_FADER1..4 mux addresses */
    uint32_t i, e, xe, down;

    sk_surf_dispatch();

    skl_hold(SKL_N_PLAY, (fm1_in.buttons >> panel.btn[B_PLAY]) & 1u, &was_play);
    skl_hold(SKL_N_STOP, (fm1_in.buttons >> panel.btn[B_SAVE]) & 1u, &was_stop);
    skl_hold(SKL_N_REC, (fm1_in.buttons >> panel.btn[B_REC]) & 1u, &was_rec);
    skl_hold(SKL_N_METRO, (fm1_in.buttons >> panel.btn[B_ARP]) & 1u, &was_metro);
    skl_hold(SKL_N_FACE, (fm1_in.buttons >> panel.btn[B_EDIT]) & 1u, &was_face);
    skl_hold(SKL_N_PADFACE, (fm1_in.buttons >> panel.btn[B_SCL]) & 1u, &was_padf);
    skl_hold(SKL_N_KEYS, (fm1_in.buttons >> panel.btn[B_GLO]) & 1u, &was_keys);
    skl_hold(SKL_N_STOP_ALL, (fm1_in.buttons >> panel.btn[B_FX]) & 1u, &was_stopall);
    skl_hold(SKL_N_SCENE_M, (fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u, &was_sc_m);
    skl_hold(SKL_N_SCENE_P, (fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u, &was_sc_p);

    down = smk_extra_down();
    xe = smk_extra_edges();
    skl_hold(SKL_N_CAPTURE, (down & (1u << SKL_X_CHORD)) != 0, &was_cap);
    skl_hold(SKL_N_BANK_M, (down & (1u << SKL_X_LEFT)) != 0, &was_bank_m);
    skl_hold(SKL_N_BANK_P, (down & (1u << SKL_X_RIGHT)) != 0 || (down & (1u << SKL_X_FADER)) != 0,
             &was_bank_p);
    (void)xe;

    if ((pressed >> panel.btn[B_EDIT]) & 1u) {
        sk_live.face ^= 1u;
        sk_live.dirty = 1;
        ui_message(sk_live.face ? "MIXER" : "SESSION");
    }
    if ((pressed >> panel.btn[B_SCL]) & 1u) {
        sk_live.pad_face ^= 1u;
        sk_live.dirty = 1;
        ui_message(sk_live.pad_face ? "DRUM PADS" : "CLIP PADS");
    }
    if ((pressed >> panel.btn[B_GLO]) & 1u) {
        sk_live.keys_to_daw ^= 1u;
        sk_live_keys_daw = sk_live.keys_to_daw;
        sk_live.dirty = 1;
        ui_message(sk_live.keys_to_daw ? "KEYS -> LIVE" : "KEYS -> SYNTH");
    }

    if (sk_live.face == SKL_FACE_MIXER) {
        for (i = 0; i < 4u; i++) {
            skl_hold(SKL_N_MUTE0 + i, smk_pad_down[i], &was_mute[i]);
            skl_hold(SKL_N_SOLO0 + i, smk_pad_down[i + 4u], &was_solo[i]);
            skl_hold(SKL_N_ARM0 + i, smk_pad_down[i + 8u], &was_arm[i]);
            skl_hold(SKL_N_SEL0 + i, smk_pad_down[i + 12u], &was_sel[i]);
        }
    } else {
        for (i = 0; i < 8u; i++) {
            int on = smk_pad_down[i] || smk_pad_down[i + 8u];
            if (on && !pad_was[i])
                skl_note(SKL_N_PAD0 + i, smk_pad_p[i] > 8u ? smk_pad_p[i] : 100u);
            else if (!on && pad_was[i])
                skl_note(SKL_N_PAD0 + i, 0);
            pad_was[i] = (uint8_t)on;
        }
    }

    for (e = 0; e < 8u; e++) {
        int32_t s = fm1_enc_take(e);
        int v;
        if (!s)
            continue;
        v = 64 + (int)s;
        if (v < 1)
            v = 1;
        if (v > 127)
            v = 127;
        skl_cc(SKL_CC_KNOB0 + e, (uint32_t)v);
    }

    for (i = 0; i < 4u; i++) {
        int raw = smk_an[faddr[i]];
        uint32_t v;
        if (raw < 0)
            continue;
        v = (uint32_t)raw >> 3;
        if (v > 127u)
            v = 127u;
        if (v == fader_cc[i])
            continue;
        fader_cc[i] = (uint8_t)v;
        sk_live.fader[i] = (uint8_t)v;
        skl_cc(SKL_CC_FADER0 + i, v);
        sk_live.dirty = 1;
    }
}

static const sk_live_t *sk_live_state(void) { return &sk_live; }
static void sk_live_ack_dirty(void) { sk_live.dirty = 0; }
