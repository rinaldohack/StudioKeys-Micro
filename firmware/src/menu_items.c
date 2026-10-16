/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU's rows and their settings: what ui_menu.c draws and steps, and what the editor reads and sets (editor_menu.c
 * MENU_DESC / MENU_SET). One apply path for both: menu_put. Grouped: the screen (COLOR, STYLE, LARGE, ANIM, LEDS,
 * LANG), the controls (HOLD: the layer threshold, KNOB ACCEL, FX LATCH, BPM LOCK), the sound (SPEAKER EQ: FLAT
 * LOWCUT BASS+, USB LEVEL), USB SERIAL, then CALIBRATION (the setup screen: HARDWARE CALIBRATION), ABOUT and UPDATE,
 * the rows with no value (MI_VALUES: the rows before them hold one). 1.0.5: in four tabs (MI_TAB). */
enum { MI_COLOR, MI_STYLE, MI_LARGE, MI_ANIM, MI_LEDS, MI_LANG, MI_HOLD, MI_ACCEL, MI_LATCH, MI_BPMLOCK, MI_MASTER, MI_LOWCUT, MI_USB, MI_RMAUD, MI_SERIAL, MI_PANEL, MI_ABOUT, MI_UPDATE, MI_COUNT };
#define MI_VALUES MI_PANEL
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "STYLE", "LARGE", "ANIM", "LEDS", "LANG", "HOLD", "KNOB ACCEL", "FX LATCH", "BPM LOCK",
                                              "MASTER", "MASTER EQ", "USB LEVEL", "REMOTE OUT", "USB SERIAL", "CALIBRATION", "ABOUT", "UPDATE"};
/* 1.0.5: the MENU's tabs (ui_menu.c: ALGORITHM steps between them, PRESETS among one tab's rows; the editor gets a
 * row's tab after its MENU_DESC reply). A tab's rows follow each other in MI order (tests/ui_test.c checks it); at
 * most MTAB_ROWS each (the page does not scroll: ui_menu.c fits them). A new row joins a tab here, a new tab is
 * appended (its index is what the editor is told) */
enum { MTAB_DISPLAY, MTAB_CONTROL, MTAB_AUDIO, MTAB_SYSTEM, MTAB_COUNT };
#define MTAB_ROWS 6u
static const char *const MTAB_NAME[MTAB_COUNT] = {"DISPLAY", "CONTROL", "AUDIO", "SYSTEM"};
static const uint8_t MI_TAB[MI_COUNT] = {
    MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY, /* COLOR STYLE LARGE ANIM LEDS LANG */
    MTAB_CONTROL, MTAB_CONTROL, MTAB_CONTROL, MTAB_CONTROL,                 /* HOLD KNOB ACCEL FX LATCH BPM LOCK */
    MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO,                         /* MASTER (volume), Master EQ, USB LEVEL, REMOTE OUT */
    MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM,                     /* USB SERIAL, CALIBRATION, ABOUT, UPDATE */
};
/* favorites.factory[15][28]: settings_init (panel.c) reads this. panel.c is included before favorites.c, so the
 * body lives here, after the favorites object. */
static uint8_t *palette_ice_mark(void)
{
    return &favorites.factory[15][28];
}
/* MENU > LANG. English is ui_prefs bit 128 clear. Indonesian sets it. Bits 1,2,4,8,16,32,64 are ui.c PREF_*.
 * Labels only (lang_str): parameter names and preset names stay English. */
