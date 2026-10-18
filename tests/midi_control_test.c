/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * End-to-end channel input and external-clock tests using the real queues,
 * UART parser, ownership model, sequencer, voice renderer and UI source. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void midi_test_reset(void)
{
    ui_power_on();
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_owners, 0, sizeof midi_owners); memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target); memset(&midi_clock, 0, sizeof midi_clock);
    memset(&um, 0, sizeof um); memset(midi_in_source, 0, sizeof midi_in_source);
    mi_r = mi_w = 0; midi_in_overflow = 0; midi_beat_samples = 0;
    fm1_in.notes = kb_prev = 0; fm1_ms = 0; song.sel = 0; midi_hint = 0;
    song.g[G_ROUTE] = 0;                           /* MIDI IN is MULTI by default (params.c PE("ROUT", ..., 0)) */
    events_block(CTL);
}
static void queued(uint32_t st, uint32_t d1, uint32_t d2, uint32_t source)
{
    midi_enqueue((st >= 0xF8u ? 0xFu : st >> 4) | st << 8 | d1 << 16 | d2 << 24, source);
    events_block(CTL);
}
static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static int controls_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0xE0, 127, 127, 1);
    bad += check("USB maximum bend is exactly the default +2 semitones", midi_bend_target[0] == 512);
    queued(0xE0, 0, 0, 1);
    bad += check("USB minimum bend is exactly the default -2 semitones", midi_bend_target[0] == -512);
    queued(0xE0, 0, 64, 1);
    bad += check("bend centre is zero", midi_bend_target[0] == 0);
    queued(0xB0, 101, 0, 1); queued(0xB0, 100, 0, 1); queued(0xB0, 6, 12, 1); queued(0xB0, 38, 50, 1);
    queued(0xE0, 127, 127, 1);
    bad += check("RPN0 supports semitone and cent bend sensitivity", midi_ch[0].semis == 12 && midi_ch[0].cents == 50 && midi_bend_target[0] == 3200);
    queued(0xB0, 99, 0, 1); queued(0xB0, 6, 24, 1);
    bad += check("selecting NRPN cancels RPN data entry", midi_ch[0].semis == 12);
    queued(0x90, 60, 100, 1); int32_t out[CTL]; int32_t before = midi_bend_q8[0];
    track_render(t, out, CTL);
    bad += check("bend is smoothed toward the target in the real voice renderer", midi_bend_q8[0] > before && midi_bend_q8[0] < midi_bend_target[0]);
    for (uint32_t i = 0; i < 64; i++) track_render(t, out, CTL);
    bad += check("bend smoothing reaches the exact target without changing saved pitch", midi_bend_q8[0] == 3200 && t->p[P_TRANS] == 0 && t->v[0].note == 60);
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers centres bend and preserves RPN sensitivity", !midi_bend_target[0] && midi_ch[0].semis == 12 && gate_note(t, 60));
    queued(0xB0, 1, 87, 1); queued(0xB0, 11, 23, 1); queued(0xD0, 90, 0, 1);
    bad += check("existing modwheel/expression/aftertouch stay routed to the track", t->mw == 87 && t->ex_off == 104 && t->at == 90);
    queued(0xE4, 127, 127, 1);                     /* channel 5 is part 4: the drum track */
    bad += check("drum track ignores pitch bend", !midi_bend_target[3]);
    return bad;
}
static int sustain_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0x90, 60, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 60, 0, 1);
    bad += check("pedal holds a released synth note and its owner", gate_note(t, 60) && midi_notes[0][60] == (1u | MIDI_PEDAL_NOTE));
    queued(0xB0, 64, 0, 1);
    bad += check("pedal release releases its held note", !gate_note(t, 60) && !midi_notes[0][60] && !midi_owners[0]);
    queued(0x90, 62, 100, 1); queued(0xB0, 64, 127, 1); queued(0xB0, 123, 0, 1);
    bad += check("All Notes Off honours sustain rather than hard-killing", gate_note(t, 62) && (midi_notes[0][62] & MIDI_PEDAL_NOTE));
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers releases pedal-held notes", !gate_note(t, 62) && !midi_notes[0][62]);
    queued(0x95, 36, 100, 1); queued(0xB5, 64, 127, 1); queued(0x85, 36, 0, 1);
    bad += check("drum note-offs do not accumulate pedal-held owners", !midi_notes[4][36] && !midi_owners[3]);
    queued(0x90, 67, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 67, 0, 1); queued(0xB0, 120, 0, 1);
    bad += check("All Sound Off ignores pedal and discards all track ownership", !midi_notes[0][67] && !midi_owners[0] && !t->nheld && !gate_note(t, 67));
    return bad;
}
static int ownership_test(void)
{
    int bad = 0; midi_test_reset(); song.g[G_ROUTE] = 1; track_t *t = &trk[0];
    queued(0x94, 60, 100, 1); queued(0x95, 60, 100, 2);
    bad += check("USB/TRS channels can share one sounding note with independent owners", midi_owners[0] == 2u && gate_note(t, 60));
    queued(0x84, 60, 0, 1);
    bad += check("one channel's note-off preserves another channel's hold", midi_owners[0] == 1u && gate_note(t, 60));
    queued(0x85, 60, 0, 2);
    bad += check("last channel owner releases the shared note", !midi_owners[0] && !gate_note(t, 60));
    fm1_in.notes = 1u; events_block(CTL); uint32_t local = kb_note[0];
    queued(0x94, local, 100, 1); queued(0x84, local, 0, 1);
    bad += check("MIDI release cannot cut a still-held local keyboard note", gate_note(t, local));
    fm1_in.notes = 0; events_block(CTL);
    bad += check("local release after MIDI release ends the shared note", !gate_note(t, local));
    queued(0x94, 65, 100, 1); song.sel = 1; queued(0x84, 65, 0, 1);
    bad += check("note-off follows its note-on across selected-track changes", !gate_note(&trk[0], 65) && !midi_notes[4][65]);
    song.sel = 0; queued(0x94, 67, 100, 1); queued(0xB4, 64, 127, 1); queued(0x84, 67, 0, 1);
    panic_req |= 1u; events_block(CTL);
    queued(0x90, 67, 100, 1); queued(0xB4, 64, 0, 1);
    bad += check("preset panic forgets old pedal owners so later pedal-up preserves new notes", !midi_notes[4][67] && gate_note(&trk[0], 67));
    for (uint32_t i = 0; i <= MQ; i++) midi_enqueue(0x09u | 0x90u << 8 | 70u << 16 | 100u << 24, 1);
    events_block(CTL);
    bad += check("queue overflow recovers ownership and releases stuck notes", !midi_in_overflow && mi_r == mi_w && !midi_owners[0] && !gate_note(&trk[0], 67));
    return bad;
}
static void clock_setup(uint32_t mode)
{
    midi_test_reset(); song.g[G_CLOCK] = (int16_t)mode;
    trk[0].p[P_SLEN] = 16; trk[0].p[P_SDIV] = 2;
    for (uint32_t i = 0; i < 16; i++) trk[0].step[i] = (step_t){{(uint8_t)(60 + i)}, 1, ST_NOTE, 0, 100};
    events_block(CTL);
}
static void clock_packet(uint32_t source, uint32_t status, uint32_t ms)
{
    fm1_ms = ms;
    if (source == 2u) { um_byte(status); events_block(CTL); }
    else queued(status, 0, 0, source);
}
static void clock_to(uint32_t source, uint32_t start, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) clock_packet(source, 0xF8, start + (i * 125u) / 6u);
}
static int clock_test(uint32_t source)
{
    int bad = 0; clock_setup(source);
    clock_packet(source == 1u ? 2u : 1u, 0xFA, 1);
    bad += check(source == 1 ? "USB mode ignores TRS Start" : "TRS mode ignores USB Start", !song.playing);
    clock_packet(source, 0xFA, 2);
    bad += check(source == 1 ? "USB Start begins at step zero" : "TRS Start begins at step zero", song.playing && trk[0].seq_idx == 0u);
    clock_to(source, 2, 7);
    bad += check(source == 1 ? "USB six pulses advance exactly one 16th step" : "TRS six pulses advance exactly one 16th step", trk[0].seq_idx == 1u && song.g[G_BPM] == 120);
    fm1_ms = 132; events_block(CTL); uint16_t idx = trk[0].seq_idx; uint32_t pos = trk[0].seq_pos;
    clock_packet(source, 0xFC, 133);
    bad += check("external Stop preserves step position and releases sequence notes", !song.playing && trk[0].seq_idx == idx && trk[0].seq_pos == pos && !trk[0].seq_n);
    clock_packet(source, 0xFB, 150);
    bad += check("external Continue resumes the existing step position", song.playing && trk[0].seq_idx == idx && trk[0].seq_pos == pos);
    clock_packet(source, 0xF8, 151); fm1_ms = 672; events_block(CTL);
    bad += check("lost clock times out and releases transport notes", !song.playing && !trk[0].seq_n);
    clock_packet(source, 0xFA, 700); clock_to(source, 700, 7);
    bad += check("Start after timeout restarts cleanly at the master's phase", song.playing && trk[0].seq_idx == 1u);
    /* Original base values remain after MIDI Stop even while motion sounds. */
    trk[0].p[P_REV] = 23; motion_clear(&trk[0]); motion_set_event(&trk[0], 0, P_REV, 100);
    clock_packet(source, 0xFA, 1000); clock_packet(source, 0xF8, 1000);
    bad += check("external-clock playback applies recorded motion", trk[0].p[P_REV] == 100);
    clock_packet(source, 0xFC, 1010);
    bad += check("external Stop restores the musical parameter base", trk[0].p[P_REV] == 23);
    return bad;
}
static int clock_arp_and_boundaries(void)
{
    int bad = 0; clock_setup(1); trk[0].p[P_AMODE] = 1; trk[0].p[P_ARATE] = 2;
    queued(0x90, 60, 100, 1); queued(0x90, 64, 100, 1);
    clock_packet(1, 0xFA, 10); clock_to(1, 10, 1); uint32_t before = trk[0].arp_idx;
    clock_to(2, 20, 7);
    bad += check("wrong-source clocks cannot advance the ARP", trk[0].arp_idx == before);
    clock_to(1, 10, 7);
    bad += check("selected-source MIDI clock advances the ARP", trk[0].arp_idx > before);
    clock_setup(2); fm1_ms = 100; um_byte(0x90); um_byte(60); um_byte(0xFA); um_byte(100); events_block(CTL);
    bad += check("TRS realtime interleaving preserves the incomplete note message", gate_note(&trk[0], 60) && song.playing);
    /* Millisecond counter wraps naturally during external-clock operation. */
    clock_setup(1); clock_packet(1, 0xFA, 0xFFFFFFF0u); clock_packet(1, 0xF8, 0xFFFFFFF0u);
    clock_packet(1, 0xF8, 5u); fm1_ms = 10; events_block(CTL);
    bad += check("external clock timestamps survive uint32 millisecond wrap", song.playing && midi_clock.last_ms == 5u);
    clock_setup(1); transport_req = 1; events_block(CTL); fm1_ms = 501; events_block(CTL);
    bad += check("local PLAY waiting for missing external clock also times out", !song.playing);
    return bad;
}
/* ARP HOLD latched on an external clock: a Stop, a lost clock or a tap while stopped must not leave the arp note
 * sounding; Continue plays the latched chord again; the internal clock's free-running arp is unchanged by STOP */
