/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
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
const char *sk_host_fader_label(uint32_t visual0_3);   /* 0..3 = T1..T4, left to right */
const char *sk_host_fader_value(uint32_t visual0_3);   /* secondary scribble under fader */
/* The fader cap's position (P_LEVEL of visual fader 0..3). The companion used to keep the cap in
 * its own mouse state only, so a preset load / pad / link change moved the value text but left the
 * cap where the user last dragged it. 0 = no absolute value (LIVE / HUI surfaces). */
int sk_host_fader_norm(uint32_t visual0_3, float *out01);
/* The MASTER pot's ADC (0..1023), the value sk_host_master() sets, for the top-bar fader's cap. */
uint32_t sk_host_master_get(void);
/* P8b: a signature of everything the companion draws from the keyboard — all four tracks'
 * parameters and FM6 patches, the globals, the page / desk / menu, the split and the sample
 * slots. OnIdle() compares it and repaints the desk when it moves, so a change made on the
 * keyboard (a knob, a pad, a preset load, an undo, a link event) shows up at once. It was a
 * hand-written pull list before, and every parameter somebody forgot stayed stale until the
 * next unrelated redraw. Cheap: a hash over ~1 KiB, once per idle. */
uint32_t sk_host_state_sig(void);
/* The 16 pads as the device lights them: (pad, down) injects the scan's edge; rgb is 0xGGRRBB from
 * ui_pads.c's PAD_RGB, abbr the legend cell (<= 4 chars), name the hint line. */
/* The 16 pads as the device lights them: (pad, down) injects the scan's edge; rgb is 0xRRGGBB from
 * ui_pads.c's pad_rgb_value (the same number the WS2812 chain gets), abbr the legend cell (<= 4
 * chars), name the hint line. */
void sk_host_pad(uint32_t pad0_15, int down);
uint32_t sk_host_pad_rgb(uint32_t pad0_15);
const char *sk_host_pad_abbr(uint32_t pad0_15);
const char *sk_host_pad_name(uint32_t pad0_15);
int sk_host_pad_down(uint32_t pad0_15);
/* P5e: a drawbar pad is a virtual drawbar. sk_host_pad_level() is its bar's level 0..8, or -1 when
 * the pad under the page's map is not a drawbar (the companion then treats the press as a plain
 * button). _set() rides it from a drag (0..8, clamped, captured like a knob by AUTO REC), _nudge()
 * steps it by +-1..n. Both repaint the chain and the legend; 1 = applied. */
int sk_host_pad_level(uint32_t pad0_15);
int sk_host_pad_level_set(uint32_t pad0_15, int32_t level0_8);
int sk_host_pad_level_nudge(uint32_t pad0_15, int32_t steps);
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
/* P6h: the keyboard's measured output clock and the pitch lock, as it reports them over the link.
 * 0 returned by either getter = the keyboard has not reported yet (or no link). */
void sk_host_set_hw_rate_ppm(int32_t ppm);
void sk_host_set_hw_rate_trim(int32_t q16_minus_unity);
int32_t sk_host_hw_rate_ppm(void);
int32_t sk_host_hw_rate_trim(void);
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
/* The same palette's derived colours, exactly as the panel's gfx.c palette_set makes them:
 * out[0..5] = MID DIM LINE SEL INK RAISE. A window that transcribes a panel screen (the games)
 * draws in these so the two pictures match. */
void sk_host_palette_rgb565_ex(uint16_t out6[6]);

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

/* P15: the user sample slots (USR1..USR3) — flash SMP_USER_BASE + k * SMP_USER_SIZE, the
 * same records `tools/fm1_sample_upload.py` streams over USB (smp_user_hdr_t + IMA ADPCM). */