#define PREF_LANG 128u
static const char *lang_str(const char *en)
{
    static const char *const MAP[][2] = {
        {"COLOR", "WARNA"}, {"LANG", "BAHASA"},
        {"DISPLAY", "LAYAR"}, {"CONTROL", "KONTROL"}, {"AUDIO", "SUARA"}, {"SYSTEM", "SISTEM"},
        {"ABOUT", "TENTANG"}, {"CALIBRATION", "KALIBRASI"},
        {"STYLE", "GAYA"}, {"LARGE", "BESAR"}, {"ANIM", "ANIMASI"}, {"LEDS", "LAMPU"},
        {"HOLD", "TAHAN"}, {"KNOB ACCEL", "AKSEL KNOB"}, {"FX LATCH", "KUNCI FX"},
        {"BPM LOCK", "KUNCI BPM"},
        {"USB LEVEL", "LEVEL USB"}, {"USB SERIAL", "SERIAL USB"},
    };
    uint32_t i;
    if (!(ui_prefs & PREF_LANG) || !en)
        return en;
    for (i = 0; i < NELEM(MAP); i++)
        if (str_eq(MAP[i][0], en))
            return MAP[i][1];
    return en;
}
typedef char mtab_fits_ui[sizeof ui.menu_row >= MTAB_COUNT ? 1 : -1];   /* (ui.c: the row last picked per tab) */
static uint32_t mtab_first(uint32_t t)                 /* a tab's first row */
{
    uint32_t i = 0;
    while (i < MI_COUNT && MI_TAB[i] != t)
        i++;
    return i;
}
static uint32_t mtab_rows(uint32_t t)                  /* .. and how many it has */
{
    uint32_t i, n = 0;
    for (i = 0; i < MI_COUNT; i++)
        n += MI_TAB[i] == t;
    return n;
}
/* STYLE (ui_style, gfx.c ST_*): FLAT the filled cards; LINE black areas divided by 1 px rules (#50, #57: the 0.9 look)
 * (1.0.2: PIXEL retired, a saved PIXEL reads as LINE) */
static const char *const STYLE_N[2] = {"FLAT", "LINE"};
/* the two-valued rows: a bit of ui_prefs and its names (bit clear = the default, bit set). A step up (a knob right,
 * OCT+) = ON on the ON / OFF rows (the switch's knob to the right; ANIM, USB SERIAL: their bit clears), else the
 * second name (STYLE LINE, USB LEVEL FIXED); a step down the other (menu_step) */
typedef struct { uint8_t row, bit; const char *name[2]; } menu_flag_t;
static const menu_flag_t MENU_FLAGS[] = {
    {MI_LARGE, PREF_LARGE, {"OFF", "ON"}},            /* #15 / Discussion #80: ON, big knob labels and values (ui.c large_kind) */
    {MI_ANIM, PREF_ANIM_OFF, {"ON", "OFF"}},          /* #46: OFF, values snap (ui_draw.c roll_note, ui_graph.c pr_follow) */
    {MI_ACCEL, PREF_ACCEL, {"OFF", "ON"}},            /* #52: ui_input.c accel */
    {MI_LATCH, PREF_LATCH, {"OFF", "ON"}},
    {MI_USB, PREF_USB_FIXED, {"MASTER", "FIXED"}},     /* fx.c fx_usb_fixed: FIXED, USB at the full level */
    {MI_BPMLOCK, PREF_BPM_LOCK, {"OFF", "ON"}},        /* #58: ON, SELECT sets the tempo with GLO held only (ui_input.c) */
    {MI_SERIAL, PREF_SERIAL_ON, {"OFF", "ON"}},        /* bit set: the debug port is on (usb_serial_apply) */
    {MI_LANG, PREF_LANG, {"EN", "ID"}},                /* bit 128: EN is 0, ID is Indonesian menu labels */
};
/* Master EQ (settings.lowcut, fx.c fx_lowcut): an EQ on the master output, the headphone
 * jack and USB audio together (one DAC, uac_tap reads the master). FLAT is stored as 0.
 * There is no built-in speaker. */
static const char *const SPK_EQ[3] = {"FLAT", "LOWCUT", "BASS+"};
/* MASTER (MENU > AUDIO > MASTER, the first row): the virtual master volume. The device has no fitted
 * master pot (the fader bank is MIDI), so this row is the master knob: the level is 0..1023 as the ADC
 * would read it, applied to the DAC/USB output (web/emu: k10 = adc >> 2; q12 = (k10*k10) >> 8). The VST
 * companion's settings-card fader drives the same row (sk_host_master -> web_master). The menu steps
 * MASTER_STEPS levels; the value shown is a percentage (menu_vname). Stored as a tagged byte in
 * favorites.factory[15][26] (a byte no engine uses; 0 in older settings = the full default), so it rides
 * along with every settings save and needs no record-format change. */
