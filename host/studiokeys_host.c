/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
 * The design (the device clock driven by the audio the host asks for, the exports below, the saved
 * flash handed back before boot) follows X0X's browser build of charlesvestal/fm1-x0x
 * (GPL-3.0), a StudioKEYS fork. */
/* StudioKEYS Micro reference host: the firmware's sources, as the host tests build them (tests/hostsim.c: libc,
 * engines, voices, FX, sequencer; then the UI, main.c, storage and projects, in src/studiokeys.c's order),
 * compiled natively and driven against a simulated panel:
 *   - the device clock: TIMER4 ticks (24 MHz) and fm1_ms advance with the audio the host asks for; the
 *     audio ISR (audio.c fm1_alnk0_irq, the real one) renders each I2S half (HALF_FRAMES) as the DMA would
 *     ask for it, and the main loop (main.c's, below in web_frame) runs every 15 ms, with ui_input on every
 *     millisecond in between as main.c's wait loop calls it;
 *   - the panel: buttons and encoders by their printed labels (panel.c's table maps them to the matrix),
 *     the keys as fm1_in.notes, the LEDs read back from fm1_led / fm1_led_dim through the key matrix;
 *   - the screen: lcd_fill / lcd_blit into a 240 x 240 RGB565 frame (the panel's byte order);
 *   - the flash: a 1 MiB NOR image behind storage.c's hooks (settings, projects, user presets, the user
 *     sample slots read through it as XIP); the host keeps its storage sectors and gives them back at boot.
 * Not here: USB (MIDI, audio, the editor, updates), TRS MIDI, the serial console, the battery (full), the
 * crash / UBOOT paths. Nothing of the firmware changes for this file. */
#include <stdint.h>

#define SKM_WEB 1
#ifndef SKM_CDC
#define SKM_CDC 1                    /* as src/studiokeys.c: MENU > USB SERIAL is there (nothing behind it) */
#endif
#define SKM_FLASH 1
#define SKM_OTA 0
#define SKM_UAC 0
#define SKM_UART 0
#ifndef SKM_VERSION
#define SKM_VERSION "WEB"
#endif

/* Debug trace hook. sk_host.c defines the real SK_DBG (NDJSON logger) before pulling this
 * file in; native_check.c builds it standalone, so provide a no-op fallback here. */
#ifndef SK_DBG
#define SK_DBG(hyp, loc, msg, a, b, c) ((void) 0)
#endif

static uint8_t nor[0x100000];            /* the SPI NOR (erased: 0xFF) */
#define SMP_USER_XIP(k) ((const uint8_t *)nor + SMP_USER_BASE + (k) * SMP_USER_SIZE)   /* eng_sample.c */
#define main hostsim_main
#include "../tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u             /* TIMER4: 24 MHz */
static uint32_t web_ticks;
static uint32_t fm1_ticks(void) { return web_ticks; }

/* ------------------------------------------------------ audio (ALNK0) --- */
static uint32_t web_half;                /* the half the next render fills */
#define FM1_AUDIO_HALF 1u
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return web_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{ (void)buf; (void)words; (void)isr; (void)prio; }
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}
#include "../firmware/src/audio.c"

/* ------------------------------------------------------------ the panel --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as hal/fm1_input.h: key id at (column, row bit) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL];
static uint32_t web_dim_lo;
static void fm1_led_dim_level(uint32_t lo) { web_dim_lo = lo; }
static uint32_t web_pressed, web_released, web_notes_pressed;
static int32_t web_enc_steps[8];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    if (released)
        *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = web_notes_pressed; web_notes_pressed = 0; return n; }
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s;
    if (e >= 8u)
        return 0;
    s = web_enc_steps[e];
    web_enc_steps[e] = 0;
    return s;
}
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_wdt_feed(void) {}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}

/* ----------------------------------------------------------- the screen --- */
static uint16_t web_fb[240 * 240];       /* RGB565 as the panel gets it (high byte first) */
static uint32_t web_draws;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = sw;
    web_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = p[j * w + i];
    web_draws++;
}
static void lcd_sync(void) {}
static void lcd_init(void) {}

