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
/* Mirror the NOR image to a user-data file so settings survive a process exit. */
void sk_host_state_save(void);

/* DAW MIDI on performance cable (same queue as USB MIDI on device). */
int sk_host_midi(uint8_t status, uint8_t d1, uint8_t d2);

/* Panel injection (audio thread drains; safe to call from message thread into a queue). */
void sk_host_buttons(uint32_t labels);
void sk_host_keys_mask(uint32_t mask27);
void sk_host_enc(uint32_t role, int32_t steps);
/* Physical SMK encoder 0..7 (endless detents). Prefer this over role map in the plugin. */
void sk_host_enc_phys(uint32_t idx0_7, int32_t steps);
void sk_host_master(uint32_t v0_1023);
/* VST scribble: what knob/fader currently drives. finite=1 → absolute pointer OK. */
const char *sk_host_knob_label(uint32_t slot0_7);
int sk_host_knob_finite(uint32_t slot0_7);
int sk_host_knob_norm(uint32_t slot0_7, float *out01); /* 0 = no absolute value */
const char *sk_host_knob_value(uint32_t slot0_7);      /* secondary scribble under dial */
const char *sk_host_fader_label(uint32_t visual0_3);   /* L→R order 2,3,4,1 */
const char *sk_host_fader_value(uint32_t visual0_3);   /* secondary scribble under fader */
/* LCD header badge: 0 = "VST", 1 = "CONNECTED" (locked+synced). */
int sk_host_vst_connected(void);
/* 1 when a physical keyboard MIDI/USB port is open (UBOOT may target hardware). */
void sk_host_set_device_present(int on);
int sk_host_device_present(void);

/* SMK extras: side keys (Live/HUI holds) and fader ADCs 0..1023. */
void sk_host_smk_extra(uint32_t down_mask);
void sk_host_smk_fader(uint32_t idx0_7, int16_t adc0_1023);

/* Selected-track parameters (match firmware core.h ids). */
void sk_host_set_param(uint32_t id, int32_t v);
int32_t sk_host_get_param(uint32_t id);
/* Global song parameters (G_* ids). */
void sk_host_set_gparam(uint32_t id, int32_t v);
int32_t sk_host_get_gparam(uint32_t id);
void sk_host_set_palette(uint32_t idx);
uint32_t sk_host_palette(void);
/* LCD header badge: 0 = "VST", 1 = "CONNECTED" (locked+synced). No-op on hardware builds. */
void sk_host_set_vst_connected(int on);
uint32_t sk_host_npalettes(void);
/* MENU rows (menu_items.c) — SETTINGS desk. MI_VALUES.. are actions (ABOUT / UBOOT). */
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
/* Persist settings (palette/panel/prefs) to NOR, as the device does on MENU close.
 * May run on the UI thread; writes the settings object only (no audio lock). */
void sk_host_settings_save(void);
/* 1 once when MENU>UBOOT ran while CONNECTED — plugin should send F0 22 24 35 7D F7. */
int sk_host_uboot_hw_take(void);
void sk_host_menu_uboot(void); /* enter roast / device-UBOOT notice (same as MENU > UPDATE OPEN) */
void sk_host_about_open(void);
void sk_host_about_close(void);
int sk_host_about_is_open(void);
/* Same ABOUT / CREDITS document as the OLED (ui_menu.c menu_document).
 * kind: 0=text 1=title 2=url 3=blank */
uint32_t sk_host_about_line_count(void);
const char *sk_host_about_line(uint32_t i, uint8_t *kind_out);
/* out[0..4] = BG SURF TEXT THEME ACCENT as RGB565 (active palette). */
void sk_host_palette_rgb565(uint16_t out5[5]);

/* MENU > LANG: 0 = EN, 1 = ID (ui_prefs PREF_LANG bit 128). Labels only. */
void sk_host_set_lang(int id);
int sk_host_lang(void);

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