static void arp_latch(uint32_t mode)
{
    midi_test_reset(); song.g[G_CLOCK] = (int16_t)mode; events_block(CTL);
    trk[0].p[P_AMODE] = 1; trk[0].p[P_AHOLD] = 1; trk[0].p[P_ARATE] = 2; trk[0].p[P_AGATE] = 120; trk[0].p[P_AOCT] = 2;
    events_block(CTL);
}
static uint32_t arp_ext_run(uint32_t ms)       /* Start, tap 60 (latched), clocks until the arp plays 72 */
{
    uint32_t i;
    clock_packet(1, 0xFA, ms); queued(0x90, 60, 100, 1); queued(0x80, 60, 0, 1);
    for (i = 0; i < 400 && trk[0].arp_note != 72; i++) clock_packet(1, 0xF8, ms + i * 21u);
    return ms + i * 21u;
}
static void idle_ms(uint32_t ms)               /* about ms of audio blocks, no MIDI */
{
    uint32_t i, ms0 = fm1_ms;
    for (i = 0; i < ms * 23u / 16u; i++) { fm1_ms = ms0 + i * 16u / 23u; events_block(CTL); }
}
static int arp_ext_stop_test(void)
{
    int bad = 0; uint32_t ms, i, off;
    arp_latch(1); ms = arp_ext_run(10);
    bad += check("ARP HOLD on the external clock plays the upper octave", trk[0].arp_note == 72 && gate_note(&trk[0], 72));
    clock_packet(1, 0xFC, ms + 5); idle_ms(2000);
    bad += check("external Stop ends the sounding ARP note", !trk[0].arp_note && !gate_note(&trk[0], 72) && !gate_note(&trk[0], 60));
    bad += check("external Stop keeps the latched HOLD chord", trk[0].nheld == 1u && trk[0].held[0] == 60u);
    ms = fm1_ms; clock_packet(1, 0xFB, ms);
    for (i = 1; i < 40 && !trk[0].arp_note; i++) clock_packet(1, 0xF8, ms + i * 21u);
    bad += check("Continue plays the latched chord again", trk[0].arp_note != 0);
    arp_latch(1); arp_ext_run(10); idle_ms(2000);
    bad += check("a lost external clock stops the transport and ends the ARP note", !song.playing && !trk[0].arp_note && !gate_note(&trk[0], 72));
    arp_latch(1); queued(0x90, 64, 100, 1); queued(0x80, 64, 0, 1);
    bad += check("a tap while the external transport is stopped sounds", trk[0].arp_note == 64);
    idle_ms(2000);
    bad += check("its ARP gate still ends in real time", !trk[0].arp_note && !gate_note(&trk[0], 64));
    arp_latch(0); transport_req = 1; events_block(CTL); queued(0x90, 60, 100, 1); queued(0x80, 60, 0, 1);
    for (i = 0; i < 4000 && trk[0].arp_note != 72; i++) events_block(CTL);
    off = trk[0].arp_off;
    transport_req = 2; events_block(CTL);
    bad += check("internal clock: STOP leaves the free-running ARP note to its gate", trk[0].arp_note == 72 && gate_note(&trk[0], 72) && trk[0].arp_off == off - CTL);
    for (i = 0; i < 4000 && (!trk[0].arp_note || trk[0].arp_note == 72); i++) events_block(CTL);
    bad += check("internal clock: the ARP keeps stepping after STOP", trk[0].arp_note && trk[0].arp_note != 72 && trk[0].nheld == 1u);
    return bad;
}
/* USB-MIDI back-pressure: full 64-byte bulk packets (16 events) faster than the audio ISR drains the ring are
 * left in the endpoint (NAK) instead of overflowing it, which would panic every sounding note */