#define MASTER_BYTE (favorites.factory[15][26])
#define MASTER_TAG 0x30u                       /* high nibble: "a master level is stored here" */
#define MASTER_STEPS 16u                       /* the menu's levels: 0 .. 15 */
#define MASTER_DEF (MASTER_STEPS - 1u)         /* no level stored yet: full volume (4096), so an upgrade does not drop */
static const uint32_t MASTER_ADC[MASTER_STEPS] = {
    0u, 100u, 200u, 300u, 400u, 500u, 600u, 700u, 800u, 850u, 900u, 950u, 980u, 1000u, 1012u, 1023u };
static uint32_t master_from_stored(uint32_t v) { return (v & 0xF0u) == MASTER_TAG && (v & 0x0Fu) < MASTER_STEPS ? (v & 0x0Fu) : MASTER_DEF; }
static uint32_t master_to_stored(uint32_t i) { return MASTER_TAG | (i % MASTER_STEPS); }
static uint32_t master_index(void) { return master_from_stored(MASTER_BYTE); }
#if defined(FELUCCA_WEB) || defined(FELUCCA_SMK_PANEL)
void web_master(uint32_t v);                   /* the boot loop's MASTER ADC (web emulator) */
#endif
static void master_apply(uint32_t idx)
{
    uint32_t adc, k10;
    if (idx >= MASTER_STEPS)
        idx = MASTER_STEPS - 1u;
    adc = MASTER_ADC[idx];
    k10 = adc;                                 /* the web loop's k10 == the raw ADC 0..1023 */
#if defined(FELUCCA_WEB) || defined(FELUCCA_SMK_PANEL)
    web_master(adc);                           /* the loop computes song.master_q12 from this */
#else
    song.master_q12 = (k10 * k10) >> 8;        /* tests / device HAL: the same curve */
#endif
    (void)adc; (void)k10;
}
/* The virtual master gain (Q12), for the device main loop (main.c): there is no master pot, so this is what
 * the MASTER row (MENU > AUDIO) sets, and what the DAC/USB output follows. Full (4096) until a level is stored. */
uint32_t menu_master_q12(void)
{
    uint32_t k10 = MASTER_ADC[master_index()];
    return (k10 * k10) >> 8;
}
/* Applied once at boot so a saved level is live before the first frame (web power-on). */
void menu_master_boot(void) { master_apply(master_index()); }
static const char *const HOLD_N[4] = {"0.3 s", "0.4 s", "0.5 s", "0.6 s"};   /* (the editor's names; the menu draws its own) */

/* MENU > DEBUG PORT (stored as USB SERIAL, #67). A text console for development, over USB.
 * OFF (the default, and any saved setting that never turned it on) enumerates as audio + MIDI only, device class 0:
 * macOS then attaches its USB audio driver. ON presents the console too (Apple's CDC driver can hide that audio
 * input). USB-MIDI stays either way, and with it the editor and the update installer. Applied at boot before USB
 * starts, and when the menu closes. An editor change waits 200 ms (menu_serial_at) so its reply leaves first. */
static uint32_t menu_serial_at;                        /* fm1_ms | 1 of the editor's change; 0 none */
static void usb_serial_apply(void)
{
#if FELUCCA_CDC
    if (menu_serial_at && fm1_ms - menu_serial_at < 200u)
        return;
    menu_serial_at = 0;
    usb_cdc_switch(FELUCCA_CDC_DEFAULT && (ui_prefs & PREF_SERIAL_ON));   /* bit set: console on */
#endif
}
static const menu_flag_t *menu_flag(uint32_t row)
{
    uint32_t i;
    for (i = 0; i < NELEM(MENU_FLAGS); i++)
        if (MENU_FLAGS[i].row == row)
            return &MENU_FLAGS[i];
    return 0;
}