/* Firmware UI peek — desk follows page/engine (plan phase 4). */
typedef struct {
  uint8_t page;          /* PAGES[] index */
  uint8_t home;
  uint8_t menu;
  uint8_t menu_sel;      /* menu_items.c row (MI_*) while menu open */
  uint8_t sel;           /* selected track */
  uint8_t engine;        /* eng_req of selected track */
  uint8_t desk;          /* ENGINE_ORDER rank, or 0xFF = SETTINGS (menu/home) */
  char page_title[16];
  char engine_name[12];
} sk_host_ui_t;
void sk_host_ui_get(sk_host_ui_t *out);
/* Drain button/encoder edges through ui_input (VST UI thread → host). */
void sk_host_ui_pump(void);
/* Push desk selection into firmware (0..12 engines, 13 = open SETTINGS/menu). */
void sk_host_set_desk(uint32_t desk_or_settings);
/* Live edit-knob label from firmware ENGINES[eng_vis(desk)]->edit[k].label ("" if none). */
const char *sk_host_edit_label(uint32_t desk, uint32_t k0_7);
const char *sk_host_engine_name(uint32_t desk);

/* Firmware page table (params.c PAGES[]). */
uint32_t sk_host_page_count(void);
const char *sk_host_page_title(uint32_t page);
int sk_host_goto_page(uint32_t page); /* 0 ok */
/* Fill up to 4 track/global param ids for page; returns count (skips 0xFF). */
uint32_t sk_host_page_params(uint32_t page, uint32_t out_ids[4]);
/* Firmware label for page slot 0..3 ("" if empty / step / mixer). */
const char *sk_host_page_param_label(uint32_t page, uint32_t slot0_3);
/* Track/global label by id (scope: 0=track TP, 1=global GP). */
const char *sk_host_param_label(uint32_t id, int global);

struct sk_host_param_info {
    int min;
    int max;
    int def;
    int fmt;
    const char *unit;
    const char *const *names;
};
int sk_host_get_param_info(uint32_t id, struct sk_host_param_info *out);
int sk_host_format_param(uint32_t id, int32_t val, char *buf, uint32_t cap);
uint32_t sk_host_pe0_id(void);

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
void sk_host_lock_autosave(void);        /* project_save to NOR while locked (continuous save) */
/* 1 while locked: plugin owns musical time; keyboard seq must not free-run. */
int sk_host_lock_plugin_clock(void);

/* Snapshot helpers (project blob ↔ NOR slot 0). */
int sk_host_snap_save(void);             /* 0 ok */
int sk_host_snap_load(void);             /* 0 ok */
uint32_t sk_host_snap_bytes(uint8_t *dst, uint32_t cap); /* packed project_store size */

/* Link cable 3: push = plugin→host (parse SKL/SKS); pop = host→plugin outbox. */
uint32_t sk_host_link_pop(uint8_t *dst, uint32_t cap);
void sk_host_link_push(const uint8_t *src, uint32_t n);
/* Host/web: SKS frames handled by sk_surf_link_sysex stub; hit count for tests. */
uint32_t sk_host_surf_hits(void);

/* FM6 voice on the selected track (155-byte layout, operator 0 = OP6). */
#define SK_HOST_FM6_VOICE 155u
int sk_host_fm6_get(uint8_t *dst, uint32_t n); /* bytes written, or 0 */
uint8_t sk_host_fm6_get_byte(uint32_t off);
void sk_host_fm6_set_byte(uint32_t off, uint8_t v);
void sk_host_fm6_init_voice(void); /* load FM6_INIT onto the selected track */

/* Per-track mixer (NTRK = 4). */
#define SK_HOST_NTRK 4u
void sk_host_select_track(uint32_t tr0_3);
uint32_t sk_host_selected_track(void);
int32_t sk_host_trk_param(uint32_t tr0_3, uint32_t id);
void sk_host_set_trk_param(uint32_t tr0_3, uint32_t id, int32_t v);
const char *sk_host_trk_name(uint32_t tr0_3); /* short sound name for mixer strip */
uint32_t sk_host_trk_engine(uint32_t tr0_3);
/* Engine INDEX (0..NENGINES-1) vs desk RANK (ENGINE_ORDER position). */
uint32_t sk_host_trk_engine_rank(uint32_t tr0_3);
const char *sk_host_trk_engine_name(uint32_t tr0_3);

/* Keyboard preset bank — the same list the panel's PRESETS knob / PRESETS page
 * browsed: every engine's FACTORY presets in ENGINE_ORDER, then the USED user
 * presets in slot order. The plugin's PATCH selector builds its menu from this. */
