/* Thin host stub used only when ../../host is absent. Matches sk_host.h + SKL lock. */
#include "sk_host.h"
#include "sk_skl.h"
#include <string.h>

static uint16_t g_fb[SK_HOST_FB_W * SK_HOST_FB_H];
static uint8_t g_nor[SK_HOST_NOR_SIZE];
static int g_unleashed;
static int g_lock;
static int g_claimed;
static uint32_t g_ms;
static uint32_t g_draws;
static uint32_t g_palette;
static char g_peer[32];
static uint32_t g_peer_crc;
static int g_peer_crc_ok;
static const char *g_reject = "";
static uint8_t g_out[1024];
static uint32_t g_out_r, g_out_w;
static uint8_t g_sx[128];
static uint32_t g_sx_n;
static int g_sx_on;
static int g_evt_apply;

static uint32_t stub_crc(void)
{
  /* Stable empty-NOR style CRC stand-in. */
  uint32_t c = 0xFFFFFFFFu, i;
  for (i = 0; i < 64u; i++) {
    c ^= g_nor[i];
    for (int b = 0; b < 8; b++)
      c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int)(c & 1u));
  }
  return ~c;
}

static void out_bytes(const uint8_t *p, uint32_t n)
{
  uint32_t i, space = (uint32_t)sizeof g_out - (g_out_w - g_out_r);
  if (n > space) n = space;
  for (i = 0; i < n; i++)
    g_out[(g_out_w + i) % sizeof g_out] = p[i];
  g_out_w += n;
}

static void skl_reply(uint8_t cmd, const uint8_t *pay, uint32_t np)
{
  uint8_t f[96];
  uint32_t n = 0, i;
  f[n++] = 0xF0; f[n++] = SK_SKL_HDR0; f[n++] = SK_SKL_HDR1;
  f[n++] = SK_SKL_HDR2; f[n++] = SK_SKL_HDR3; f[n++] = cmd;
  for (i = 0; i < np && n + 1u < sizeof f; i++)
    f[n++] = (uint8_t)(pay[i] & 0x7Fu);
  f[n++] = 0xF7;
  out_bytes(f, n);
}

static void skl_handle(const uint8_t *f, uint32_t n)
{
  uint8_t cmd;
  if (n < 7u || f[0] != 0xF0u || f[1] != SK_SKL_HDR0 || f[2] != SK_SKL_HDR1 ||
      f[3] != SK_SKL_HDR2 || f[4] != SK_SKL_HDR3 || f[n - 1u] != 0xF7u)
    return;
  cmd = f[5];
  if (cmd == SK_SKL_HELLO) {
    uint32_t i, m = 0;
    char ver[32];
    uint8_t pay[32], k = 0;
    const char *hv = sk_host_version();
    for (i = 6; i + 1u < n && m + 1u < sizeof ver; i++) {
      if (!f[i]) break;
      ver[m++] = (char)f[i];
    }
    ver[m] = 0;
    sk_host_lock_set_peer_version(ver);
    g_reject = "";
    g_peer_crc_ok = 0;
    for (; hv[k] && k + 1u < sizeof pay; k++)
      pay[k] = (uint8_t)hv[k];
    pay[k++] = 0;
    skl_reply(SK_SKL_HELLO_ACK, pay, k);
    {
      uint32_t crc = stub_crc();
      uint8_t c[5] = {
        (uint8_t)(crc & 0x7Fu), (uint8_t)((crc >> 7) & 0x7Fu),
        (uint8_t)((crc >> 14) & 0x7Fu), (uint8_t)((crc >> 21) & 0x7Fu),
        (uint8_t)((crc >> 28) & 0x0Fu)
      };
      skl_reply(SK_SKL_CRC_ACK, c, 5);
    }
    if (g_peer[0] && strcmp(g_peer, sk_host_version()) != 0) {
      uint8_t r = SK_SKL_REFUSE_VERSION;
      g_reject = "Refuse: keyboard build does not match the plugin.";
      g_lock = SK_LOCK_NONE;
      skl_reply(SK_SKL_RESULT, &r, 1);
    } else if (g_lock != SK_LOCK_ON) {
      g_lock = SK_LOCK_ASK;
    }
  } else if (cmd == SK_SKL_CRC && n >= 12u) {
    g_peer_crc = (uint32_t)f[6] | (uint32_t)f[7] << 7 | (uint32_t)f[8] << 14 |
                 (uint32_t)f[9] << 21 | (uint32_t)(f[10] & 0x0Fu) << 28;
    g_peer_crc_ok = 1;
  } else if (cmd == SK_SKL_CHOOSE) {
    uint8_t r;
    if (g_lock != SK_LOCK_ASK) {
      r = SK_SKL_REFUSE_STATE;
      g_reject = "Refuse: sync prompt is not open.";
      skl_reply(SK_SKL_RESULT, &r, 1);
    } else if (g_peer[0] && strcmp(g_peer, sk_host_version()) != 0) {
      r = SK_SKL_REFUSE_VERSION;
      g_reject = "Refuse: keyboard build does not match the plugin.";
      g_lock = SK_LOCK_NONE;
      skl_reply(SK_SKL_RESULT, &r, 1);
    } else if (!g_peer_crc_ok || g_peer_crc != stub_crc()) {
      r = SK_SKL_REFUSE_CRC;
      g_reject = "Refuse: snapshot CRC does not match.";
      g_lock = SK_LOCK_NONE;
      skl_reply(SK_SKL_RESULT, &r, 1);
    } else {
      g_reject = "";
      g_lock = SK_LOCK_ON;
      r = SK_SKL_OK;
      skl_reply(SK_SKL_RESULT, &r, 1);
    }
  } else if (cmd == SK_SKL_EVENT && g_lock == SK_LOCK_ON && n >= 11u) {
    g_evt_apply = 1;
    sk_host_set_param((uint32_t)f[6] | (uint32_t)f[7] << 7,
                      (int32_t)((uint32_t)f[8] | (uint32_t)f[9] << 7) - 8192);
    g_evt_apply = 0;
  } else if (cmd == SK_SKL_DROP) {
    g_lock = SK_LOCK_NONE;
    g_peer_crc_ok = 0;
  } else if (cmd == SK_SKL_DISMISS) {
    if (g_lock == SK_LOCK_ASK) g_lock = SK_LOCK_NONE;
    g_peer_crc_ok = 0;
  }
}

