/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * The SMK-37 keybed and pads are a MIDI device inside the instrument.
 * The scan (hal/fm1_input.h) posts channel messages; this drains them onto
 * the same queue USB and the TRS jack use. Source 3 is the panel, so a
 * local note is not an external clock.
 *
 * smk_ctrl is the dumb controller: the panel still leaves on USB and TRS,
 * and it does not play the internal engine. USB MIDI IN is a different
 * source and keeps playing in either mode.
 *
 * smk_remote Live/HUI: surface traffic is built in sk_live.c / sk_hui.c.
 * Keys either play the synth or leave on the performance cable (Live
 * key-routing). Pads are not forwarded raw on the surface cables. */
#include "../hal/fm1_uart.h"   /* TRS MIDI OUT; the RX driver stays off (PH8 is a matrix row) */
extern uint8_t smk_remote;         /* ui.c: 0 instrument, 1 Live, 2 HUI */
extern uint8_t sk_live_keys_daw;   /* sk_live.c: 1 = keys on performance port */
static uint32_t smk_cc_q[16];
static volatile uint32_t smk_cc_w, smk_cc_r;

/* Cable 0 is performance. Cable 1 is Live, cable 2 is HUI, cable 3 is the
 * plugin link (host SysEx only). The panel leaves on 0 / 1 / 2; link stays quiet. */
static uint32_t sk_cable(void)
{
    return smk_remote == 1u ? 1u : smk_remote == 2u ? 2u : 0u;
}

static uint32_t sk_pkt_on(uint32_t cable, uint32_t st, uint32_t d1, uint32_t d2)
{
    return (cable << 4) | (st >> 4) | (st << 8) | ((d1 & 127u) << 16) | ((d2 & 127u) << 24);
}

static uint32_t sk_pkt(uint32_t st, uint32_t d1, uint32_t d2)
{
    return sk_pkt_on(sk_cable(), st, d1, d2);
}

void smk_ctrl_send(uint32_t ch, uint32_t cc, uint32_t val)
{
    uint32_t st = 0xB0u | (ch & 15u);
    uint32_t pkt = sk_pkt(st, cc, val);
    if (smk_cc_w - smk_cc_r >= 16u)
        return;
    smk_cc_q[smk_cc_w & 15u] = pkt;
    RING_PUBLISH();
    smk_cc_w++;
}

static int sk_is_pad(uint32_t st, uint32_t d1)
{
    uint32_t ch = st & 0x0Fu, kind = st & 0xF0u;
    if (kind != 0x80u && kind != 0x90u && kind != 0xD0u)
        return 0;
    if (ch == SMK_PAD_CH)
        return 1;
    if (d1 >= SMK_PAD_NOTE0 && d1 < SMK_PAD_NOTE0 + SMK_NPADS)
        return 1;
    return 0;
}

static int sk_is_key(uint32_t st, uint32_t d1)
{
    uint32_t kind = st & 0xF0u;
    if (kind != 0x80u && kind != 0x90u)
        return 0;
    if (sk_is_pad(st, d1))
        return 0;
    return 1;
}

static void smk_panel_poll(void)
{
    while (smk_cc_r != smk_cc_w) {
        midi_out_event(smk_cc_q[smk_cc_r & 15u]);
        smk_cc_r++;
    }
    while (smk_qr != smk_qw) {
        uint32_t at = smk_qr % SMK_Q;
        uint32_t st = smk_q_st[at], d1 = smk_q_d1[at], d2 = smk_q_d2[at];
        uint32_t kind = st & 0xF0u;
        int quiet = smk_ctrl || smk_remote;
        smk_qr++;

        if (smk_remote == 1u) {
            /* Live: pads are sk_live_poll; keys synth or performance cable 0. */
            if (sk_is_pad(st, d1))
                continue;
            if (sk_is_key(st, d1) || kind == 0xE0u || (kind == 0xB0u && (d1 == 1u || d1 == 64u))) {
                if (sk_live_keys_daw) {
                    midi_out_event(sk_pkt_on(0u, st, d1, d2));
                    if (kind == 0x80u && !midi_enqueue(sk_pkt_on(0u, st, d1, d2), 3u))
                        midi_in_overflow = 1;            /* note-off still clears engine */
                } else {
                    if (!midi_enqueue(sk_pkt_on(0u, st, d1, d2), 3u))
                        midi_in_overflow = 1;
                }
                continue;
            }
            continue;
        }
        if (smk_remote == 2u) {
            /* HUI: pads/faders/knobs are sk_hui_poll; keys keep the synth. */
            if (sk_is_pad(st, d1))
                continue;
            if (sk_is_key(st, d1) || kind == 0xE0u || (kind == 0xB0u && (d1 == 1u || d1 == 64u))) {
                if ((!quiet || kind == 0x80u) && !midi_enqueue(sk_pkt_on(0u, st, d1, d2), 3u))
                    midi_in_overflow = 1;
                continue;
            }
            continue;
        }

        {
            uint32_t pkt = sk_pkt(st, d1, d2);
            /* Controller stays silent. A note-off still reaches the engine so a
             * key held across the switch does not stick. */
            if ((!quiet || kind == 0x80u) && !midi_enqueue(pkt, 3u))
                midi_in_overflow = 1;
            midi_out_event(pkt);
        }
    }
    fm1_uart1_tx_pump();
}