#define SK_HOST_SAMPLE_SLOTS 3u
#define SK_HOST_SAMPLE_MAX_SEC 3.23f   /* (0x9000 - 512) * 2 / 22050 (hal/smk_map.h) */
/* A dropped WAV (mono PCM, `frames` at `rate`): the host resamples to 22.05 kHz, peak
 * normalises, IMA-encodes, writes the slot (data first, header last) and rescans it as an
 * on-device upload does. Only the slot's own 36 KiB is touched. 0 = written. */
int sk_host_sample_slot_write(uint32_t slot0_2, const int16_t *pcm, uint32_t frames, uint32_t rate,
                              const char *name);
int sk_host_sample_slot_clear(uint32_t slot0_2);
int sk_host_sample_slot_used(uint32_t slot0_2);      /* 1 = a valid record, 0 = empty */
const char *sk_host_sample_slot_name(uint32_t slot0_2);   /* the 8-char header name, "" */
uint32_t sk_host_sample_slot_frames(uint32_t slot0_2);    /* zone 0 samples (22.05 kHz) */
uint32_t sk_host_sample_slot_rate(uint32_t slot0_2);      /* zone 0 source rate (22050) */
/* The write's error code as a short line for the UI ("" for 0). */
const char *sk_host_sample_slot_error(int rc);

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
/* P6c: the 18 printed panel buttons, each with what it does now (ui_hints.c). idx follows panel.c's
 * B_NAME: 0..11 under the fader, 12..17 under the screen. Returns the count filled (18). */
#define SK_HOST_NBTN 18u
typedef struct {
  char name[20];     /* the printed name: ARP, NOTE REPEAT, KNOB BANK, OCT+ ... */
  char act[16];      /* a tap now: HOME, SAVE, NEXT page's title, ... ("" = nothing) */
  char hold[16];     /* a long press now: SETTINGS, PERFORM, UNDO ... ("" = none) */
} sk_host_btn_hint_t;
uint32_t sk_host_button_hints(sk_host_btn_hint_t *out, uint32_t max);
/* The firmware's current page index (ui.page) — for the companion page inspector. */
uint32_t sk_host_ui_page(void);
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
/* Fill up to 8 track/global param ids for page; returns count (skips 0xFF). */
uint32_t sk_host_page_params(uint32_t page, uint32_t out_ids[8]);
/* Firmware label for page slot 0..7 ("" if empty / step / mixer). */
const char *sk_host_page_param_label(uint32_t page, uint32_t slot0_7);
/* Track/global label by id (scope: 0=track TP, 1=global GP). */
const char *sk_host_param_label(uint32_t id, int global);

/* Page inspector (companion follows the firmware page: kCtrlTagPageTitle/Knob0..3).
 * scope: 0=track, 1=global, 2=engine(EDIT), 3=step, 4=MIXER. Returns PAGES[].fam. */
uint32_t sk_host_page_scope(uint32_t page);
uint32_t sk_host_page_fam(uint32_t page);
/* One knob of the firmware page at `slot` (0..3, MIXER 0..3). kind: 1 = track param
 * P_*, 2 = global G_*. Returns 0 for an empty / step slot. */
struct sk_host_page_slot {
    int kind;
    int id;
    int min, max, def, fmt;
    int value;
    char label[12];
    const char *const *names;   /* enum names, or 0 */
};
int sk_host_page_slot(uint32_t page, uint32_t slot0_4, struct sk_host_page_slot *out);

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
/* The same text for a global (G_*) id. Both run the firmware's own param_format, so the companion
 * prints exactly what the OLED does; a companion-side copy of the rules drifted and indexed an enum's
 * names by raw value (the LOFI WAV# / DCY lists are shorter than their 0..127 range: a crash). */
int sk_host_format_gparam(uint32_t id, int32_t val, char *buf, uint32_t cap);
/* How many entries a descriptor's names list has (0 = none). An F_ENUM list is indexed by value and
 * covers min..max; an F_INT one is 0-terminated and split evenly over the range (params.c). */
int sk_host_param_name_count(uint32_t id, int global);
/* The value that carries the i-th name of a descriptor with names, or -1 when it has none: the
 * companion's enum menus list one row per name instead of one per raw value. */