void sk_host_init(void)
{
  memset(g_fb, 0, sizeof g_fb);
  memset(g_nor, 0xFF, sizeof g_nor);
  g_unleashed = 0;
  g_lock = SK_LOCK_NONE;
  g_ms = 0;
  g_draws = 0;
  g_palette = 0;
  g_peer[0] = 0;
  g_peer_crc = 0;
  g_peer_crc_ok = 0;
  g_reject = "";
  g_out_r = g_out_w = 0;
  g_sx_n = 0;
  g_sx_on = 0;
}

void sk_host_boot(void) {}

void sk_host_render_f32(float *L, float *R, uint32_t n)
{
  uint32_t i;
  if (!L || !R)
    return;
  for (i = 0; i < n; i++)
    L[i] = R[i] = 0.f;
  g_ms += (n * 1000u) / SK_HOST_FS;
}

const uint16_t *sk_host_screen(void) { return g_fb; }
uint32_t sk_host_screen_draws(void) { return g_draws; }
uint8_t *sk_host_nor(void) { return g_nor; }
uint32_t sk_host_nor_size(void) { return SK_HOST_NOR_SIZE; }
void sk_host_nor_erase(void) { memset(g_nor, 0xFF, sizeof g_nor); }
uint32_t sk_host_flash_writes(void) { return 0; }
int sk_host_midi(uint8_t status, uint8_t d1, uint8_t d2) { (void)status; (void)d1; (void)d2; return 1; }
void sk_host_buttons(uint32_t labels) { (void)labels; }
void sk_host_keys_mask(uint32_t mask27) { (void)mask27; }
void sk_host_enc(uint32_t role, int32_t steps) { (void)role; (void)steps; }
void sk_host_enc_phys(uint32_t idx0_7, int32_t steps) { (void)idx0_7; (void)steps; }
void sk_host_master(uint32_t v0_1023) { (void)v0_1023; }
const char *sk_host_knob_label(uint32_t slot0_7) { (void)slot0_7; return "—"; }
int sk_host_knob_finite(uint32_t slot0_7) { (void)slot0_7; return 0; }
int sk_host_knob_norm(uint32_t slot0_7, float *out01) { (void)slot0_7; if (out01) *out01 = 0.f; return 0; }
const char *sk_host_knob_value(uint32_t slot0_7) { (void)slot0_7; return "—"; }
const char *sk_host_fader_label(uint32_t visual0_3) { (void)visual0_3; return "—"; }
const char *sk_host_fader_value(uint32_t visual0_3) { (void)visual0_3; return "—"; }
void sk_host_smk_extra(uint32_t down_mask) { (void)down_mask; }
void sk_host_smk_fader(uint32_t idx0_7, int16_t adc0_1023) { (void)idx0_7; (void)adc0_1023; }
void sk_host_set_param(uint32_t id, int32_t v) { (void)id; (void)v; (void)g_evt_apply; }
int32_t sk_host_get_param(uint32_t id) { (void)id; return 0; }
void sk_host_set_gparam(uint32_t id, int32_t v) { (void)id; (void)v; }
int32_t sk_host_get_gparam(uint32_t id) { (void)id; return 0; }
void sk_host_set_palette(uint32_t idx) { g_palette = idx % 19u; }
uint32_t sk_host_palette(void) { return g_palette; }
void sk_host_set_vst_connected(int on) { (void)on; }
int sk_host_vst_connected(void) { return 0; }
void sk_host_set_device_present(int on) { (void)on; }
int sk_host_device_present(void) { return 0; }
uint32_t sk_host_npalettes(void) { return 19u; }
uint32_t sk_host_menu_count(void) { return 17u; }
uint32_t sk_host_menu_values(void) { return 14u; }
uint32_t sk_host_menu_tab_count(void) { return 4u; }
uint32_t sk_host_menu_tab_of(uint32_t row) { return row < 6u ? 0u : row < 10u ? 1u : row < 13u ? 2u : 3u; }
const char *sk_host_menu_tab_name(uint32_t t) {
  static const char *n[] = {"DISPLAY", "CONTROL", "AUDIO", "SYSTEM"};
  return t < 4u ? n[t] : "";
}
const char *sk_host_menu_row_label(uint32_t row) { (void)row; return "—"; }
uint32_t sk_host_menu_n(uint32_t row) { (void)row; return 2u; }
uint32_t sk_host_menu_get(uint32_t row) { (void)row; return 0u; }
const char *sk_host_menu_vname(uint32_t row, uint32_t v) { (void)row; (void)v; return "—"; }
void sk_host_menu_put(uint32_t row, uint32_t v) { (void)row; (void)v; }
int sk_host_uboot_hw_take(void) { return 0; }
void sk_host_menu_uboot(void) {}
void sk_host_about_open(void) {}
void sk_host_about_close(void) {}
int sk_host_about_is_open(void) { return 0; }
uint32_t sk_host_about_line_count(void) { return 0; }
const char *sk_host_about_line(uint32_t i, uint8_t *kind_out) {
  (void)i; if (kind_out) *kind_out = 0; return "";
}
void sk_host_palette_rgb565(uint16_t out5[5])
{
  if (!out5) return;
  out5[0] = 0x1903; out5[1] = 0x2985; out5[2] = 0xefbc; out5[3] = 0x96e9; out5[4] = 0xfe28;
}
static int g_lang;
void sk_host_set_lang(int id) { g_lang = id ? 1 : 0; }
int sk_host_lang(void) { return g_lang; }
void sk_host_set_unleashed(int on) { g_unleashed = on ? 1 : 0; }
int sk_host_unleashed(void) { return g_unleashed; }
uint32_t sk_host_voice_budget(void) { return g_unleashed ? 128u : 8u; }
uint32_t sk_host_voice_ceiling(void) { return 128u; }
void sk_host_waveform_peek(float *dst, uint32_t n)
{
  uint32_t i;
  if (!dst) return;
  for (i = 0; i < n; i++) dst[i] = 0.f;
}
const char *sk_host_version(void) { return "stub-1.0.0"; }
uint32_t sk_host_sample_rate(void) { return SK_HOST_FS; }
uint32_t sk_host_now_ms(void) { return g_ms; }
uint32_t sk_host_playing(void) { return 0; }
int sk_host_lock_get(void) { return g_lock; }
void sk_host_lock_ask(void) { if (g_lock != SK_LOCK_ON) g_lock = SK_LOCK_ASK; }
void sk_host_lock_set_peer_version(const char *ver)
{
  if (!ver) { g_peer[0] = 0; return; }
  strncpy(g_peer, ver, sizeof g_peer - 1);
  g_peer[sizeof g_peer - 1] = 0;
}
uint32_t sk_host_lock_peer_crc(void) { return g_peer_crc_ok ? g_peer_crc : 0u; }
const char *sk_host_lock_reject_reason(void) { return g_reject ? g_reject : ""; }
void sk_host_lock_choose(int plugin_to_hw)
{
  uint8_t dir = plugin_to_hw ? (uint8_t)SK_SKL_DIR_PLUGIN_TO_HW : (uint8_t)SK_SKL_DIR_HW_TO_PLUGIN;
  uint32_t crc = stub_crc();
  uint8_t f[16];
  uint32_t n = 0;
  f[n++] = 0xF0; f[n++] = SK_SKL_HDR0; f[n++] = SK_SKL_HDR1;
  f[n++] = SK_SKL_HDR2; f[n++] = SK_SKL_HDR3; f[n++] = SK_SKL_CRC;
  f[n++] = (uint8_t)(crc & 0x7Fu);
  f[n++] = (uint8_t)((crc >> 7) & 0x7Fu);
  f[n++] = (uint8_t)((crc >> 14) & 0x7Fu);
  f[n++] = (uint8_t)((crc >> 21) & 0x7Fu);
  f[n++] = (uint8_t)((crc >> 28) & 0x0Fu);
  f[n++] = 0xF7;
  skl_handle(f, n);
  f[0] = 0xF0; f[1] = SK_SKL_HDR0; f[2] = SK_SKL_HDR1; f[3] = SK_SKL_HDR2;
  f[4] = SK_SKL_HDR3; f[5] = SK_SKL_CHOOSE; f[6] = dir; f[7] = 0xF7;
  if (g_lock == SK_LOCK_NONE && !(g_peer[0] && strcmp(g_peer, sk_host_version()) != 0))
    g_lock = SK_LOCK_ASK;
  skl_handle(f, 8);
}
void sk_host_lock_dismiss(void)
{
  uint8_t f[7] = {0xF0, SK_SKL_HDR0, SK_SKL_HDR1, SK_SKL_HDR2, SK_SKL_HDR3, SK_SKL_DISMISS, 0xF7};
  skl_handle(f, 7);
}
void sk_host_lock_drop(void)
{
  uint8_t f[7] = {0xF0, SK_SKL_HDR0, SK_SKL_HDR1, SK_SKL_HDR2, SK_SKL_HDR3, SK_SKL_DROP, 0xF7};
  skl_handle(f, 7);
}
uint32_t sk_host_state_crc(void) { return stub_crc(); }
int sk_host_lock_claim_instance(void) { if (g_claimed) return 0; g_claimed = 1; return 1; }
void sk_host_lock_release_instance(void) { g_claimed = 0; g_lock = SK_LOCK_NONE; g_peer_crc_ok = 0; }
void sk_host_lock_autosave(void) {}
void sk_host_ui_get(sk_host_ui_t *out)
{
  if (!out) return;
  memset(out, 0, sizeof *out);
  out->desk = 0xFF;
  strcpy(out->page_title, "HOME");
  strcpy(out->engine_name, "ANALOG");
}
void sk_host_ui_pump(void) {}
void sk_host_set_desk(uint32_t desk_or_settings) { (void)desk_or_settings; }
const char *sk_host_edit_label(uint32_t desk, uint32_t k0_7)
{
  static const char *k[] = {"WAVE", "DTN", "MIX", "NOIS", "CUT", "RES", "DRV", "KTR"};
  (void)desk;
  return k0_7 < 8u ? k[k0_7] : "";
}
const char *sk_host_engine_name(uint32_t desk)
{
  static const char *k[] = {
    "ANALOG", "FM6", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL",
    "GRAIN", "PHYS", "NOISE", "SLICE", "DRUM"};
  return desk < 13u ? k[desk] : "";
}
uint32_t sk_host_page_count(void) { return 37u; }
const char *sk_host_page_title(uint32_t page)
{
  static const char *k[] = {"ENV", "LFO", "FX", "EDIT 1", "EDIT 2", "GLOBAL", "MIXER"};
  return page < 7u ? k[page] : "PAGE";
}
int sk_host_goto_page(uint32_t page) { (void)page; return 0; }
uint32_t sk_host_page_params(uint32_t page, uint32_t out_ids[4])
{
  (void)page;
  if (!out_ids) return 0;
  out_ids[0] = 1; out_ids[1] = 2; out_ids[2] = 3; out_ids[3] = 4;
  return 4;
}
const char *sk_host_page_param_label(uint32_t page, uint32_t slot0_3)
{
  static const char *k[] = {"ATK", "DEC", "SUS", "REL"};
  (void)page;
  return slot0_3 < 4u ? k[slot0_3] : "";
}
const char *sk_host_param_label(uint32_t id, int global)
{
  (void)global;
  static const char *k[] = {"LVL", "ATK", "DEC", "SUS", "REL"};
  return id < 5u ? k[id] : "P";
}
int sk_host_lock_plugin_clock(void) { return g_lock == SK_LOCK_ON; }
int sk_host_snap_save(void) { return 0; }
int sk_host_snap_load(void) { return 0; }
uint32_t sk_host_snap_bytes(uint8_t *dst, uint32_t cap)
{
  if (dst && cap) memset(dst, 0, cap < 16 ? cap : 16);
  return 16;
}
uint32_t sk_host_link_pop(uint8_t *dst, uint32_t cap)
{
  uint32_t n = g_out_w - g_out_r;
  if (!dst || !cap || !n) return 0;
  if (n > cap) n = cap;
  if (g_out_r + n > sizeof g_out) n = (uint32_t)sizeof g_out - g_out_r;
  memcpy(dst, g_out + g_out_r, n);
  g_out_r += n;
  if (g_out_r == g_out_w) g_out_r = g_out_w = 0;
  return n;
}
void sk_host_link_push(const uint8_t *src, uint32_t n)
{
  uint32_t i;
  if (!src || !n) return;
  for (i = 0; i < n; i++) {
    uint8_t b = src[i];
    if (b == 0xF0u) { g_sx_on = 1; g_sx_n = 0; }
    if (!g_sx_on) continue;
    if (g_sx_n < sizeof g_sx) g_sx[g_sx_n++] = b;
    if (b == 0xF7u) { g_sx_on = 0; skl_handle(g_sx, g_sx_n); g_sx_n = 0; }
  }
}
uint32_t sk_host_surf_hits(void) { return 0; }