#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
/* Host/web: Live/HUI surfaces are device-only. Stub the entry points ui_* call. */
typedef struct {
    uint8_t face, pad_face, keys_to_daw, bank, playing, recording, metro, dirty;
    uint8_t mute[4], solo[4], arm[4], sel[4], fader[4], clip_led[8];
    char name[4][9];
} sk_live_t;
typedef struct {
    uint8_t bank, nibble, playing, recording, dirty;
    uint8_t mute[8], solo[8], rec[8], sel[8], fader[8];
    char name[8][5];
    char tc[12];
} sk_hui_t;
static sk_live_t sk_live_host;
static sk_hui_t sk_hui_host;
static void sk_live_enter(void) { memset(&sk_live_host, 0, sizeof sk_live_host); sk_live_host.dirty = 1; }
static void sk_hui_enter(void)
{
    memset(&sk_hui_host, 0, sizeof sk_hui_host);
    sk_hui_host.dirty = 1;
    sk_hui_host.tc[0] = '-'; sk_hui_host.tc[1] = '-'; sk_hui_host.tc[2] = 0;
}
static void sk_live_poll(uint32_t p) { (void)p; }
static void sk_hui_poll(uint32_t p) { (void)p; }
static const sk_live_t *sk_live_state(void) { return &sk_live_host; }
static const sk_hui_t *sk_hui_state(void) { return &sk_hui_host; }
static void sk_live_ack_dirty(void) { sk_live_host.dirty = 0; }
static void sk_hui_ack_dirty(void) { sk_hui_host.dirty = 0; }

/* Host/web stand-in for firmware sk_surf_link_sysex (device builds include sk_surf.c).
 * Non-static so host/sk_host.c (which #includes this TU) can expose sk_host_surf_hits(). */
uint32_t sk_surf_host_hits;
void sk_surf_link_sysex(const uint8_t *d, uint32_t n)
{
    uint8_t cmd;
    if (!d || n < 7u || d[0] != 0xF0u || d[n - 1u] != 0xF7u)
        return;
    if (d[1] != 0x7Du || d[2] != 0x53u || d[3] != 0x4Bu || d[4] != 0x53u)
        return;
    cmd = d[5];
    sk_surf_host_hits++;
    if (cmd == 0x01u && n >= 9u) {
        uint32_t note = d[6] & 127u, vel = d[7] & 127u;
        if (note >= 36u && note < 44u)
            sk_live_host.clip_led[note - 36u] = vel ? 1u : 0u;
        sk_live_host.dirty = 1;
    } else if (cmd == 0x02u && n >= 9u) {
        uint32_t cc = d[6] & 127u, val = d[7] & 127u;
        if (cc >= 41u && cc < 45u)
            sk_live_host.fader[cc - 41u] = (uint8_t)val;
        sk_live_host.dirty = 1;
    } else if (cmd == 0x11u && n >= 9u) {
        uint32_t strip = d[6] & 7u, val = d[7] & 127u;
        sk_hui_host.fader[strip] = (uint8_t)val;
        sk_hui_host.dirty = 1;
    } else if (cmd == 0x12u && n >= 9u) {
        /* HUI note on cable 2 — host stub only bumps dirty for hit counting. */
        sk_hui_host.dirty = 1;
    }
}

/* Inject SMK side keys / faders for the plugin host (Live/HUI holds). */
#define SK_HOST_SMK_EXTRA 1
#define SK_HOST_PADS 1               /* the companion's pad grid injects ui_pads.c's edges (sk_host_pad) */
static volatile int16_t smk_an[8];
static uint32_t sk_host_x_down, sk_host_x_edges;
static uint32_t smk_extra_edges(void)
{
    uint32_t e = sk_host_x_edges;
    sk_host_x_edges = 0;
    return e;
}
static uint32_t smk_extra_down(void) { return sk_host_x_down; }
static void sk_host_smk_extra_set(uint32_t down)
{
    sk_host_x_edges |= (sk_host_x_down ^ down);
    sk_host_x_down = down;
}
static void sk_host_smk_an_set(uint32_t idx, int16_t v)
{
    if (idx < 8u)
        smk_an[idx] = v;
}