/* a row's value as 0..menu_n(row) - 1 in the order the menu steps it (LEDS: OFF DIM LO DIM HI INV, LEDS_MENU;
 * MASTER: the 16 levels of MASTER_ADC) */
static uint32_t menu_n(uint32_t row)
{
    return row == MI_COLOR ? NPALETTES : row == MI_LOWCUT ? 3u : row == MI_HOLD ? 4u : row == MI_LEDS ? LEDS_COUNT
         : row == MI_MASTER ? MASTER_STEPS : 2u;
}
/* MASTER's names: the level as a percentage of the top (0% .. 100%), so the menu and the editor show a number */
static const char *master_pct(uint32_t v)
{
    static char buf[6];
    uint32_t adc = MASTER_ADC[v % MASTER_STEPS];
    uint32_t pct = (adc * 100u + 511u) / 1023u;
    buf[0] = (char)('0' + (pct / 100u) % 10u);
    buf[1] = (char)('0' + (pct / 10u) % 10u);
    buf[2] = (char)('0' + pct % 10u);
    buf[3] = '%';
    buf[4] = 0;
    return pct >= 100u ? "100%" : (pct >= 10u ? buf + 1 : buf + 2);
}
static uint32_t menu_get(uint32_t row)
{
    const menu_flag_t *f = menu_flag(row);
    uint32_t i = 0;
    if (f)
        return (ui_prefs & f->bit) != 0;
    switch (row) {
    case MI_COLOR: return settings.palette % NPALETTES;
    case MI_MASTER: return master_index();
    case MI_STYLE: return ui_style == ST_LINE;
    case MI_LOWCUT: return settings.lowcut % 3u;
    case MI_RMAUD: return favorites.factory[15][27] ? 1u : 0u;
    case MI_HOLD: return settings_hold % 4u;
    case MI_LEDS:
        while (i + 1u < LEDS_COUNT && LEDS_MENU[i] != settings_leds)
            i++;
        return i;
    }
    return 0;
}
static const char *menu_vname(uint32_t row, uint32_t v)
{
    const menu_flag_t *f = menu_flag(row);
    if (f)
        return f->name[v & 1u];
    switch (row) {
    case MI_COLOR: return UI_PALETTES[v % NPALETTES].name;
    case MI_MASTER: return master_pct(v);
    case MI_STYLE: return STYLE_N[v & 1u];
    case MI_LOWCUT: return SPK_EQ[v % 3u];
    case MI_RMAUD: return v ? "ON" : "OFF";
    case MI_HOLD: return HOLD_N[v & 3u];
    case MI_LEDS: return LEDS_NAME[LEDS_MENU[v % LEDS_COUNT]];
    }
    return "";
}
/* set a row (v below menu_n(row)) and do what the menu does on a change: the palette, the EQ and USB LEVEL at once;
 * STYLE (ui_draw.c style_apply), LARGE, ANIM, LEDS, HOLD, the flags are read where they are used, every frame;
 * USB SERIAL when the menu has closed (usb_serial_apply). Saved by the caller (menu_close, the editor) */
#if defined(FELUCCA_SMK_PANEL)
void sk_on_palette_changed(uint32_t v);
void sk_on_lang_changed(uint32_t v);
#endif
#if FELUCCA_OTA
void skl_notify_palette(uint32_t v);
void skl_notify_lang(uint32_t v);
#endif