uint32_t sk_host_preset_bank_count(void);
/* n < count: fill *engine_out (ENGINES index, or SK_HOST_ENG_USER for a user
 * preset) and *preset_out (factory index, or user slot). 0 = factory, 1 = user. */
#define SK_HOST_ENG_USER 0xFFu
int sk_host_preset_bank_at(uint32_t n, uint32_t *engine_out, uint32_t *preset_out);
/* Short sound name of bank entry n ("" if out of range). */
const char *sk_host_preset_bank_name(uint32_t n);
/* Engine display name of bank entry n ("USER" for a user preset). */
const char *sk_host_preset_bank_engine_name(uint32_t n);
/* Bank index of the selected track's current sound (for the checkmark). */
uint32_t sk_host_preset_bank_pos(void);
/* Load bank entry n into the selected track, switching its engine when needed. */
int sk_host_preset_bank_go(uint32_t n);

/* Per-engine factory presets, grouped for the selector's submenus.
 * rank is the desk rank (ENGINE_ORDER position), matching sk_host_set_desk(). */
uint32_t sk_host_engine_factory_count(uint32_t rank);      /* shown factory presets */
const char *sk_host_engine_factory_name(uint32_t rank, uint32_t i); /* i-th shown */
uint32_t sk_host_engine_factory_preset(uint32_t rank, uint32_t i);  /* i-th shown -> stored index */
/* Select the engine (desk) if needed and load its i-th shown factory preset. */
int sk_host_engine_factory_apply(uint32_t rank, uint32_t i);

/* User presets (NOR slots) in slot order. */
uint32_t sk_host_user_count(void);
int sk_host_user_used(uint32_t slot);
const char *sk_host_user_name(uint32_t slot);       /* sound name */
const char *sk_host_user_label(uint32_t slot);      /* "U07" */
const char *sk_host_user_engine_name(uint32_t slot);
int sk_host_user_apply(uint32_t slot);              /* load slot into the selected track */

/* Selected track's current sound, for menu checkmarks. */
uint32_t sk_host_sel_engine_rank(void);
int sk_host_sel_preset(void);        /* stored factory preset index, -1 if user */
int sk_host_sel_user_slot(void);     /* user slot, -1 if factory */

/* Favorites (the PRESETS page's FAV star / LIST ALL|FAV filter). The plugin's
 * patch window mirrors the board's own browse list. */
int sk_host_favorites_filter(void);              /* 0 = ALL, 1 = FAV */
void sk_host_set_favorites_filter(int on);
int sk_host_engine_factory_fav(uint32_t rank, uint32_t i);  /* i-th shown factory preset */
void sk_host_engine_factory_fav_set(uint32_t rank, uint32_t i, int on);
int sk_host_user_fav(uint32_t slot);
void sk_host_user_fav_set(uint32_t slot, int on);

/* SAVE > USER page actions on NOR slot `slot` (0..31). Save stores the selected
 * track's sound; rename with name NULL/"" uses the automatic name. Return 1 on
 * an applied change so the caller can refresh. */
int sk_host_user_save(uint32_t slot);
int sk_host_user_erase(uint32_t slot);
int sk_host_user_rename(uint32_t slot, const char *name);
int sk_host_user_slot_count(void);   /* total slots (32), used or not */

/* Import-bay drag targets. Load a bay row into the selected track:
 *  - FM6 packed voice (128 B) → FM6, and  - ANALOG 12× int16 → ANALOG.
 * Both set the track's engine and refresh FM6's macro view. Return 1 ok. */
int sk_host_load_fm6_packed(const uint8_t *packed, uint32_t n);
int sk_host_load_analog_values(const int16_t *values /* 12 */, uint32_t n);
/* Save a bay row's sound straight into NOR slot `slot` (drag I-* → Uxx). `kind`
 * 1 = FM6 packed voice, 2 = ANALOG values. Return 1 ok. */
int sk_host_user_save_sound(uint32_t slot, int kind, const uint8_t *packed, const int16_t *values,
                            uint32_t n, const char *name);

/* #region agent log — debug-2f1224 (session 2f1224) */
void sk_host_dbg(const char *hyp, const char *loc, const char *msg, long a, long b, long c);
int sk_host_audio_mtx_trylock(void);
void sk_host_audio_mtx_unlock(void);
/* #endregion */

#ifdef __cplusplus
}
#endif