#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_diag.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#include "../firmware/src/ui_hints.c"     /* P6c: the panel buttons' current functions (VST strips) */
#include "../firmware/src/game.c"         /* P19/P20: FLAPPY / TETRIS */

/* ------------------------------------------------------------ the flash --- */
static uint32_t web_flash_writes;
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= sizeof nor || 4096u > sizeof nor - off)
        return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    web_flash_writes++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)       /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= sizeof nor || n > sizeof nor - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    web_flash_writes++;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }             /* the FM-1's part (project.c) */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

/* ------------------------------------------------- main.c and its HAL --- */
typedef struct { uint32_t vec, pc, emu, dbg, rets; } fm1_crash_t;
static struct { uint8_t p3_rst, wdt_con; uint32_t rst_src; } fm1_boot;
uint32_t _data_start[1], _data_end[1], _data_load[1], _bss_start[1], _bss_end[1];
uint32_t _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1], _rt_load[1];
uint32_t _rd_start[1], _rd_end[1];      /* .ram_data: empty on the host, the mirror is heap-backed */
static uint32_t web_master_adc = 700;   /* MASTER, 0..1023 as the ADC reads it */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : 620; }
static void fm1_adc_init(void) {}
static uint8_t smk_defer_pads;          /* main.c: skip the slow pad scan while audio is rendering */
static int32_t fm1_batt_gauge(void) { return 700; }
static void fm1_uart1_tx_init(void) {}
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}
static void fm1_reboot(void) {}
static void fm1_enter_uboot(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_irq_enable_all(void) {}
static void fm1_irq_init(void) {}
static void fm1_time_init(void) {}
static void fm1_reset_reason(void) {}
static void fm1_wdt_arm(uint32_t v) { (void)v; }
static void fm1_mailbox_clear(void) {}
static void fm1_guard_enable(uint32_t m) { (void)m; }
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
#if SKM_CDC
static void cdc_task(void) {}
#endif
/* main.c's calls into the USB driver (registers): nothing here (usb.c's own functions stay as they are) */
#define usb_poll() ((void)0)
#define usb_start() ((void)0)
#define usb_retry(ms) ((void)(ms))
#define usb_detach() ((void)0)
#include "../firmware/src/bootanim.c"     /* P6f: the boot splash (web_boot_anim) */
#include "../firmware/src/main.c"
#undef usb_poll
#undef usb_start
#undef usb_retry
#undef usb_detach

/* --------------------------------------------------------- the device --- */
void web_boot(void);
int web_boot_anim(void);
static uint32_t web_booted, web_boot_ms, web_boot_t0, web_last_frame, web_anim;
static int web_anim_played;                   /* P6f: once per power-on, not once per editor */
static uint32_t web_boot_at;                   /* the device clock at this power-on (web_power_on) */
static int32_t web_knob = 512 * 16;           /* main.c: the MASTER filter (ADC x 16) */

/* P6f: the boot animation on the companion (bootanim.c boot_screen / boot_pads), stepped by the device
 * clock in web_ms: the mark on the screen, the pads as the red loading bar, then all green. The
 * instance's own boot does not run it (a headless host has no window); the editor asks for it - and
 * gets it once (1), or 0 once this power-on has answered. Two ways it used to park the window on the
 * splash for good: replaying it on every editor reopen, and starting it on a device the host keeps
 * suspended (a silent synth under Smart Disable) - the hold is device time, so a clock that is not
 * being advanced by the audio thread never reaches its end. Hence: once, and only when the clock has
 * proved it is alive since power-on (100 ms of it; a fresh boot keeps the mark its own boot drew). */
int web_boot_anim(void)
{
    if (web_anim_played)
        return 0;
    web_anim_played = 1;
    if ((uint32_t)(fm1_ms - web_boot_at) < 100u)
        return 0;                              /* suspended: the boot screen from web_power_on stays */
    boot_screen();
    boot_pads(0);
    pad_boot_paint();
    web_boot_t0 = fm1_ms;
    web_boot_ms = fm1_ms + BOOT_MS;
    web_last_frame = fm1_ms;
    web_anim = 1;
    web_booted = 1;
    return 1;
}

/* main.c fm1_main's boot, up to the main loop (no USB, no UART, no panel setup) */
static void web_power_on(void)
{
    persist_boot();
    settings_init();
    usb_serial_apply();
    lcd_init();
    boot_screen();                         /* P6f: the mark (the wordmark is gone); the 1 s bar runs on the editor's open */
    memset(&skm_dbg, 0, sizeof skm_dbg);
    skm_dbg.magic = DBG_MAGIC;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    skm_init();
    audio_init();
    web_boot_ms = fm1_ms + 430u;          /* main.c: 30 + 400 ms before the first frame */
    web_boot_at = fm1_ms;                 /* P6f: the clock a later splash asks about */
    web_anim_played = 0;                  /* P6f: a new power-on owes the editor its splash again */
    web_booted = 1;
}

/* one pass of main.c fm1_main's loop (what the browser has: no USB, editor, UBOOT, console) */
static void web_frame(void)
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT), a = fm1_adc_read(FM1_ADC_MASTER);
    uint32_t k10;
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    web_knob += (a * 16 - web_knob) / 8;
    k10 = (uint32_t)(web_knob / 16);
    song.master_q12 = (k10 * k10) >> 8;
    skm_dbg.ui_frames++;
    ui_input();
    settings_poll();
    ui_leds();
    ui_draw();
}

