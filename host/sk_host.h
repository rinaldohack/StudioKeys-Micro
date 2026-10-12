/* SPDX-License-Identifier: GPL-3.0-only
 * StudioKEYS Micro native host — the firmware C compiled for the DAW process.
 * Plugin talks only through this header (queue + framebuffer). See VST_IMPLEMENTATION_PLAN.md. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SK_HOST_FB_W 240u
#define SK_HOST_FB_H 240u
#define SK_HOST_NOR_SIZE 0x100000u
#define SK_HOST_FS 44100u

/* 0 = no session, 1 = sync prompt open, 2 = locked */
enum { SK_LOCK_NONE = 0, SK_LOCK_ASK = 1, SK_LOCK_ON = 2 };

/* SMK side-key bits (ui_input.c SMK_X_*). */
enum {
  SK_X_LEFT = 0,
  SK_X_RIGHT = 1,   /* hold ~500 ms → Ableton Live */
  SK_X_FADER = 2,   /* hold ~500 ms → Mackie HUI */
  SK_X_CHORD = 3,
  SK_X_REPEAT = 4,
  SK_X_BT = 5
};

void sk_host_init(void);
void sk_host_boot(void);

/* Render n frames at 44.1 kHz into L/R (host resamples to the DAW rate). */
void sk_host_render_f32(float *L, float *R, uint32_t n);

const uint16_t *sk_host_screen(void);
uint32_t sk_host_screen_draws(void);

uint8_t *sk_host_nor(void);
uint32_t sk_host_nor_size(void);
void sk_host_nor_erase(void);
uint32_t sk_host_flash_writes(void);

/* DAW MIDI on performance cable (same queue as USB MIDI on device). */
int sk_host_midi(uint8_t status, uint8_t d1, uint8_t d2);

/* Panel injection (audio thread drains; safe to call from message thread into a queue). */
void sk_host_buttons(uint32_t labels);
void sk_host_keys_mask(uint32_t mask27);
void sk_host_enc(uint32_t role, int32_t steps);
void sk_host_master(uint32_t v0_1023);

/* SMK extras: side keys (Live/HUI holds) and fader ADCs 0..1023. */
void sk_host_smk_extra(uint32_t down_mask);
void sk_host_smk_fader(uint32_t idx0_7, int16_t adc0_1023);

/* Selected-track parameters (match firmware core.h ids). */
void sk_host_set_param(uint32_t id, int32_t v);
int32_t sk_host_get_param(uint32_t id);
void sk_host_set_palette(uint32_t idx);
uint32_t sk_host_palette(void);
uint32_t sk_host_npalettes(void);
/* out[0..4] = BG SURF TEXT THEME ACCENT as RGB565 (active palette). */
void sk_host_palette_rgb565(uint16_t out5[5]);

/* Power: 0 Limited (8 voices), 1 Unleashed (full host ceiling; callback may pull down). */
void sk_host_set_unleashed(int on);
int sk_host_unleashed(void);
uint32_t sk_host_voice_budget(void);   /* Limited=8; Unleashed=working cap (up to ceiling) */
uint32_t sk_host_voice_ceiling(void);  /* compile-time host array size (NVOICE) */

/* Last render window, peak-normalized, for SAMPLE desk / meters (n ≤ 256). */
void sk_host_waveform_peek(float *dst, uint32_t n);

const char *sk_host_version(void);
uint32_t sk_host_sample_rate(void);
uint32_t sk_host_now_ms(void);
uint32_t sk_host_playing(void);

/* Locked session (link port / cable 3 SKL SysEx). One keyboard ↔ one instance.
 * Frame bytes: sk_skl.h and host/README.md. */
int sk_host_lock_get(void);
void sk_host_lock_ask(void);
void sk_host_lock_set_peer_version(const char *ver); /* empty/null = same build; mismatch rejects */
uint32_t sk_host_lock_peer_crc(void);                /* last CRC from peer (0 if none) */
const char *sk_host_lock_reject_reason(void);
void sk_host_lock_choose(int plugin_to_hw);
void sk_host_lock_dismiss(void);
void sk_host_lock_drop(void);
uint32_t sk_host_state_crc(void);
int sk_host_lock_claim_instance(void);   /* 0 = another instance already owns a lock seat */
void sk_host_lock_release_instance(void);
void sk_host_lock_autosave(void);        /* keep NOR marked while locked (continuous save) */

/* Link cable 3: push = plugin→host (parse SKL); pop = host→plugin outbox. */
uint32_t sk_host_link_pop(uint8_t *dst, uint32_t cap);
void sk_host_link_push(const uint8_t *src, uint32_t n);

#ifdef __cplusplus
}
#endif
