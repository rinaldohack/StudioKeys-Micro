/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * Dispatch USB MIDI surface packets queued in usb.c (cables 1 and 2),
 * and plugin→keyboard surface SysEx on the link cable (cable 3). */
static void sk_live_usb_msg(uint32_t pkt);
static void sk_hui_usb_msg(uint32_t pkt);

static void sk_surf_dispatch(void)
{
    uint32_t pkt;
    while (usb_surf_pop(&pkt)) {
        uint32_t cable = (pkt >> 4) & 15u;
        if (cable == 1u)
            sk_live_usb_msg(pkt);
        else if (cable == 2u)
            sk_hui_usb_msg(pkt);
    }
}

/* Plugin surface on link: F0 7D 53 4B 53 cmd … F7 (SurfaceCard.h).
 * Keyboard shows/applies and forwards on Live (cable 1) or HUI (cable 2). */
void sk_surf_link_sysex(const uint8_t *d, uint32_t n)
{
    uint8_t cmd;
    if (!d || n < 7u || d[0] != 0xF0u || d[n - 1u] != 0xF7u)
        return;
    if (d[1] != 0x7Du || d[2] != 0x53u || d[3] != 0x4Bu || d[4] != 0x53u)
        return;
    cmd = d[5];
    if (cmd == 0x01u && n >= 9u) {
        /* Live note → cable 1 */
        uint32_t note = d[6] & 127u, vel = d[7] & 127u;
        uint32_t st = vel ? 0x90u : 0x80u;
        midi_out_event((1u << 4) | (st >> 4) | (st << 8) | (note << 16) | (vel << 24));
        if (smk_remote == 1u) {
            /* Keep Live card LEDs in step when already in Live. */
            if (note >= 36u && note < 44u)
                sk_live.clip_led[note - 36u] = vel ? 1u : 0u;
            sk_live.dirty = 1;
        }
    } else if (cmd == 0x02u && n >= 9u) {
        uint32_t cc = d[6] & 127u, val = d[7] & 127u;
        midi_out_event((1u << 4) | 0xBu | (0xB0u << 8) | (cc << 16) | (val << 24));
        if (smk_remote == 1u && cc >= 41u && cc < 45u) {
            sk_live.fader[cc - 41u] = (uint8_t)val;
            sk_live.dirty = 1;
        }
    } else if (cmd == 0x11u && n >= 9u) {
        /* HUI strip fader: strip 0..7, value 0..127 → pitch-bend on ch strip */
        uint32_t strip = d[6] & 7u, val = d[7] & 127u;
        uint32_t v14 = val * 127u;
        if (v14 > 0x3FFFu)
            v14 = 0x3FFFu;
        midi_out_event((2u << 4) | 0xEu | ((0xE0u | strip) << 8) |
                       ((v14 & 127u) << 16) | (((v14 >> 7) & 127u) << 24));
        if (smk_remote == 2u) {
            sk_hui.fader[strip] = (uint8_t)val;
            sk_hui.dirty = 1;
        }
    } else if (cmd == 0x12u && n >= 9u) {
        /* HUI note → cable 2 (bank / transport / mute / solo / …) */
        uint32_t note = d[6] & 127u, vel = d[7] & 127u;
        uint32_t st = vel ? 0x90u : 0x80u;
        midi_out_event((2u << 4) | (st >> 4) | (st << 8) | (note << 16) | (vel << 24));
    }
}
