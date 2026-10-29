/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* Host test of the BLE-MIDI codec and adapter, firmware/src/ble_midi.c with SKM_BLE_MIDI=1:
 * inbound packet framing (header, 7- and 13-bit timestamp), running status inside and across packets,
 * realtime, SysEx, the malformed cases, the byte ring and its resync, and the outbound packets the
 * stack's notify would receive. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define SKM_OTA 1
#define SKM_CDC 0
#define SKM_BLE_MIDI 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb.c"
#include "../firmware/src/ble_midi.c"

static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) {}

static int check(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static uint32_t pk(const uint8_t *p, uint32_t n)      /* one GATT write, then the drain the ISR runs */
{
    uint32_t before = mi_w;
    ble_midi_rx(p, n);
    ble_midi_poll();
    return mi_w - before;
}

/* ---- outbound: the stack's notify --------------------------------------- */
static uint8_t nb[8];
static uint32_t nn, calls;
static int notify(const uint8_t *buf, uint32_t n)
{
    if (n > sizeof nb)
        return -1;
    memcpy(nb, buf, n);
    nn = n;
    calls++;
    return 0;
}

int main(void)
{
    uint32_t bad = 0, w0;

    /* ---- inbound framing ------------------------------------------------ */
    {
        static const uint8_t p[] = {0x80, 0x80, 0x10, 0x90, 60, 100};   /* 13-bit timestamp */
        uint32_t got;
        w0 = mi_w;
        got = pk(p, sizeof p);
        bad += (uint32_t)check("ble: 13-bit timestamp packet -> one USB-MIDI packet",
            got == 1u && midi_in_q[w0 % MQ] == 0x643C9009u &&
            midi_in_source[w0 % MQ] == 4u && midi_in_ms[w0 % MQ] == fm1_ms);
    }
    {
        static const uint8_t p[] = {0x80, 0x10, 0xC1, 5};                /* 7-bit timestamp, channel 2 */
        uint32_t got;
        w0 = mi_w;
        got = pk(p, sizeof p);
        bad += (uint32_t)check("ble: 7-bit timestamp packet, program change",
            got == 1u && midi_in_q[w0 % MQ] == 0x0005C10Cu && midi_in_source[w0 % MQ] == 4u);
    }
    {
        static const uint8_t p[] = {0x80, 0x00, 0x90, 60, 100, 62, 101}; /* running status in a packet */
        uint32_t got;
        w0 = mi_w;
        got = pk(p, sizeof p);
        bad += (uint32_t)check("ble: running status inside one packet",
            got == 2u && midi_in_q[w0 % MQ] == 0x643C9009u &&
            midi_in_q[(w0 + 1u) % MQ] == 0x653E9009u);
    }
    {
        static const uint8_t p[] = {0x80, 0x00, 63, 102};                /* running status across packets */
        uint32_t got;
        w0 = mi_w;
        got = pk(p, sizeof p);
        bad += (uint32_t)check("ble: running status carries across packets",
            got == 1u && midi_in_q[w0 % MQ] == 0x663F9009u);
    }
    {
        static const uint8_t p[] = {0x80, 0x00, 0xF8, 64, 103};          /* realtime keeps running status */
        uint32_t got;
        w0 = mi_w;
        got = pk(p, sizeof p);
        bad += (uint32_t)check("ble: clock inside a packet, running status kept",
            got == 2u && midi_in_q[w0 % MQ] == 0x0000F80Fu &&
            midi_in_q[(w0 + 1u) % MQ] == 0x67409009u);
    }
    {
        static const uint8_t p[] = {0x80, 0x00, 0xF0, 0x7D, 0x01, 0xF7, 0x90, 65, 104};
        uint32_t got;
        w0 = mi_w;
        got = pk(p, sizeof p);
        bad += (uint32_t)check("ble: SysEx dropped, a status after it is not",
            got == 1u && midi_in_q[w0 % MQ] == 0x68419009u);
    }
    {
        static const uint8_t sx[] = {0x80, 0x00, 0xF0, 0x7D, 0xF7};      /* SysEx, and no status after */
        static const uint8_t data[] = {0x80, 0x00, 65, 104};
        uint32_t got, w1;
        pk(sx, sizeof sx);
        w1 = mi_w;
        got = pk(data, sizeof data);
        bad += (uint32_t)check("ble: SysEx cancels running status, so bare data is dropped",
            got == 0u && mi_w == w1);
    }

    /* ---- malformed packets ---------------------------------------------- */
    {
        static const uint8_t badhdr[] = {0x00, 0x00, 0x90, 60, 100};      /* no header bit */
        static const uint8_t cutts[] = {0x80, 0x90};                      /* 13-bit timestamp, one byte */
        uint32_t d0 = bm.in_drops, n0 = bm.in_pkts, b0 = bm.in_bytes, w1 = mi_w;
        int r1 = ble_midi_rx(badhdr, sizeof badhdr);
        ble_midi_poll();
        int r2 = ble_midi_rx(cutts, sizeof cutts);
        ble_midi_poll();
        bad += (uint32_t)check("ble: a packet without the header bit, or a cut timestamp, is refused",
            r1 == -1 && r2 == -1 && bm.in_drops == d0 + 2u && bm.in_bytes == b0 &&
            bm.in_pkts == n0 && mi_w == w1);
    }
    {
        static const uint8_t hdr[] = {0x80};                              /* a header and no MIDI */
        uint32_t n0 = bm.in_pkts, w1 = mi_w;
        bad += (uint32_t)check("ble: a header-only packet is legal and empty",
            ble_midi_rx(hdr, sizeof hdr) == 0 && bm.in_pkts == n0 + 1u && mi_w == w1);
    }

    /* ---- the byte ring -------------------------------------------------- */
    {
        static uint8_t big[64];
        static const uint8_t small[] = {0x80, 0x00, 0x90, 66, 105};
        uint32_t i, k = 0, d0 = bm.in_drops;
        big[k++] = 0x80;                                                 /* header + a 7-bit timestamp */
        big[k++] = 0x00;
        for (i = 0; i < 20u; i++) {                                      /* 60 payload bytes: 20 note-ons */
            big[k++] = 0x90;
            big[k++] = (uint8_t)(36u + i);
            big[k++] = 100;
        }
        big[k++] = 0x90;                                                 /* the write ends mid-message */
        big[k++] = 36;
        /* both writes before any drain: 62 bytes fill all but 2 of the 64, so the next cannot fit */
        w0 = mi_w;
        {
            uint32_t r1 = (uint32_t)ble_midi_rx(big, sizeof big);
            uint32_t r2 = (uint32_t)ble_midi_rx(small, sizeof small);
            ble_midi_poll();
            bad += (uint32_t)check("ble: a write that does not fit the ring is dropped whole, and resyncs",
                r1 == 0u && r2 == 0xFFFFFFFFu && bm.in_drops == d0 + 1u && mi_w == w0 + 20u && bm.st == 0u);
        }
        {
            static const uint8_t data[] = {0x80, 0x00, 66, 105};         /* data only: needs a status */
            uint32_t w1 = mi_w;
            pk(data, sizeof data);
            bad += (uint32_t)check("ble: after a resync the parser waits for a status byte", mi_w == w1);
        }
    }

    /* ---- outbound -------------------------------------------------------- */
    {
        uint32_t d0 = bm.out_drops, p0 = bm.out_pkts;
        calls = 0;
        midi_out_event(0x643C9009u);                                     /* no notify yet: nothing queued */
        ble_midi_flush();
        bad += (uint32_t)check("ble: no notify -> nothing is queued, and nothing is counted",
            bm.out_pkts == p0 && bm.out_drops == d0 && calls == 0u);
    }
    ble_midi_set_notify(notify);
    {
        static const uint8_t want[] = {0x80, 0xC5, 0x06, 0x90, 0x3C, 0x64};
        fm1_ms = 0x2345u;                                                /* 13-bit: 0x0345 */
        calls = 0;
        midi_out_event(0x643C9009u);
        ble_midi_flush();
        bad += (uint32_t)check("ble: note on -> header, 13-bit timestamp, status, two data bytes",
            calls == 1u && nn == sizeof want && !memcmp(nb, want, sizeof want));
    }
    {
        static const uint8_t want[] = {0x80, 0xC5, 0x06, 0xF8};
        calls = 0;
        midi_out_event(0x0000F80Fu);                                     /* realtime: the status alone */
        ble_midi_flush();
        bad += (uint32_t)check("ble: clock -> a four-byte packet",
            calls == 1u && nn == sizeof want && !memcmp(nb, want, sizeof want));
    }
    {
        calls = 0;
        midi_out_event(0x0001F20Fu);                                     /* system common: never sent */
        ble_midi_flush();
        bad += (uint32_t)check("ble: system common is not put on the air", calls == 0u);
    }
    {
        uint32_t d0 = bm.out_drops, p0 = bm.out_pkts;
        calls = 0;
        midi_out_event(0x641E9009u);
        midi_out_event(0x653E9009u);
        ble_midi_flush();
        bad += (uint32_t)check("ble: two queued messages go out in order",
            calls == 2u && bm.out_pkts == p0 + 2u && bm.out_drops == d0);
    }
    ble_midi_set_notify(0);
    {
        uint32_t p0 = bm.out_pkts;
        calls = 0;
        midi_out_event(0x641E9009u);
        ble_midi_flush();
        bad += (uint32_t)check("ble: a disconnect stops the queue", calls == 0u && bm.out_pkts == p0);
    }

    printf("%s\n", bad ? "BLE-MIDI TEST FAILED" : "ble-midi test passed");
    return (int)bad;
}
