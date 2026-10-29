/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* BLE-MIDI: the packet codec (Apple BLE-MIDI 1.0) and the adapter between the radio and the MIDI queue.
 *
 * SKM_BLE_MIDI is 0 by default and then this whole file compiles away: an image that links no radio
 * stack must not pay a byte for one. It is 1 only in a build that links btstack.a + btctrler.a.
 *
 * Neither direction touches the queue from the radio's task, because midi_enqueue() (usb.c) has one
 * writer and that writer is the TIMER5 ISR (usb_poll, uart_midi_poll, smk_panel):
 *   in   the GATT write callback (the stack's task) -> ble_midi_rx() -> a 64-byte ring -> the TIMER5
 *        ISR drains it in ble_midi_poll() and enqueues, exactly as midi_uart.c does with its DMA ring.
 *   out  midi_out_event() (the audio ISR) queues a packet; ble_midi_flush() (the stack's task) turns
 *        it into a BLE packet and notifies. A GATT notify from the audio ISR would stall the render.
 * The one byte the two directions share is bm_notify, written by the task on connect / disconnect and
 * read by the audio ISR: a pointer store is atomic here, and the ISR treats NULL as "no radio".
 *
 * Inbound packets: a header byte (MSB set, bits 6..0 reserved = 0), then the timestamp (one 7-bit byte,
 * or two when the first has its MSB set, giving 13 bits), then one or more MIDI messages that may use
 * running status. The timestamp is decoded to prove the packet is well formed and then dropped: the
 * queue stamps every message with fm1_ms on arrival (usb.c midi_enqueue), and a 13-bit millisecond
 * counter wraps every 8.2 s, which is not a number to hand the clock path. Running status is carried
 * across packets: the spec allows it, and a host that restarts the status byte in every packet is
 * unaffected by the carry.
 *
 * SysEx is dropped, the same rule the TRS input takes (midi_uart.c): a firmware update reachable from
 * a pairing dialog is not a trade worth making. A SysEx that opens cancels running status, so the
 * stream resynchronises on the next status byte.
 *
 * Deliberately not done here: 'BLE' is not a clock source. song.g[G_CLOCK] indexes params.c N_CLOCK
 * {"INT","USB","TRS"} and this file tags its packets source 4, so clock messages from a phone arrive
 * and are not used for tempo. Notes and CC are. Adding "BLE" to N_CLOCK is the follow-up, and it needs
 * the companion's own copy of that list in the same commit.
 */

#ifndef SKM_BLE_MIDI
#define SKM_BLE_MIDI 0
#endif

#if SKM_BLE_MIDI

#define SKM_BLE_MIDI_LINKED 1      /* main.c drains the ring only where this file is in the TU (not the host) */

#define BM_SRC  4u                 /* midi_in_source (usb.c): 1 USB, 2 TRS, 3 the SMK panel, 4 BLE */
#define BM_RING 64u                /* bytes in: about three 20-byte GATT writes of headroom */
#define BM_TXQ  16u                /* packets out: four audio blocks of headroom */
#define BM_TXO  6u                 /* header, a 13-bit timestamp, a 3-byte message = the longest packet */

static volatile uint8_t bm_ring[BM_RING];
static uint32_t bm_txq[BM_TXQ];
static int (*volatile bm_notify)(const uint8_t *, uint32_t);   /* the stack's notify; NULL until it exists */
static volatile uint32_t bm_w, bm_r;                          /* byte ring: the task gives, the ISR takes */
static volatile uint32_t bm_tw, bm_tr;                        /* packet ring: the ISR gives, the task takes */
static struct {
    uint32_t in_pkts, in_bytes, in_msgs, in_drops;            /* inbound, read by the BT page / console */
    uint32_t out_pkts, out_drops;
    uint8_t st, need, got, d0, sysex;                         /* the drain's running-status parser */
    uint8_t resync;                                           /* a packet did not fit: the stream is holed */
} bm;   /* the counters are diagnostics: the task and the ISR both bump them, so one may be lost */

static uint32_t bm_dlen(uint32_t s)                /* data bytes of a channel status */
{
    return (s & 0xE0u) == 0xC0u ? 1u : 2u;         /* Cx Dx: 1, else 2 */
}

/* One MIDI byte of the payload, into the same USB-MIDI packet shape the other inputs produce
 * (cable 0, CIN = status >> 4), so seq.c routes a phone's channels exactly as a cable's. */
static void bm_byte(uint32_t b)
{
    if (b >= 0xF8u) {
        if (b == 0xF8u || b == 0xFAu || b == 0xFBu || b == 0xFCu) {
            if (midi_enqueue(0xFu | b << 8, BM_SRC))
                bm.in_msgs++;
            else {
                midi_in_overflow = 1;
                bm.in_drops++;
            }
        }
        return;                                    /* realtime keeps running status */
    }
    if (b & 0x80u) {
        bm.sysex = b == 0xF0u;
        bm.st = b < 0xF0u ? (uint8_t)b : 0;        /* system common / SysEx cancel running status */
        bm.need = (uint8_t)bm_dlen(b);
        bm.got = 0;
        return;
    }
    if (bm.sysex || !bm.st)
        return;
    if (bm.need == 2u && !bm.got) {
        bm.d0 = (uint8_t)b;
        bm.got = 1;
        return;
    }
    {
        uint32_t d1 = bm.need == 2u ? bm.d0 : b, d2 = bm.need == 2u ? b : 0u;
        uint32_t pkt = (bm.st >> 4) | (uint32_t)bm.st << 8 | d1 << 16 | d2 << 24;
        bm.got = 0;
        if (midi_enqueue(pkt, BM_SRC))
            bm.in_msgs++;
        else {
            midi_in_overflow = 1;
            bm.in_drops++;
        }
    }
}

/* The radio's task: one GATT write, header and timestamp stripped, payload into the ring.
 * 0 taken, -1 not a BLE-MIDI packet (or no room). in_pkts counts what was taken. */
static int __attribute__((unused)) ble_midi_rx(const uint8_t *buf, uint32_t n)
{
    uint32_t i, w;
    if (!n || !(buf[0] & 0x80u)) {                 /* the header: MSB set, 6..0 reserved = 0 */
        bm.in_drops++;
        return -1;
    }
    if (n == 1u) {
        bm.in_pkts++;
        return 0;                                  /* a header and nothing else: legal, no MIDI in it */
    }
    i = (buf[1] & 0x80u) ? 3u : 2u;                /* 13-bit timestamp, or 7 */
    if (i > n) {
        bm.in_drops++;
        return -1;
    }
    w = bm_w;
    if (n - i > BM_RING - (w - bm_r)) {            /* no room: drop the packet whole, never a part of it */
        bm.in_drops++;
        bm.resync = 1;
        return -1;
    }
    while (i < n)
        bm_ring[w++ & (BM_RING - 1u)] = buf[i++];
    RING_PUBLISH();
    bm_w = w;
    bm.in_pkts++;
    return 0;
}

/* The TIMER5 ISR, next to uart_midi_poll (main.c). */
static void ble_midi_poll(void)
{
    while (bm_r != bm_w) {
        uint8_t b = bm_ring[bm_r & (BM_RING - 1u)];
        RING_PUBLISH();
        bm_r++;
        bm.in_bytes++;
        bm_byte(b);
    }
    if (bm.resync) {                               /* what arrived after the hole starts clean */
        bm.resync = 0;
        bm.st = bm.need = bm.got = bm.d0 = bm.sysex = 0;
    }
}

static uint32_t bm_tlen(uint32_t st)               /* the packet's length with header and timestamp */
{
    if (st >= 0xF8u)
        return 4u;                                 /* realtime: the status alone */
    if (st >= 0xF0u)
        return 0u;                                 /* system common / SysEx: never sent from here */
    return (st & 0xE0u) == 0xC0u ? 5u : 6u;
}

static void bm_send(uint32_t pkt)                  /* the radio's task: build one packet and notify it */
{
    uint8_t out[BM_TXO];
    uint32_t st = (pkt >> 8) & 0xFFu, n = bm_tlen(st), ts = fm1_ms & 0x1FFFu;
    if (!n || !bm_notify) {
        bm.out_drops += n != 0u;                   /* worth counting unless it is a system common we never send */
        return;
    }
    out[0] = 0x80u;                                /* header */
    out[1] = (uint8_t)(0x80u | (ts & 0x7Fu));      /* 13-bit timestamp: the low 7 bits, and the MSB ... */
    out[2] = (uint8_t)((ts >> 7) & 0x7Fu);         /* ... says a second byte follows with the top 6 */
    out[3] = (uint8_t)st;
    if (n > 4u)
        out[4] = (uint8_t)((pkt >> 16) & 0xFFu);
    if (n > 5u)
        out[5] = (uint8_t)((pkt >> 24) & 0xFFu);
    if (bm_notify(out, n)) {
        bm.out_drops++;
        return;
    }
    bm.out_pkts++;
}

/* The audio ISR (usb.c midi_out_event): queue only. */
static void ble_midi_tx(uint32_t pkt)
{
    if (!bm_notify)
        return;                                    /* no radio: do not queue what nobody will send */
    if (bm_tw - bm_tr >= BM_TXQ) {
        bm.out_drops++;
        return;
    }
    bm_txq[bm_tw % BM_TXQ] = pkt;
    RING_PUBLISH();
    bm_tw++;
}

/* The radio's task, on its own tick: what ble_midi_tx queued, out to the GATT notify. With no radio
 * the queue is drained and dropped, so a reconnect never sends a stale note. */
static void __attribute__((unused)) ble_midi_flush(void)
{
    while (bm_tr != bm_tw) {
        uint32_t pkt = bm_txq[bm_tr % BM_TXQ];
        RING_PUBLISH();
        bm_tr++;
        if (bm_notify)
            bm_send(pkt);
    }
}

/* The stack adapter calls this once its GATT notify works, and again with NULL on disconnect.
 * The contract: 0 notified, -1 not (no link, no buffer), and it must not block. */
static void __attribute__((unused)) ble_midi_set_notify(int (*fn)(const uint8_t *, uint32_t))
{
    bm_notify = fn;
}

#endif  /* SKM_BLE_MIDI */