static int usb_burst_test(void)
{
    int bad = 0; uint8_t pkt[64]; uint32_t e, k, taken = 0;
    midi_test_reset();
    queued(0x90, 48, 100, 1);
    for (e = 0; e < 16; e++) {                       /* CC7 = 100 on 16 channels */
        pkt[4 * e] = 0x0B; pkt[4 * e + 1] = (uint8_t)(0xB0 | e); pkt[4 * e + 2] = 7; pkt[4 * e + 3] = 100;
    }
    for (k = 0; k < 5; k++)                          /* 5 polls at 2 kHz before the next half drains */
        taken += (uint32_t)ep1_take(pkt, sizeof pkt);
    bad += check("a USB burst fills the MIDI ring without overflowing it", taken == 3u && !midi_in_overflow && mi_w - mi_r == 48u);
    bad += check("the packet that does not fit is refused (left for the next poll)", !ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 48u);
    for (e = 0; e < 8; e++)                          /* TRS MIDI meanwhile: its 8 slots are free */
        midi_in_event(0x0Bu | 0xB1u << 8 | 1u << 16 | e << 24);
    bad += check("TRS MIDI into a ring full of USB still fits (no overflow)", !midi_in_overflow && mi_w - mi_r == 56u);
    events_block(CTL);
    bad += check("the ring drains with no panic: the held note keeps sounding", gate_note(&trk[0], 48) && mi_r == mi_w);
    bad += check("the refused packet is taken once the ring has room", ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 16u);
    events_block(CTL);
    bad += check("the held note survives all 88 events", gate_note(&trk[0], 48) && !midi_in_overflow);
    for (k = 0; k < 3; k++) ep1_take(pkt, sizeof pkt);
    midi_in_event(0x09u | 0x90u << 8 | 50u << 16 | 100u << 24);   /* one more event: 49 used, 15 free */
    bad += check("free space below one packet refuses a whole packet", !ep1_take(pkt, sizeof pkt) && !midi_in_overflow);
    events_block(CTL);
    return bad;
}
/* GLO > SYSTEM ROUT (#68): MULTI listens to channels 1..5 — 1 the pretend keyboard (the selected track, its
 * OCT / TRN / QNT key map, its P13 split and chord), 2..5 parts 1..4 — and channels 6..16 are free for other
 * instruments (notes, bend, CCs, aftertouch, panic and reset all ignored, from USB and TRS). SEL plays the
 * selected track from every channel; a switch to MULTI lets go of what channels 2..16 held (2..5 played the
 * selected track under SEL, now their own parts) and keeps only channel 1, the same track either way. */