static uint8_t g_fm6[SK_HOST_FM6_VOICE];
int sk_host_fm6_get(uint8_t *dst, uint32_t n)
{
  if (!dst || n < SK_HOST_FM6_VOICE) return 0;
  memcpy(dst, g_fm6, SK_HOST_FM6_VOICE);
  return (int)SK_HOST_FM6_VOICE;
}
uint8_t sk_host_fm6_get_byte(uint32_t off)
{
  if (off >= SK_HOST_FM6_VOICE) return 0;
  if (off == 134u) {
    int16_t e0 = g_trk[g_sel][0];
    if (e0 >= 1 && e0 <= 32) return (uint8_t)((e0 - 1) & 31);
  }
  return g_fm6[off];
}
void sk_host_fm6_set_byte(uint32_t off, uint8_t v)
{
  if (off < SK_HOST_FM6_VOICE) {
    g_fm6[off] = v;
    if (off == 134u) {
      g_trk[g_sel][0] = (int16_t)((v & 31u) + 1u);
    }
  }
}
void sk_host_fm6_init_voice(void)
{
  memset(g_fm6, 0, sizeof g_fm6);
  g_fm6[134] = 0; /* ALG */
}

static uint32_t g_sel;
static int16_t g_trk[SK_HOST_NTRK][96];
void sk_host_select_track(uint32_t tr0_3) { g_sel = tr0_3 % SK_HOST_NTRK; }
uint32_t sk_host_selected_track(void) { return g_sel; }
int32_t sk_host_trk_param(uint32_t tr0_3, uint32_t id)
{
  if (id >= 96u) return 0;
  return g_trk[tr0_3 % SK_HOST_NTRK][id];
}
void sk_host_set_trk_param(uint32_t tr0_3, uint32_t id, int32_t v)
{
  if (id >= 96u) return;
  g_trk[tr0_3 % SK_HOST_NTRK][id] = (int16_t)v;
}
const char *sk_host_trk_name(uint32_t tr0_3)
{
  static const char *k[] = {"T1", "T2", "T3", "T4"};
  return k[tr0_3 % SK_HOST_NTRK];
}
uint32_t sk_host_trk_engine(uint32_t tr0_3) { return (tr0_3 % SK_HOST_NTRK); }
uint32_t sk_host_trk_engine_rank(uint32_t tr0_3) { return (tr0_3 % SK_HOST_NTRK); }
const char *sk_host_trk_engine_name(uint32_t tr0_3)
{
  static const char *e[] = {"ANALOG", "FM6", "WHEEL", "DRUM"};
  return e[tr0_3 % 4];
}