/* output ring, stereo float, filled by web_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due;

/* one millisecond of the device: the clock, the audio halves due by now, the main loop */
static void web_ms(void)
{
    if (!web_booted)
        web_boot();
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {      /* an I2S half: the audio ISR renders it */
        uint32_t k;
        const int32_t *o = &abuf[web_half * HALF_WORDS];
        web_samples_due -= 1000u * HALF_FRAMES;
        fm1_alnk0_irq();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)o[2u * k] / 8388608.0f;
            ring[i + 1] = (float)o[2u * k + 1u] / 8388608.0f;
        }
        web_half ^= 1u;
    }
    if (!web_booted)
        return;
    if ((int32_t)(fm1_ms - web_boot_ms) < 0) {            /* the screen is held before the first frame */
        if (web_anim && fm1_ms - web_last_frame >= 20u) { /* P6f: the 1 s splash, 20 ms frames */
            web_last_frame = fm1_ms;
            boot_pads(fm1_ms - web_boot_t0);
            pad_boot_paint();
        }
        return;
    }
    if (web_boot_ms) {                                    /* main.c: the splash goes, the loop starts */
        web_boot_ms = 0;
        web_anim = 0;
        pad_boot_clear();
        lcd_fill(0, 0, 240, 240, T_BG);
        web_last_frame = fm1_ms - 15u;
    }
    if (fm1_ms - web_last_frame >= 15u) {                /* main.c: a frame, then ui_input until 15 ms are over */
        web_last_frame = fm1_ms;
        web_frame();
    } else {
        ui_input();                                       /* main.c's wait loop */
    }
}

#define EXPORT __attribute__((used, visibility("default")))
#undef __attribute__

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (!web_booted)
        web_boot();
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        web_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

/* the flash: erased, then the page's saved sectors (web_nor), then boot */
EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return web_flash_writes; }
EXPORT void web_boot(void)
{
    if (!web_booted)
        web_power_on();
}

/* Full power-on again (a new plugin instance in the same process after sk_host_init erased and
 * overlaid NOR): persist_boot()/settings_init() must re-read the restored image, or the second
 * instance would come up on a blank device. */
EXPORT void web_reboot(void)
{
    web_booted = 0;
    web_power_on();
}