int sk_host_param_name_value(uint32_t id, int global, int nameIdx);
uint32_t sk_host_pe0_id(void);

/* P16: per-step automation (firmware motion.c). track 0..3, independent of the selection.
 * Events are (step 0..NSTEP-1, param P_*, value); on 1 = the step playback is enabled,
 * armed 1 = AUTO REC (knob moves become events while the transport plays). */
uint32_t sk_host_motion_count(uint32_t track);
int sk_host_motion_on(uint32_t track);
void sk_host_motion_set_on(uint32_t track, int on);
int sk_host_motion_armed(uint32_t track);
void sk_host_motion_set_armed(uint32_t track, int on);
int sk_host_motion_event(uint32_t track, uint32_t i, uint32_t *step, uint32_t *param, int32_t *value);
int sk_host_motion_set_event(uint32_t track, uint32_t step, uint32_t param, int32_t value); /* 0 ok, 1 bad, 2 full */
int sk_host_motion_remove_event(uint32_t track, uint32_t step, uint32_t param); /* 0 ok, 1 none */
void sk_host_motion_clear(uint32_t track);
int sk_host_motion_label(uint32_t track, uint32_t param, char *buf, uint32_t cap);
int sk_host_motion_range(uint32_t track, uint32_t param, int *lo, int *hi);

/* P18: undo/redo — the CFW's SAVE-held swap (firmware ui.c undo_swap) on the companion. A step is
 * the selected track's sound (engine, preset, user slot, the parameters a load may change, the FM6
 * patch and the motion pool), swapped in and out; PROGRAM LOADS take a checkpoint automatically and
 * knob moves coalesce into one step per sweep. Undo/redo act on the track the step was taken on.
 * sk_host_undo_mark(): checkpoint the sound before an edit anyway (Init, an import).
 * sk_host_undo()/sk_host_redo(): 1 = swapped, 0 = nothing to do / the motion pool is full.
 * Depth getters for the toolbar chips; the stamp changes on every stack change so the UI relights. */
void sk_host_undo_mark(void);
int sk_host_undo(void);
int sk_host_redo(void);
uint32_t sk_host_undo_depth(void);
uint32_t sk_host_redo_depth(void);
uint32_t sk_host_undo_stamp(void);
uint32_t sk_host_undo_capacity(void);

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
/* User slot a mixer channel's sound came from (-1 = factory/init, not a user preset). */
int sk_host_trk_user_slot(uint32_t tr0_3);

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
/* Load rank's engine INIT patch: the engine defaults with its first preset
 * (TOOLS > INIT). The plugin's Pxx list shows this as an INIT row per engine. */
int sk_host_engine_init(uint32_t rank);

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

/* SAVE > USER page actions on NOR slot `slot` (0..99). Save stores the selected
 * track's sound; rename with name NULL/"" uses the automatic name. Return 1 on
 * an applied change so the caller can refresh. */
int sk_host_user_save(uint32_t slot);
int sk_host_user_erase(uint32_t slot);
int sk_host_user_rename(uint32_t slot, const char *name);
int sk_host_user_slot_count(void);   /* total slots (100), used or not */

/* P11 program files (.skp): read a slot's full parameter set (record order, ids 0..P_COUNT-1)
 * or store an imported program. FM6 is excluded from the generic path — an FM6 sound carries
 * its voice in a separate patch and arrives as a DX7 .syx instead. */
int sk_host_user_values(uint32_t slot, int16_t *out, uint32_t cap); /* P_COUNT or 0 */
int sk_host_user_save_values(uint32_t slot, const char *engine, const int16_t *values,
                             uint32_t np, const char *name);
int sk_host_param_count(void);

/* P13 key split: 0 in `note` = off; keys below it play track `lo`, at/above it `hi` (0..3).
 * Setter clamps, releases sounding notes and marks the UI for redraw; 1 = applied. */