static void menu_put(uint32_t row, uint32_t v)
{
    const menu_flag_t *f = menu_flag(row);
    if (f) {
        ui_prefs = (uint8_t)(v ? ui_prefs | f->bit : ui_prefs & ~f->bit);
        fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;   /* (at once; every frame too: ui_input) */
        if (f->bit == PREF_LANG) {
#if defined(FELUCCA_SMK_PANEL)
            sk_on_lang_changed(v);
#endif
#if FELUCCA_OTA
            skl_notify_lang(v);
#endif
        }
        return;
    }
    switch (row) {
    case MI_COLOR:
        settings.palette = v;
        palette_set(v);                                /* (the menu signature redraws) */
        ui.force = 1;
#ifdef SK_DBG_HOST
        sk_host_dbg("P1", "menu_items.c:menu_put", "color_set", (long)v, (long)settings.palette, 0);
#endif
#if defined(FELUCCA_SMK_PANEL)
        sk_on_palette_changed(v);
#endif
#if FELUCCA_OTA
        skl_notify_palette(v);
#endif
        break;
    case MI_STYLE: ui_style = (uint8_t)(v ? ST_LINE : ST_FLAT); break;   /* (drawn so from the next frame) */
    case MI_MASTER: MASTER_BYTE = (uint8_t)master_to_stored(v); master_apply(v); break;
    case MI_LOWCUT: settings.lowcut = v; fx_lowcut = (uint8_t)v; break;
    case MI_RMAUD:
        favorites.factory[15][27] = (uint8_t)(v ? 1u : 0u);
        ui.force = 1;                                  /* redraw the switch while this row stays selected */
        break;
    case MI_HOLD: settings_hold = (uint8_t)v; break;
    case MI_LEDS: settings_leds = LEDS_MENU[v]; break;
    }
}
/* the menu's step, any of KNOB 1..4 or OCT+ (s > 0) / OCT- (s < 0): the next / previous value, stopping at the ends;
 * COLOR wraps round its palettes. On an ON / OFF row up is ON (MENU_FLAGS) */
static uint32_t menu_step(uint32_t row, int32_t s)
{
    const menu_flag_t *f = menu_flag(row);
    uint32_t v = menu_get(row), n = menu_n(row);
    if (f && str_eq(f->name[0], "ON"))                 /* (ANIM, USB SERIAL: ON is their value 0) */
        s = -s;
    if (s > 0)
        return v + 1u < n ? v + 1u : row == MI_COLOR ? 0u : v;
    if (s < 0)
        return v ? v - 1u : row == MI_COLOR ? n - 1u : 0u;
    return v;
}
/* The settings row stays "UPDATE" for the editor. The screen always shows both languages. */
static const char *menu_row_label(uint32_t row)
{
    if (row == MI_UPDATE)
        return (ui_prefs & PREF_LANG) ? "PERBAHARUI / UBOOT" : "UPGRADE / UBOOT";
    if (row == MI_RMAUD)
        return "AUDIO OUT ON REMOTE MODE";
    if (row == MI_SERIAL)
        return "DEBUG PORT";
    return lang_str(MI_NAME[row]);
}
/* Shown once UBOOT is actually entered (menu, the 5 s hold, or the USB request). */
static void uboot_screen(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 2, 240, &AF_M, "WARNING :", T_ACCENT, 1);
    draw_text_box(0, 22, 240, &AF_M, "UBOOT/Upgrade MODE.", T_THEME, 1);
    draw_text_box(0, 44, 240, &AF_S, "Entering by accident?", T_TEXT, 1);
    draw_text_box(0, 60, 240, &AF_S, "Exit by using power button (Restart).", T_TEXT, 1);
    draw_text_box(0, 78, 240, &AF_S, "DO NOT POWER OFF", T_ACCENT, 1);
    draw_text_box(0, 94, 240, &AF_S, "WHILE SENDING FIRMWARE.", T_ACCENT, 1);
    draw_text_box(0, 112, 240, &AF_S, "The upgrader will restart", T_TEXT, 1);
    draw_text_box(0, 128, 240, &AF_S, "your keyboard by itself.", T_TEXT, 1);
    draw_text_box(0, 148, 240, &AF_S, "Gak sengaja kesini?", T_TEXT, 1);
    draw_text_box(0, 164, 240, &AF_S, "Keluar pakai tombol power (Restart)", T_TEXT, 1);
    draw_text_box(0, 182, 240, &AF_S, "JANGAN EXIT KALAU LAGI", T_ACCENT, 1);
    draw_text_box(0, 198, 240, &AF_S, "UPGRADE FIRMWARE!", T_ACCENT, 1);
    draw_text_box(0, 216, 240, &AF_S, "Upgrader nanti restart keyboardnya sendiri.", T_TEXT, 1);
}
/* MENU > SYSTEM > UPDATE (OCT+): the same UBOOT entry as OCT- + OCT+ held in main.c. Does not return.
 * Host tests include this file without the device HAL, so the body stays empty there.
 * VST host (FELUCCA_SMK_PANEL): never enters UBOOT in-process. If the keyboard is CONNECTED,
 * ask the plugin to send the hardware UBOOT SysEx and show a disconnect notice; else roast. */