static int any_gate(void)
{
    for (uint32_t i = 0; i < NTRK * NVOICE; i++) if (trk[i / NVOICE].v[i % NVOICE].active && trk[i / NVOICE].v[i % NVOICE].gate) return 1;
    return 0;
}
static int route_test(void)
{
    int bad = 0; uint32_t ch, src, owned = 0;
    midi_test_reset();
    bad += check("ROUT defaults to MULTI", song.g[G_ROUTE] == 0);
    for (src = 1; src <= 2u; src++)
        for (ch = 5; ch < 16u; ch++) queued(0x90 | ch, 60 + ch, 100, src);
    for (ch = 5; ch < 16u; ch++) owned |= midi_notes[ch][60 + ch];
    bad += check("MULTI: note-ons on channels 6..16 (USB and TRS) play nothing", !any_gate() && !owned && !midi_owners[2] && !midi_hint);
    queued(0x91, 61, 100, 1); queued(0x92, 62, 100, 2); queued(0x93, 63, 100, 1); queued(0x94, 64, 100, 2);
    bad += check("MULTI: channels 2..5 play parts 1..4 (2 -> trk0, 5 -> trk3)",
                 gate_note(&trk[0], 61) && gate_note(&trk[1], 62) && gate_note(&trk[2], 63) && gate_note(&trk[3], 64) &&
                 midi_notes[1][61] == 1u && midi_notes[4][64] == 4u);
    queued(0x85, 127, 127, 1); queued(0x8F, 0, 0, 2); queued(0x89, 127, 127, 1);
    bad += check("MULTI: pitch bend on channels 6, 10, 16 bends no part", !midi_bend_target[0] && !midi_bend_target[1] && !midi_bend_target[2] && !midi_ch[5].bend && !midi_ch[15].bend);
    queued(0xB5, 1, 99, 1); queued(0xB9, 11, 10, 2); queued(0xDF, 77, 0, 1);
    bad += check("MULTI: CC1 / CC11 and aftertouch on channels 6..16 reach no MOD source", !trk[2].mw && !trk[2].ex_off && !trk[2].at && !trk[0].mw && !trk[0].at);
    queued(0xB5, 101, 0, 1); queued(0xB5, 100, 0, 1); queued(0xB5, 6, 24, 1);
    bad += check("MULTI: RPN on channel 6 changes nothing", midi_ch[5].semis != 24u);
    queued(0xB5, 64, 127, 1); queued(0x82, 62, 0, 2);
    bad += check("MULTI: a channel 6 sustain pedal does not hold channel 3 (part 2)'s note", !gate_note(&trk[1], 62) && !midi_notes[2][62]);
    queued(0x92, 62, 100, 2); queued(0xB2, 1, 50, 1);
    queued(0xB5, 120, 0, 1); queued(0xB9, 123, 0, 2); queued(0xBF, 121, 0, 1);
    bad += check("MULTI: CC120 / CC123 / CC121 on channels 6..16 leave parts 1..4 sounding", gate_note(&trk[1], 62) && gate_note(&trk[3], 64) && midi_owners[1] == 1u && trk[1].mw == 50);
    queued(0xB2, 123, 0, 2); queued(0x84, 64, 0, 2);
    bad += check("MULTI: panic on channel 3 (part 2) still works", !gate_note(&trk[1], 62) && gate_note(&trk[0], 61) && gate_note(&trk[2], 63) && !midi_owners[1]);

    /* channel 1 is the keybed: the selected track, its key map (TRN / QNT), its split and its chord */
    midi_test_reset(); song.sel = 2; trk[2].p[P_VOICE] = V_POLY;
    queued(0x90, 60, 100, 1);
    bad += check("MULTI: channel 1 plays the selected track", gate_note(&trk[2], 60) && !gate_note(&trk[0], 60) && !midi_hint);
    queued(0xB0, 64, 127, 1); queued(0x80, 60, 0, 1);
    bad += check("MULTI: channel 1's pedal and note-off follow it", gate_note(&trk[2], 60) && midi_owners[2] == 1u);
    queued(0xB0, 64, 0, 1);
    bad += check("MULTI: channel 1's pedal release ends it", !gate_note(&trk[2], 60) && !midi_owners[2]);
    trk[2].p[P_TRANS] = 2; queued(0x90, 60, 100, 1);
    bad += check("MULTI: TRN moves channel 1 with the keys", gate_note(&trk[2], 62));
    queued(0x80, 60, 0, 1);
    bad += check("MULTI: and its note-off ends the note it became", !gate_note(&trk[2], 62) && !midi_owners[2]);
    trk[2].p[P_TRANS] = 0;
    song.octave = 1; queued(0x90, 60, 100, 1);
    bad += check("MULTI: OCT+ moves channel 1 up with the keys", gate_note(&trk[2], 72));
    queued(0x80, 60, 0, 1); song.octave = 0;
    song.sel = 1; queued(0x90, 60, 100, 1);
    bad += check("MULTI: swapping the selected track moves channel 1 with it", gate_note(&trk[1], 60) && !gate_note(&trk[2], 60));
    queued(0x80, 60, 0, 1);
    trk[1].p[P_CHRD] = CH_MAJ; queued(0x90, 60, 100, 1);
    bad += check("MULTI: channel 1 takes the selected track's chord", gate_note(&trk[1], 60) && gate_note(&trk[1], 64) && gate_note(&trk[1], 67));
    queued(0x80, 60, 0, 1); trk[1].p[P_CHRD] = 0;
    split_set(60, 2, 3);
    queued(0x90, 72, 100, 1);
    bad += check("MULTI: channel 1 obeys the split (at/above C4: the HIGH track)", gate_note(&trk[3], 72));
    queued(0x80, 72, 0, 1); split_set(0, 0, 1);
    (void)src; (void)owned;

    midi_test_reset(); song.sel = 2; song.g[G_ROUTE] = 1; trk[2].p[P_VOICE] = V_POLY; trk[2].p[P_SUS] = 127; events_block(CTL);
    queued(0x90, 60, 100, 1); queued(0x94, 62, 100, 2); queued(0x99, 64, 100, 1); queued(0x9F, 65, 100, 2);
    bad += check("SEL: channels 1, 5, 10, 16 all play the selected track", gate_note(&trk[2], 60) && gate_note(&trk[2], 62) && gate_note(&trk[2], 64) && gate_note(&trk[2], 65) && !gate_note(&trk[0], 60) && midi_owners[2] == 4u);
    queued(0xEF, 127, 127, 1); queued(0xB9, 1, 66, 2); queued(0xD4, 33, 0, 1);
    bad += check("SEL: bend, CC1 and aftertouch from channels 5..16 reach the selected track", midi_bend_target[2] == 512 && trk[2].mw == 66 && trk[2].at == 33);
    queued(0xB4, 64, 127, 1); queued(0x84, 62, 0, 2);
    bad += check("SEL: channel 5 pedal holds its note", gate_note(&trk[2], 62) && (midi_notes[4][62] & MIDI_PEDAL_NOTE));
    song.g[G_ROUTE] = 0; events_block(CTL);
    bad += check("SEL -> MULTI releases channels 6..16's notes (pedal-held too) and 2..5's, keeping channel 1's",
                 !gate_note(&trk[2], 62) && !gate_note(&trk[2], 64) && !gate_note(&trk[2], 65) && gate_note(&trk[2], 60) &&
                 !midi_notes[4][62] && !midi_notes[9][64] && !midi_notes[15][65] && midi_owners[2] == 1u && !midi_ch[4].pedal);
    bad += check(".. and resets their bend: each part follows its own channel 2..5", !midi_bend_target[2] && !midi_ch[15].bend);
    queued(0x80, 60, 0, 1);
    bad += check("channel 1's note-off still releases its note after the switch", !any_gate() && !midi_owners[2]);
    return bad;
}
/* P13: live keys split into two zones (below the split note plays the LOW track, at/above it HI).
 * Sequencer notes are never split; MIDI and the keybed are, and a note-off always follows the
 * note. The setting rides in two settings bytes, so it survives a power cycle. */