void sk_host_split_get(int32_t *note, int32_t *lo, int32_t *hi);
int sk_host_split_set(int32_t note, int32_t lo, int32_t hi);

/* Import-bay drag targets. Load a bay row into the selected track:
 *  - FM6 packed voice (128 B) → FM6, and  - ANALOG 12× int16 → ANALOG.
 * Both set the track's engine and refresh FM6's macro view. Return 1 ok. */
int sk_host_load_fm6_packed(const uint8_t *packed, uint32_t n);
int sk_host_load_analog_values(const int16_t *values /* 12 */, uint32_t n);
/* Save a bay row's sound straight into NOR slot `slot` (drag I-* → Uxx). `kind`
 * 1 = FM6 packed voice, 2 = ANALOG values. Return 1 ok. */
int sk_host_user_save_sound(uint32_t slot, int kind, const uint8_t *packed, const int16_t *values,
                            uint32_t n, const char *name);

/* P19 / P20 — the easter eggs (FLAPPY, TETRIS).
 * The companion renders the same deterministic state the CFW plays, in a bigger window, and is
 * the authority while the link is locked: it owns the one simulation, ships the state, and
 * applies the keyboard's forwarded input. Both sides mute every output while a game is open.
 * sk_host_game_view() fills a flat snapshot (no pointers) the UI can draw each idle. */
#define SK_GAME_NONE 0
#define SK_GAME_FLAPPY 1
#define SK_GAME_TETRIS 2

#define SK_GAME_TET_COLS 10
#define SK_GAME_TET_ROWS 18

typedef struct {
  uint8_t kind;      /* SK_GAME_*; 0 = closed */
  uint8_t alive;     /* flappy: bird alive; tetris: 0 while game over */
  uint8_t started;   /* flappy: the first flap happened */
  uint8_t over;      /* tetris: game over */
  uint8_t piece, rot, px, py, next0, next1; /* tetris: the active piece and the queue */
  /* flappy: the three gates, in the panel's 240 x 240 px. int16 because a gate leaves the left
   * edge (negative) and the ones to come wait off-screen at 336 / 432: as uint8 they truncated to
   * 80 / 176 and the window drew the gates in places the bird never met. */
  int16_t gx[3];
  int16_t gy[3];                            /* the gap's centre */
  uint8_t gh[3];                            /* the gap's height */
  int16_t bird_y, bird_vy;                  /* flappy: px and Q8 px/tick */
  int16_t score, lines;
  uint32_t rev;                             /* bumped on every state change */
  uint8_t board[SK_GAME_TET_COLS * SK_GAME_TET_ROWS]; /* 0 empty, 1..7 the tetromino colour */
} sk_game_view_t;

/* Input ops (the same numbers the link carries). */
enum {
  SK_GAME_IN_FLAP = 1,
  SK_GAME_IN_LEFT,
  SK_GAME_IN_RIGHT,
  SK_GAME_IN_ROT,
  SK_GAME_IN_DROP,
  SK_GAME_IN_GRAV
};

int sk_host_game_kind(void);            /* SK_GAME_* (0 = closed) */
void sk_host_game_open(int kind);       /* open on this side (and, when linked, on the keyboard) */
void sk_host_game_close(void);
void sk_host_game_input(int op);        /* flap / move / rotate / drop */
void sk_host_game_view(sk_game_view_t *out);
uint32_t sk_host_game_rev(void);
/* Advance the link side: throttled state broadcast while linked (call from OnIdle). */
void sk_host_game_poll(void);
/* Tests only (tests/game_host_test.cpp): pack one TETRIS half as the authority does, and apply one
 * game packet as the mirror does. The wire itself is exercised on the device (sk_link_lock.c). */
uint32_t sk_host_game_pack_test(uint8_t *d, uint32_t cap, int part);
int sk_host_game_apply_test(const uint8_t *d, uint32_t n);

#ifdef __cplusplus
}
#endif