#if defined(FELUCCA_SMK_PANEL)
static uint32_t sk_uboot_roast_until;
static int sk_uboot_hw_mode;                             /* 1 = device-UBOOT notice (not roast) */
static int sk_uboot_hw_pending;                          /* plugin should forward UBOOT SysEx */
static void sk_uboot_roast_draw(void);
static void sk_uboot_roast_poll(void);
int sk_host_device_present(void);                       /* ui_draw.c — USB keyboard reachable */
#endif
static void menu_update(void)
{
#if defined(FELUCCA_SMK_PANEL)
    /* Arm roast / device-UBOOT notice only — never draw or flash here.
     * SETTINGS desk may call this on the UI thread; lcd_* + RebuildDesk races crash Ableton.
     * ui_draw → sk_uboot_roast_poll paints on the host render path. */
    sk_uboot_roast_until = fm1_ms + 5000u;
    sk_uboot_hw_mode = sk_host_device_present() ? 1 : 0;
    sk_uboot_hw_pending = sk_uboot_hw_mode;
    ui.menu = 0;
    ui.uboot = 0;
    ui.force = 1;
#elif defined(FELUCCA_ON_DEVICE)
    fm1_audio_stop();
    uboot_screen();
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;
    fm1_enter_uboot();
#endif
}
#if defined(FELUCCA_SMK_PANEL)
static void sk_uboot_roast_draw(void)
{
    int id = (ui_prefs & PREF_LANG) != 0;
    uint32_t left_ms, left_s;
    char foot[20];
    lcd_fill(0, 0, 240, 240, T_BG);
    if (sk_uboot_hw_mode) {
        draw_text_box(0, 36, 240, &AF_M, "DEVICE DISCONNECTED", T_ACCENT, 1);
        draw_text_box(0, 72, 240, &AF_S, "UBOOT MODE in device.", T_THEME, 1);
        if (id)
            draw_text_box(0, 100, 240, &AF_S, "Keyboard masuk UBOOT.", T_TEXT, 1);
        else
            draw_text_box(0, 100, 240, &AF_S, "Hardware entered UBOOT.", T_TEXT, 1);
    } else {
        draw_text_box(0, 4, 240, &AF_M, id ? "UBOOT DITOLAK" : "UBOOT REJECTED", T_ACCENT, 1);
        if (id) {
            draw_text_box(0, 28, 240, &AF_S, "lu ngapain anjir mau UBOOT", T_THEME, 1);
            draw_text_box(0, 44, 240, &AF_S, "disini? disana tuh di", T_THEME, 1);
            draw_text_box(0, 60, 240, &AF_S, "keyboard beneran nya!", T_THEME, 1);
            draw_text_box(0, 88, 240, &AF_S, "untuk developer, ini BUKAN", T_TEXT, 1);
            draw_text_box(0, 104, 240, &AF_S, "emulator JieLi AC791N", T_TEXT, 1);
            draw_text_box(0, 120, 240, &AF_S, "beneran, jadi jangan", T_TEXT, 1);
            draw_text_box(0, 136, 240, &AF_S, "ngerusuh disini yah.", T_TEXT, 1);
        } else {
            draw_text_box(0, 28, 240, &AF_S, "why the fuck you wanted", T_THEME, 1);
            draw_text_box(0, 44, 240, &AF_S, "UBOOT on VST? go do that", T_THEME, 1);
            draw_text_box(0, 60, 240, &AF_S, "on your hardware!", T_THEME, 1);
            draw_text_box(0, 88, 240, &AF_S, "for developers: this WAS", T_TEXT, 1);
            draw_text_box(0, 104, 240, &AF_S, "NOT a 1:1 JieLi AC791N", T_TEXT, 1);
            draw_text_box(0, 120, 240, &AF_S, "emulator. go have fun", T_TEXT, 1);
            draw_text_box(0, 136, 240, &AF_S, "somewhere else.", T_TEXT, 1);
        }
    }
    /* ASCII only — font has no ellipsis / arrow (those showed as ????) */
    left_ms = sk_uboot_roast_until > fm1_ms ? sk_uboot_roast_until - fm1_ms : 0;
    left_s = (left_ms + 999u) / 1000u;
    if (left_s > 5u)
        left_s = 5u;
    foot[0] = (char)('0' + (left_s % 10u));
    foot[1] = 's';
    foot[2] = ' ';
    foot[3] = '-';
    foot[4] = '>';
    foot[5] = ' ';
    foot[6] = 'M';
    foot[7] = 'E';
    foot[8] = 'N';
    foot[9] = 'U';
    foot[10] = 0;
    draw_text_box(0, 200, 240, &AF_S, foot, T_MID, 1);
}
static void sk_uboot_roast_poll(void)
{
    if (!sk_uboot_roast_until)
        return;
    if ((int32_t)(fm1_ms - sk_uboot_roast_until) < 0) {
        sk_uboot_roast_draw();
        return;
    }
    sk_uboot_roast_until = 0;
    sk_uboot_hw_mode = 0;
    ui.uboot = 0;
    ui.menu = 1;                                        /* back to SETTINGS / MENU, not HOME */
    ui.home = 0;
    ui.force = 1;
}