/* Preset bank stubs: a tiny stand-in so the plugin builds without the host. */
uint32_t sk_host_preset_bank_count(void) { return 0u; }
int sk_host_preset_bank_at(uint32_t n, uint32_t *e, uint32_t *p)
{
  (void) n; if (e) *e = 0; if (p) *p = 0; return -1;
}
const char *sk_host_preset_bank_name(uint32_t n) { (void) n; return ""; }
const char *sk_host_preset_bank_engine_name(uint32_t n) { (void) n; return ""; }
uint32_t sk_host_preset_bank_pos(void) { return 0u; }
int sk_host_preset_bank_go(uint32_t n) { (void) n; return 0; }
uint32_t sk_host_engine_factory_count(uint32_t rank)
{
  static const uint32_t c[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  return rank < 13u ? c[rank] : 0u;
}
const char *sk_host_engine_factory_name(uint32_t rank, uint32_t i)
{
  static const char *n[] = {"INIT"};
  (void) rank; (void) i; return n[0];
}
uint32_t sk_host_engine_factory_preset(uint32_t rank, uint32_t i) { (void) rank; (void) i; return 0u; }
int sk_host_engine_factory_apply(uint32_t rank, uint32_t i) { (void) rank; (void) i; return 0; }
uint32_t sk_host_user_count(void) { return 0u; }
int sk_host_user_used(uint32_t slot) { (void) slot; return 0; }
const char *sk_host_user_name(uint32_t slot) { (void) slot; return ""; }
const char *sk_host_user_label(uint32_t slot) { (void) slot; return ""; }
const char *sk_host_user_engine_name(uint32_t slot) { (void) slot; return ""; }
int sk_host_user_apply(uint32_t slot) { (void) slot; return 0; }
uint32_t sk_host_sel_engine_rank(void) { return 0u; }
int sk_host_sel_preset(void) { return 0; }
int sk_host_sel_user_slot(void) { return -1; }

/* Favorites + user-slot ops: no-op stand-ins for stub builds. */
int sk_host_favorites_filter(void) { return 0; }
void sk_host_set_favorites_filter(int on) { (void) on; }
int sk_host_engine_factory_fav(uint32_t rank, uint32_t i) { (void) rank; (void) i; return 0; }
void sk_host_engine_factory_fav_set(uint32_t rank, uint32_t i, int on) { (void) rank; (void) i; (void) on; }
int sk_host_user_fav(uint32_t slot) { (void) slot; return 0; }
void sk_host_user_fav_set(uint32_t slot, int on) { (void) slot; (void) on; }
int sk_host_user_save(uint32_t slot) { (void) slot; return 0; }
int sk_host_user_erase(uint32_t slot) { (void) slot; return 0; }
int sk_host_user_rename(uint32_t slot, const char *name) { (void) slot; (void) name; return 0; }
int sk_host_user_slot_count(void) { return 32; }
int sk_host_load_fm6_packed(const uint8_t *packed, uint32_t n) { (void) packed; (void) n; return 0; }
int sk_host_load_analog_values(const int16_t *values, uint32_t n) { (void) values; (void) n; return 0; }
int sk_host_user_save_sound(uint32_t slot, int kind, const uint8_t *packed, const int16_t *values,
                            uint32_t n, const char *name)
{
  (void) slot; (void) kind; (void) packed; (void) values; (void) n; (void) name; return 0;
}

uint32_t sk_host_pe0_id(void)
{
  return 83;
}

int sk_host_format_param(uint32_t id, int32_t val, char *buf, uint32_t cap)
{
  if (!buf || !cap) return 0;
  snprintf(buf, cap, "%d", (int)val);
  return 1;
}
