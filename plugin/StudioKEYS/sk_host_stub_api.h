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
void sk_host_enc_phys(uint32_t idx0_7, int32_t steps);
void sk_host_master(uint32_t v0_1023);
const char *sk_host_knob_label(uint32_t slot0_7);
int sk_host_knob_finite(uint32_t slot0_7);
int sk_host_knob_norm(uint32_t slot0_7, float *out01);
const char *sk_host_knob_value(uint32_t slot0_7);
const char *sk_host_fader_label(uint32_t visual0_3);
const char *sk_host_fader_value(uint32_t visual0_3);
void sk_host_smk_extra(uint32_t down_mask);
void sk_host_smk_fader(uint32_t idx0_7, int16_t adc0_1023);
void sk_host_set_param(uint32_t id, int32_t v);
int32_t sk_host_get_param(uint32_t id);
void sk_host_set_gparam(uint32_t id, int32_t v);
int32_t sk_host_get_gparam(uint32_t id);
void sk_host_set_palette(uint32_t idx);
uint32_t sk_host_palette(void);
void sk_host_set_vst_connected(int on);
int sk_host_vst_connected(void);
void sk_host_set_device_present(int on);
int sk_host_device_present(void);
uint32_t sk_host_npalettes(void);
uint32_t sk_host_menu_count(void);
uint32_t sk_host_menu_values(void);
uint32_t sk_host_menu_tab_count(void);
uint32_t sk_host_menu_tab_of(uint32_t row);
const char *sk_host_menu_tab_name(uint32_t t);
const char *sk_host_menu_row_label(uint32_t row);
uint32_t sk_host_menu_n(uint32_t row);
uint32_t sk_host_menu_get(uint32_t row);
const char *sk_host_menu_vname(uint32_t row, uint32_t v);
void sk_host_menu_put(uint32_t row, uint32_t v);
int sk_host_uboot_hw_take(void);
void sk_host_menu_uboot(void);
void sk_host_about_open(void);
void sk_host_about_close(void);
int sk_host_about_is_open(void);
uint32_t sk_host_about_line_count(void);
const char *sk_host_about_line(uint32_t i, uint8_t *kind_out);
void sk_host_palette_rgb565(uint16_t out5[5]);
void sk_host_set_lang(int id);
int sk_host_lang(void);
void sk_host_set_unleashed(int on);
int sk_host_unleashed(void);
uint32_t sk_host_voice_budget(void);
uint32_t sk_host_voice_ceiling(void);
void sk_host_waveform_peek(float *dst, uint32_t n);
const char *sk_host_version(void);
uint32_t sk_host_sample_rate(void);
uint32_t sk_host_now_ms(void);
uint32_t sk_host_playing(void);

typedef struct {
  uint8_t page, home, menu, menu_sel, sel, engine, desk;
  char page_title[16];
  char engine_name[12];
} sk_host_ui_t;
void sk_host_ui_get(sk_host_ui_t *out);
void sk_host_ui_pump(void);
void sk_host_set_desk(uint32_t desk_or_settings);
const char *sk_host_edit_label(uint32_t desk, uint32_t k0_7);
const char *sk_host_engine_name(uint32_t desk);
uint32_t sk_host_page_count(void);
const char *sk_host_page_title(uint32_t page);
int sk_host_goto_page(uint32_t page);
uint32_t sk_host_page_params(uint32_t page, uint32_t out_ids[4]);
const char *sk_host_page_param_label(uint32_t page, uint32_t slot0_3);
const char *sk_host_param_label(uint32_t id, int global);

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
int sk_host_lock_plugin_clock(void);
int sk_host_snap_save(void);
int sk_host_snap_load(void);
uint32_t sk_host_snap_bytes(uint8_t *dst, uint32_t cap);
uint32_t sk_host_link_pop(uint8_t *dst, uint32_t cap);
void sk_host_link_push(const uint8_t *src, uint32_t n);
uint32_t sk_host_surf_hits(void);

#define SK_HOST_FM6_VOICE 155u
int sk_host_fm6_get(uint8_t *dst, uint32_t n);
uint8_t sk_host_fm6_get_byte(uint32_t off);
void sk_host_fm6_set_byte(uint32_t off, uint8_t v);
void sk_host_fm6_init_voice(void);

#define SK_HOST_NTRK 4u
void sk_host_select_track(uint32_t tr0_3);
uint32_t sk_host_selected_track(void);
int32_t sk_host_trk_param(uint32_t tr0_3, uint32_t id);
void sk_host_set_trk_param(uint32_t tr0_3, uint32_t id, int32_t v);
const char *sk_host_trk_name(uint32_t tr0_3);
uint32_t sk_host_trk_engine(uint32_t tr0_3);
const char *sk_host_trk_engine_name(uint32_t tr0_3);

#ifdef __cplusplus
}
#endif
