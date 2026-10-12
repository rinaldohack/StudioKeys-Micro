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
void sk_host_master(uint32_t v0_1023) { (void)v0_1023; }
void sk_host_smk_extra(uint32_t down_mask) { (void)down_mask; }
void sk_host_smk_fader(uint32_t idx0_7, int16_t adc0_1023) { (void)idx0_7; (void)adc0_1023; }
void sk_host_set_param(uint32_t id, int32_t v) { (void)id; (void)v; (void)g_evt_apply; }
int32_t sk_host_get_param(uint32_t id) { (void)id; return 0; }
void sk_host_set_palette(uint32_t idx) { g_palette = idx % 19u; }
uint32_t sk_host_palette(void) { return g_palette; }
uint32_t sk_host_npalettes(void) { return 19u; }
void sk_host_palette_rgb565(uint16_t out5[5])
{
  if (!out5) return;
  out5[0] = 0x1903; out5[1] = 0x2985; out5[2] = 0xefbc; out5[3] = 0x96e9; out5[4] = 0xfe28;
}
void sk_host_set_unleashed(int on) { g_unleashed = on ? 1 : 0; }
int sk_host_unleashed(void) { return g_unleashed; }
uint32_t sk_host_voice_budget(void) { return g_unleashed ? 32u : 8u; }
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