static int split_test(void)
{
    int bad = 0;
    persist_t p;
    midi_test_reset();
    split_set(60, 2, 3);
    queued(0x90, 48, 100, 1);
    bad += check("split: a MIDI note below the split plays the LOW track",
                 gate_note(&trk[2], 48) && !gate_note(&trk[0], 48));
    queued(0x80, 48, 0, 1);
    bad += check("split: its note-off ends it on that track", !gate_note(&trk[2], 48));
    queued(0x90, 72, 100, 1);
    bad += check("split: a note at/above it plays the HIGH track",
                 gate_note(&trk[3], 72) && !gate_note(&trk[0], 72));
    queued(0x80, 72, 0, 1);
    bad += check("split: and its note-off too", !gate_note(&trk[3], 72));

    /* the keybed goes through the same routing */
    midi_test_reset();
    split_set(60, 2, 3);
    fm1_in.notes = 1u << 7;                          /* key 7 = C4 (60): HIGH */
    keyboard_block();
    bad += check("split: keybed C4 plays the HIGH track", gate_note(&trk[3], 60) && !gate_note(&trk[0], 60));
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("split: releasing the key ends the note there", !gate_note(&trk[3], 60));

    /* a chord splits by the key that played it, not per chord tone */
    midi_test_reset();
    split_set(60, 2, 3);
    trk[2].p[P_VOICE] = V_POLY;                      /* a chord needs poly voices */
    trk[2].p[P_CHRD] = CH_MAJ;
    queued(0x90, 56, 100, 1);
    {
        int ok = chord_last[2].n > 1u;
        for (uint32_t i = 0; ok && i < chord_last[2].n; i++)
            ok = gate_note(&trk[2], chord_last[2].note[i]) && !gate_note(&trk[0], chord_last[2].note[i]);
        bad += check("split: every chord tone follows the key's zone (LOW here)", ok);
    }
    queued(0x80, 56, 0, 1);
    trk[2].p[P_CHRD] = 0;
    midi_test_reset();

    /* off: the channel's own track again */
    split_set(0, 0, 1);
    queued(0x90, 40, 100, 1);
    bad += check("split OFF: notes play the channel's track", gate_note(&trk[0], 40) && !gate_note(&trk[1], 40));
    queued(0x80, 40, 0, 1);

    /* lo == hi is not a split */
    split_set(60, 1, 1);
    bad += check("split: one track on both sides means off", split_note == 0);
    split_set(70, 2, 3);

    /* persistence: two scratch bytes of the settings record */
    memset(&p, 0, sizeof p);
    settings_export(&p);
    bad += check("split: saved in the settings record",
                 p.favorites.factory[15][24] == 70 && p.favorites.factory[15][25] == (2u | 3u << 4));
    split_set(0, 0, 0);
    settings_import(&p, sizeof p);
    bad += check("split: restored from it", split_note == 70 && split_lo == 2 && split_hi == 3);
    p.favorites.factory[15][25] = 0xFF;
    settings_import(&p, sizeof p);
    bad += check("split: a corrupt record clamps the tracks", split_lo < NTRK && split_hi < NTRK);
    memset(&p, 0, sizeof p);
    settings_import(&p, sizeof p);
    bad += check("split: an older record (both bytes 0) is off", split_note == 0);
    midi_test_reset();
    return bad;
}

