/* Thin copy of sk_host.h for stub builds when ../../host is absent. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SK_HOST_FB_W 240u
#define SK_HOST_FB_H 240u
#define SK_HOST_NOR_SIZE 0x100000u
#define SK_HOST_FS 44100u

enum { SK_LOCK_NONE = 0, SK_LOCK_ASK = 1, SK_LOCK_ON = 2 };
enum {
  SK_X_LEFT = 0, SK_X_RIGHT = 1, SK_X_FADER = 2,
  SK_X_CHORD = 3, SK_X_REPEAT = 4, SK_X_BT = 5
};

void sk_host_init(void);
void sk_host_boot(void);
void sk_host_render_f32(float *L, float *R, uint32_t n);
const uint16_t *sk_host_screen(void);
uint32_t sk_host_screen_draws(void);
uint8_t *sk_host_nor(void);
uint32_t sk_host_nor_size(void);
void sk_host_nor_erase(void);
uint32_t sk_host_flash_writes(void);
int sk_host_midi(uint8_t status, uint8_t d1, uint8_t d2);
void sk_host_buttons(uint32_t labels);
void sk_host_keys_mask(uint32_t mask27);
void sk_host_enc(uint32_t role, int32_t steps);
void sk_host_master(uint32_t v0_1023);
void sk_host_smk_extra(uint32_t down_mask);
void sk_host_smk_fader(uint32_t idx0_7, int16_t adc0_1023);
void sk_host_set_param(uint32_t id, int32_t v);
int32_t sk_host_get_param(uint32_t id);
void sk_host_set_palette(uint32_t idx);
uint32_t sk_host_palette(void);
uint32_t sk_host_npalettes(void);
void sk_host_palette_rgb565(uint16_t out5[5]);
void sk_host_set_unleashed(int on);
int sk_host_unleashed(void);
uint32_t sk_host_voice_budget(void);
uint32_t sk_host_voice_ceiling(void);
void sk_host_waveform_peek(float *dst, uint32_t n);
const char *sk_host_version(void);
uint32_t sk_host_sample_rate(void);
uint32_t sk_host_now_ms(void);
uint32_t sk_host_playing(void);
int sk_host_lock_get(void);
void sk_host_lock_ask(void);
void sk_host_lock_set_peer_version(const char *ver);
uint32_t sk_host_lock_peer_crc(void);
const char *sk_host_lock_reject_reason(void);
void sk_host_lock_choose(int plugin_to_hw);
void sk_host_lock_dismiss(void);
void sk_host_lock_drop(void);
uint32_t sk_host_state_crc(void);
int sk_host_lock_claim_instance(void);
void sk_host_lock_release_instance(void);
void sk_host_lock_autosave(void);
uint32_t sk_host_link_pop(uint8_t *dst, uint32_t cap);
void sk_host_link_push(const uint8_t *src, uint32_t n);

#ifdef __cplusplus
}
#endif