/* —— host peek/poke for the SETTINGS desk (same rows as the OLED MENU) —— */
uint32_t sk_host_menu_count(void) { return MI_COUNT; }
uint32_t sk_host_menu_values(void) { return MI_VALUES; }
uint32_t sk_host_menu_tab_count(void) { return MTAB_COUNT; }
uint32_t sk_host_menu_tab_of(uint32_t row) { return row < MI_COUNT ? MI_TAB[row] : 0u; }
const char *sk_host_menu_tab_name(uint32_t t)
{
    return t < MTAB_COUNT ? lang_str(MTAB_NAME[t]) : "";
}
const char *sk_host_menu_row_label(uint32_t row)
{
    return row < MI_COUNT ? menu_row_label(row) : "";
}
uint32_t sk_host_menu_n(uint32_t row) { return row < MI_COUNT ? menu_n(row) : 0u; }
uint32_t sk_host_menu_get(uint32_t row) { return row < MI_COUNT ? menu_get(row) : 0u; }
const char *sk_host_menu_vname(uint32_t row, uint32_t v)
{
    return row < MI_COUNT ? menu_vname(row, v) : "";
}
void sk_host_menu_put(uint32_t row, uint32_t v)
{
    if (row >= MI_VALUES)
        return;
    if (v >= menu_n(row))
        v = menu_n(row) - 1u;
    menu_put(row, v);
    /* Do not settings_save() here — UI thread; flash/persist races Ableton audio.
     * Locked continuous-save / next MENU close persists prefs. */
    ui.force = 1;
}
int sk_host_uboot_hw_take(void)
{
    int v = sk_uboot_hw_pending;
    sk_uboot_hw_pending = 0;
    return v;
}
void sk_host_menu_uboot(void) { menu_update(); }
void sk_host_about_open(void)
{
    ui.menu = 2;
    ui.home = 0;
    ui.confirm = 0;
    ui.force = 1;
    song.seq_mode = 0;
}
void sk_host_about_close(void)
{
    if (ui.menu >= 2) {
        ui.menu = 1;
        ui.force = 1;
    }
}
int sk_host_about_is_open(void) { return ui.menu >= 2; }
#endif