/* P13 surface: the SPLIT page's four knobs and its pad actions */
static int split_surface_test(void)
{
    int bad = 0;
    midi_test_reset();
    split_set(0, 0, 1);
    go_page(GR_SPLIT);
    bad += check("split page: it is in the page table", str_eq(cur_page()->title, "SPLIT"));
    turn(EN_K1, 1);
    bad += check("split page: K1 arms the split at C4", split_note == 60 && split_lo == 0 && split_hi == 1);
    turn(EN_K2, 2);
    bad += check("split page: K2 moves the split note", split_note == 62);
    turn(EN_K3, 1);
    bad += check("split page: K3 picks LOW and moves HIGH out of its way", split_lo == 1 && split_hi == 2);
    turn(EN_K4, 3);
    bad += check("split page: K4 picks HIGH the same way", split_hi == 1 && split_lo == 2);
    turn(EN_K1, -1);
    bad += check("split page: K1 again disarms it", split_note == 0);
    turn(EN_K2, 1);
    bad += check("split page: turning the note re-arms it", split_note == 61);

    split_set(0, 0, 1);
    split_pad_track(0u, 3u);
    bad += check("split pads: L4 picks LOW track 4", split_note == 60 && split_lo == 3 && split_hi == 1);
    split_pad_track(1u, 3u);
    bad += check("split pads: H4 moves LOW aside", split_hi == 3 && split_lo == 0);
    split_pad_note(72);
    bad += check("split pads: C5 sets the note", split_note == 72);
    split_pad_toggle();
    bad += check("split pads: the toggle disarms it", split_note == 0);
    split_pad_toggle();
    bad += check("split pads: and arms it at the default again", split_note == 60);
    split_set(0, 0, 1);
    midi_test_reset();
    return bad;
}

int main(void)
{
    int bad = controls_test() + sustain_test() + ownership_test() + clock_test(1) + clock_test(2) + clock_arp_and_boundaries() +
              arp_ext_stop_test() + usb_burst_test() + route_test() + split_test() + split_surface_test();
    printf("%s\n", bad ? "MIDI CONTROL/CLOCK TEST FAILED" : "MIDI control/clock integration tests passed"); return bad != 0;
}