/* input, by the printed labels: buttons bit b = B_FX .. B_OCTUP (panel.c), keys bit k = key k (F3 .. G5),
 * encoders by role (EN_SELECT .. EN_K4), MASTER 0..1023 */
static uint32_t web_btn_labels, web_key_mask;
EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, b, old = fm1_in.buttons;
    web_btn_labels = labels;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    web_pressed |= m & ~old;
    web_released |= old & ~m;
    fm1_in.buttons = m;
}
EXPORT void web_keys(uint32_t mask)
{
    uint32_t old = fm1_in.notes;
    mask &= (1u << 27) - 1u;
    web_key_mask = mask;
    web_notes_pressed |= mask & ~old;
    fm1_in.notes = mask;
}
EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < NE)
        web_enc_steps[panel.enc[role] % 8u] += n * panel.dir[role];
}
/* Physical encoder index 0..7 (SMK Micro). HOME bank and on-device cards use this path. */
EXPORT void web_enc_phys(uint32_t e, int32_t n)
{
    if (e < 8u)
        web_enc_steps[e] += n;
}
EXPORT void web_master(uint32_t v) { web_master_adc = v > 1023u ? 1023u : v; }
/* a channel message from Web MIDI, as a USB-MIDI packet arrives (usb.c midi_enqueue): 0 = the ring was full */
EXPORT uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status >= 0xF0u)
        return 1;
    return (uint32_t)midi_enqueue((status >> 4) | status << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24, 1);
}

/* the LEDs: bit b = the button labelled b lit (bit 14: PLAY's green), keys bit k; the dim glow and the breath alike */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_buttons(const uint8_t *l)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= led_on(l, panel.btn[b]) << b;
    return m;
}
static uint32_t led_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_on(l, 14u + k) << k;
    return m;
}
EXPORT uint32_t web_lit_buttons(void)
{
    return led_buttons(fm1_led) | ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u) << 14;
}
EXPORT uint32_t web_lit_keys(void) { return led_keys(fm1_led); }
EXPORT uint32_t web_dim_buttons(void) { return led_buttons(fm1_led_dim); }
EXPORT uint32_t web_dim_keys(void) { return led_keys(fm1_led_dim); }
EXPORT uint32_t web_dim_level(void) { return web_dim_lo; }
EXPORT uint32_t web_breath_buttons(void) { return led_buttons(fm1_led_breath); }   /* #119: dark .. ~60 % of lit */
EXPORT uint32_t web_breath_keys(void) { return led_keys(fm1_led_breath); }

/* for the bench (host/native_check.c), a heavy song straight into the tracks (as hostsim's
 * renders set them): T1 FM6 PAD, T2 PHYS DRONE STRING (SYMP), T3 GRAIN CLOUD PAD, 4-note chords on all 16
 * steps, held; T4 DRUM, kick snare and hats. PLAY is the page's (or the test's) to press */
EXPORT void web_test_heavy(void)
{
    static const uint8_t ENG[3][2] = {{12, 4}, {ENGI_PHYS, 7}, {8, 0}};
    static const uint8_t CH[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    uint32_t p, i;
    for (p = 0; p < 3u; p++) {
        track_t *t = &trk[p];
        host_preset(t, ENG[p][0], ENG[p][1]);
        t->p[P_VOICE] = V_POLY;
        t->p[P_SUS] = 127;
        for (i = 0; i < 16u; i++) {
            uint8_t n[4], k;
            for (k = 0; k < 4u; k++)
                n[k] = (uint8_t)(CH[i / 4u][k] + 12u * p);
            put_step(t, i, 4, n, ST_NOTE, 0);
        }
    }
    host_drums(&trk[3]);
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        n[k++] = i % 2u ? 42 : 46;
        put_step(&trk[3], i, k, n, ST_NOTE, 0);
    }
    ui.force = 1;
}

EXPORT uint32_t web_test_user_preset(uint32_t i) { return (uint32_t)up_used(i); }

EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_playing(void) { return song.playing; }
EXPORT uint32_t web_sample_rate(void) { return FS; }
