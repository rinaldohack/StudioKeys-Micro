/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* StudioKEYS Micro menu (HOME held): its rows in tabs (1.0.5: DISPLAY CONTROL AUDIO SYSTEM; ALGORITHM between them, PRESETS
 * within one; no scrolling), the rows in menu_items.c (the settings, shared with the editor's MENU_DESC / MENU_SET).
 * No BACK row: HOME closes it (ui_input.c). The two-valued rows are bits of ui_prefs (MENU_FLAGS). PRESETS scrolls from
 * ABOUT through all credits (OCT- back to the tabs). ui.menu: 1 the tabs, 2 information. */
/* ------------------------------------------------------------ menu --- */
/* (the rows, their values and names, usb_serial_apply: menu_items.c) */

/* All notices are in flash. Wrap at the font's actual width, without dropping URL
 * characters; the same layout computes the scroll limit and draws clipped bands.
 * Mixed case here (long text); S throughout, the title in M. */
#define MENU_DOC_H 196                                /* the document: screen rows 24..220 */
#include "lc_logo.h"
enum { MC_TEXT, MC_TITLE, MC_URL };
typedef struct { const char *text; uint8_t kind; } menu_credit_t;
static const menu_credit_t MENU_CREDITS[] = {
    {"LangitCinta! StageLabs by Rinaldo.", MC_TITLE},
    {"StudioKEYS Micro", MC_TITLE},
    {"No synth under USD 1k were \"fun enough\".", MC_TEXT},
    {"So here you go.", MC_TEXT},
    {"Assisted coding with Gemini (Google),", MC_TEXT},
    {"Grok (X), and Claude (Anthropic).", MC_TEXT},
    {"This is what you got from strong", MC_TEXT},
    {"Computer Engineering degree,", MC_TEXT},
    {"with strong music background, I guess ...", MC_TEXT},
    {"So, enjoy my love letter.", MC_TEXT},
    {"", MC_TEXT},
    {"Ableton Live", MC_TITLE},
    {"The DAW I made my food from.", MC_TEXT},
    {"Love you guys.", MC_TEXT},
    {"", MC_TEXT},
    {"Mackie HUI", MC_TITLE},
    {"Jurgen Mossgraber, DrivenByMoss.", MC_TEXT},
    {"LGPL-3.0.", MC_TEXT},
    {"https://github.com/git-moss/DrivenByMoss", MC_URL},
    {"", MC_TEXT},
    {"BAUD GIRL", MC_TITLE},
    {"Madeline. The firmware that", MC_TEXT},
    {"inspired me the most.", MC_TEXT},
    {"https://baudgirl.com", MC_URL},
    {"", MC_TEXT},
    {"ENGINE LINEAGE", MC_TITLE},
    {"Leo Kuroshita / Hügelton (GPL-3.0).", MC_TEXT},
    {"Upstream project name retained for license.", MC_TEXT},
    {"", MC_TEXT},
    {"M-VAVE", MC_TITLE},
    {"Beautiful hardware,", MC_TEXT},
    {"beats everyone on the price.", MC_TEXT},
    {"But the software ... yeah.", MC_TEXT},
    {"That's why I'm here.", MC_TEXT},
    {"", MC_TEXT},
    {"CrispyZebra (PHASE waveforms), the DRUM voices, the Sample Pack: GPL-3.0", MC_TEXT},
    {"Fukiai icon font: MIT", MC_TEXT},
    {"", MC_TEXT},
    {"KLATTSCH", MC_TITLE},
    {"Tony Gies / MIT", MC_TEXT},
    {"https://github.com/tgies/klattsch", MC_URL},
    {"", MC_TEXT},
    {"DAISYSP", MC_TITLE},
    {"Electrosmith, Emilie Gillet", MC_TEXT},
    {"MIT", MC_TEXT},
    {"https://github.com/electro-smith/DaisySP", MC_URL},
    {"", MC_TEXT},
    {"DEXED + MSFA", MC_TITLE},
    {"Pascal Gauthier, Google Inc.", MC_TEXT},
    {"msfa: Apache-2.0, Dexed: GPL-3.0", MC_TEXT},
    {"https://github.com/asb2m10/dexed", MC_URL},
    {"", MC_TEXT},
    {"RINGS", MC_TITLE},
    {"Emilie Gillet / MIT", MC_TEXT},
    {"https://github.com/pichenettes/eurorack", MC_URL},
    {"", MC_TEXT},
    {"INTER TIGHT", MC_TITLE},
    {"The Inter Project Authors", MC_TEXT},
    {"SIL Open Font License 1.1", MC_TEXT},
    {"https://github.com/rsms/inter-tight", MC_URL},
    {"", MC_TEXT},
    {"VSCO-2 CE + VCSL", MC_TITLE},
    {"Versilian Studios / CC0 1.0", MC_TEXT},
    {"https://github.com/sgossner/VSCO-2-CE", MC_URL},
    {"https://github.com/sgossner/VCSL", MC_URL},
    {"", MC_TEXT},
    {"VOICE", MC_TITLE},
    {"Dennis H. Klatt (1980)", MC_TEXT},
    {"https://doi.org/10.1121/1.383940", MC_URL},
    {"Hillenbrand, Getty, Clark and Wheeler (1995)", MC_TEXT},
    {"https://doi.org/10.1121/1.411872", MC_URL},
};

#if defined(SKM_SMK_PANEL)
/* Companion About modal — same document as menu_document() / OLED ABOUT. */
enum { SK_AC_TEXT = 0, SK_AC_TITLE = 1, SK_AC_URL = 2, SK_AC_BLANK = 3 };
uint32_t sk_host_about_line_count(void)
{
    /* header(5) + CREDITS + credits + footer(5) */
    return 5u + 1u + (uint32_t)NELEM(MENU_CREDITS) + 5u;
}
const char *sk_host_about_line(uint32_t i, uint8_t *kind_out)
{
    static char ver[48];
    const uint32_t nCred = (uint32_t)NELEM(MENU_CREDITS);
    uint8_t kind = SK_AC_TEXT;
    const char *text = "";
    if (i == 0u) { text = "StudioKEYS Micro"; kind = SK_AC_TITLE; }
    else if (i == 1u) {
        snprintf(ver, sizeof ver, "%s public release.", SKM_VERSION);
        text = ver;
        kind = SK_AC_TITLE;
    }
    else if (i == 2u) { text = __DATE__; kind = SK_AC_TEXT; }
    else if (i == 3u) { text = "Use with StudioKEYS companion VST only."; kind = SK_AC_TEXT; }
    else if (i == 4u) { text = "GPL-3.0-only"; kind = SK_AC_TEXT; }
    else if (i == 5u) { text = "CREDITS"; kind = SK_AC_TITLE; }
    else if (i < 6u + nCred) {
        const menu_credit_t *c = &MENU_CREDITS[i - 6u];
        text = c->text;
        if (!text[0])
            kind = SK_AC_BLANK;
        else if (c->kind == MC_TITLE)
            kind = SK_AC_TITLE;
        else if (c->kind == MC_URL) {
            kind = SK_AC_URL;
            if (str_len(text) >= 8u && !memcmp(text, "https://", 8))
                text += 8;
        } else
            kind = SK_AC_TEXT;
    } else {
        uint32_t f = i - (6u + nCred);
        if (f == 0u) { text = "LICENSES + SOURCE"; kind = SK_AC_TITLE; }
        else if (f == 1u) text = "DRUM + own samples: GPL-3.0-only";
        else if (f == 2u) text = "Fonts and icons: their licences travel with the source and the release files.";
        else if (f == 3u) text = "NO WARRANTY";
        else text = "Full notices in source.";
    }
    if (kind_out)
        *kind_out = kind;
    return text;
}
#endif

static int32_t menu_text(const char *s, int32_t y, uint16_t col, int draw)
{
    while (*s) {
        char line[64];
        uint32_t n = 0, space = 0, take, prev = 0;
        int32_t pen = 0;                                 /* 1/16 px, as text_w adds it up */
        while (s[n] && n < sizeof line - 1u) {
            uint32_t c = (uint8_t)s[n];
            line[n] = s[n];
            line[n + 1u] = 0;
            pen += (prev ? kern(&AF_S, prev, c) : 0) + AF_S.g[glyph(&AF_S, c)].adv;
            prev = c;
            if ((pen + 8) >> 4 > 226)
                break;
            if (s[n] == ' ')
                space = n;
            n++;
        }
        take = s[n] && space ? space : n;
        if (!take) take = 1;                         /* progress even for an unusually wide glyph */
        line[take] = 0;
        if (draw && y + AF_S.h > 0 && y < MENU_DOC_H)
            cv_text(6, y, &AF_S, line, col);
        y += 18;
        s += take;
        while (*s == ' ') s++;
    }
    return y;
}

/* the LangitCinta! mark, drawn in white and centred; the height is reserved even when measuring the scroll */
static int32_t menu_logo(int32_t y, int draw)
{
    if (draw) {
        int32_t x = (240 - (int32_t)LC_LOGO_W) / 2;
        cv_alpha(x, y, LC_LOGO_W, LC_LOGO_H, LC_LOGO, ramp(0xFFFF, T_BG));
    }
    return y + (int32_t)LC_LOGO_H + 8;
}

static int32_t menu_document(int32_t y, int draw)
{
    uint32_t i;
    if (draw) {
        cv_text(6, y + 2, &AF_M, "StudioKEYS Micro", T_THEME);
        cv_text(6, y + 24, &AF_M, SKM_VERSION " public release.", T_THEME);
        cv_text_r(232, y + 27, &AF_S, __DATE__, T_MID, T_BG);
        cv_text(6, y + 46, &AF_S, "Use with StudioKEYS companion VST only.", T_TEXT);
        cv_text(6, y + 64, &AF_S, "GPL-3.0-only", T_TEXT);
    }
    y = menu_text("CREDITS", y + 90, T_THEME, draw) + 8;
    y = menu_logo(y, draw);
    for (i = 0; i < NELEM(MENU_CREDITS); i++) {
        const menu_credit_t *c = &MENU_CREDITS[i];
        const char *text = c->text;
        /* Display host/path once; the full source URL stays in the flash table. */
        if (c->kind == MC_URL && str_len(text) >= 8u && !memcmp(text, "https://", 8))
            text += 8;
        if (!text[0]) y += 8;
        else y = menu_text(text, y, c->kind == MC_TITLE ? T_THEME : c->kind == MC_URL ? T_MID : T_TEXT, draw);
    }
    y = menu_text("LICENSES + SOURCE", y + 12, T_THEME, draw);
    y = menu_text("DRUM + own samples: GPL-3.0-only", y, T_TEXT, draw);
    y = menu_text("Fonts and icons: their licences travel with the source and the release files.", y, T_TEXT, draw);
    y = menu_text("NO WARRANTY", y, T_TEXT, draw);
    y = menu_text("Full notices in source.", y + 8, T_TEXT, draw);
    return y + 4;
}
static int32_t menu_scroll_max(void)
{
    int32_t h = menu_document(0, 0);
    return h > MENU_DOC_H ? h - MENU_DOC_H : 0;
}

static void menu_dots(void);
/* the menu's header (0..24): its icon and title, the REC mark, the way back (the tabs: HOME CLOSE before the dots) */
static void menu_head(void)
{
    cv_begin(240, H_HEAD, T_BG);
    GFX_HOOK_ALIGN(0, HEAD_MY + AF_M_CAP_Y, 0, HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V, "header icon on its title's line");
    cv_icon_mid(8, H_HEAD / 2, 16, ui.menu == 2 ? ICON_X_INFO : ICON_X_COG, T_THEME, T_BG);   /* (as draw_head's) */
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    cv_text(30, HEAD_MY, &AF_M, ui.menu == 2 ? "ABOUT / CREDITS" : "MENU", T_TEXT);
    if (ui.menu >= 2) {                               /* the way back at the right, the REC mark before it (then */
        const char *w = song.rec ? 0 : "BACK";        /* the keycap goes without its word: no room for both) */
        int32_t x = 232 - kh_w(KC_OCTDN, w);
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_key_hint(x, HALF_UP(H_HEAD - KC_H), KC_OCTDN, w, 1, T_BG);
        draw_rec_mark(x - 20, T_BG);
    } else {
        int32_t dots = 12 + (int32_t)(MTAB_COUNT - 1u) * 8;
        int32_t x = 232 - dots - 8 - kh_w(KC_HOME, "CLOSE");
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_key_hint(x, HALF_UP(H_HEAD - KC_H), KC_HOME, "CLOSE", 1, T_BG);
        draw_rec_mark(x - 20, T_BG);
        menu_dots();
    }
    cv_blit(0, Y_HEAD);
}

/* The page (1.0.5): the tabs across the top, the rows of the one shown below, the key hints at the bottom.
 *   header   0..24  the cog and MENU, the REC mark, HOME CLOSE, the tabs as dots at the right (the one shown a THEME
 *                   pill)
 *   tabs    29..53  a 24 px cushion per tab: its icon alone (SURF, MID); the one shown wider, with its name (ACCENT,
 *                   INK; S, LARGE M), MT_GAP between any two. With ANIM ON the cushions grow and shrink into place
 *                   (mt_follow), the gaps kept
 *   rows    59..    one SURF panel (FLAT; LINE: none, a rule under the tabs), a row a label and its value, 1 px rules
 *                   between them; the selected row THEME with INK, as the browser's. A value THEME (M) at the right,
 *                   ON / OFF as a switch, COLOR its palette's five colours, CALIBRATION, ABOUT and UPDATE a chevron
 *                   (OCT+ opens them). LARGE: 28 px rows (a tab of up to 5), the labels in M
 *   keys   224      K1 LIST, K2 SET, K3 TAB (a row that opens: OCT+ OPEN). HOME CLOSE is a pill in the header.
 * Drawn in two bands (the canvas holds 124 rows) split in a gap between two rows; while the tabs slide only their
 * strip (24..58) is drawn again */
static const uint16_t MTAB_ICON[MTAB_COUNT] = {ICON_X_EYE, ICON_X_KNOB, ICON_X_SPEAKER, ICON_X_COG};
#define MT_Y 29                                        /* the tab bar */
#define MT_H 24
#define MT_IW 32                                       /* a cushion with its icon alone (the icon centred in it) */
#define MT_NX 28                                       /* the name's pen in the one shown; 10 px after it */
#define MT_X0 6
#define MT_X1 234
#define MT_GAP 8                                       /* between two tabs, always (the bar centred in MT_X0..MT_X1) */
#define MT_ONE 64                                      /* mt.pos per tab */
#define MP_Y 59                                        /* the rows' panel */
#define MP_PAD 3
#define MR_X 8                                         /* a row */
#define MR_W 224
#define MR_LX 16                                       /* its label */
#define MR_VR 224                                      /* its value's right edge */
#define MK_Y 224                                       /* the key hints */
static int menu_large(void) { return (ui_prefs & PREF_LARGE) != 0u; }
static uint32_t menu_tab(void) { return MI_TAB[ui.menu_sel % MI_COUNT]; }
/* the shown tab's rows: 24 px (LARGE 28, a tab of up to 5 rows). Six rows sit 1 px apart so the
 * panel still clears the key hints. */
static int32_t mr_h(void) { return menu_large() && mtab_rows(menu_tab()) <= 5u ? 28 : 24; }
static int32_t mr_gap(void) { return mtab_rows(menu_tab()) > 5u ? 1 : 2; }
static int32_t mr_y(uint32_t k) { return MP_Y + MP_PAD + (int32_t)k * (mr_h() + mr_gap()); }

/* the tab bar's place: the tab shown at pos tab * MT_ONE; with ANIM ON it follows in halving steps, a frame each
 * (a frame missed, as when the menu opens: it snaps, as ui_graph.c pr_follow) */
static struct { int16_t pos; uint32_t frame; } mt;
static void mt_follow(void)
{
    int32_t want = (int32_t)menu_tab() * MT_ONE, d = want - mt.pos;
    if (ui.frame != mt.frame + 1u || (ui_prefs & PREF_ANIM_OFF) || (d < 3 && d > -3))
        mt.pos = (int16_t)want;
    else
        mt.pos = (int16_t)(mt.pos + d / 2);
    mt.frame = ui.frame;
}
static const aafont_t *mt_font(void) { return menu_large() ? &AF_M : &AF_S; }
/* the tabs at the place mt.pos, MT_GAP apart, centred: each one's share of the shown look (0 .. MT_ONE), x, width */
static void mt_layout(int32_t *a, int32_t *x, int32_t *w)
{
    uint32_t k;
    int32_t tot = 0, cum = 0;
    for (k = 0; k < MTAB_COUNT; k++) {
        int32_t d = mt.pos - (int32_t)k * MT_ONE;
        a[k] = MT_ONE - (d < 0 ? -d : d);
        if (a[k] < 0) a[k] = 0;
        w[k] = MT_IW + (MT_NX - MT_IW + text_w(mt_font(), lang_str(MTAB_NAME[k])) + 10) * a[k] / MT_ONE;
        tot += w[k];
    }
    cum = MT_X0 + HALF_UP(MT_X1 - MT_X0 - tot - (int32_t)(MTAB_COUNT - 1u) * MT_GAP);   /* (the bar centred) */
    for (k = 0; k < MTAB_COUNT; k++) {                 /* the same gap between every two, sliding too */
        x[k] = cum;
        cum += w[k] + MT_GAP;
    }
}
static void menu_tabs(void)                            /* (in screen rows: cv_oy set by the caller) */
{
    int32_t a[MTAB_COUNT], x[MTAB_COUNT], w[MTAB_COUNT];
    uint32_t k;
    mt_layout(a, x, w);
    if (ux.style)                                      /* LINE: a rule under the tabs */
        cv_rule(4, MP_Y - 3, 232, 1);
    for (k = 0; k < MTAB_COUNT; k++) {
        int on = k == (uint32_t)((mt.pos + MT_ONE / 2) / MT_ONE);   /* (half way, the shown look moves over) */
        uint16_t bg = on ? T_ACCENT : T_SURF;
        cv_rrect(x[k], MT_Y, w[k], MT_H, -8, bg, T_BG);
        GFX_HOOK_ALIGN(x[k], MT_Y, x[k] + MT_IW, MT_Y + MT_H, AL_HV, "menu tab icon centred in its cushion");
        cv_icon_in(x[k], MT_Y, MT_IW, MT_H, 16, MTAB_ICON[k], on ? T_INK : T_MID, bg);
        if (a[k] == MT_ONE) {                          /* (settled: its name) */
            GFX_HOOK_ALIGN(0, MT_Y, 0, MT_Y + MT_H, AL_V, "menu tab name centred up/down");
            cv_text_on(x[k] + MT_NX, MT_Y + (menu_large() ? CAP_IN(M, MT_H) : CAP_IN(S, MT_H)), mt_font(), lang_str(MTAB_NAME[k]),
                       T_INK, T_ACCENT);
        }
    }
}
/* the header's tab dots at the right, the shown tab's a THEME pill */
static void menu_dots(void)
{
    uint32_t k, t = menu_tab();
    int32_t x = 232 - (12 + (int32_t)(MTAB_COUNT - 1u) * 8);
    for (k = 0; k < MTAB_COUNT; k++) {
        cv_rrect(x, H_HEAD / 2 - 2, k == t ? 12 : 4, 4, -2, k == t ? T_THEME : T_DIM, T_BG);
        x += (k == t ? 12 : 4) + 4;
    }
}

/* a two-valued row whose values are ON and OFF: a switch (a 26 x 14 cushion, its knob at the right when ON) */
static void menu_switch(int32_t xr, int32_t y, int32_t h, int on, int sel)
{
    int32_t x = xr - 26, ty = y + HALF_UP(h - 14);
    uint16_t under = sel ? T_THEME : T_SURF;
    uint16_t track = sel ? (on ? T_INK : ux_mix(T_THEME, T_INK, 35)) : on ? T_THEME : T_DIM;
    uint16_t knob = sel ? (on ? T_THEME : T_INK) : on ? T_INK : T_TEXT;
    cv_rrect(x, ty, 26, 14, -7, track, under);
    cv_rrect(on ? x + 14 : x + 2, ty + 2, 10, 10, -5, knob, track);
}
/* the tab's row k (menu_items.c row `row`) */
static void menu_row(uint32_t k, uint32_t row)
{
    int32_t y = mr_y(k), h = mr_h(), large = menu_large();
    int32_t vy = y + CAP_IN(M, h);
    int sel = row == ui.menu_sel;
    uint16_t bg = sel ? T_THEME : T_SURF, fg = sel ? T_INK : T_TEXT, val = sel ? T_INK : T_THEME;
    const char *v = row < MI_VALUES ? menu_vname(row, menu_get(row)) : "";
    if (sel)
        cv_rrect(MR_X, y, MR_W, h, 6, T_THEME, T_SURF);
    else if (k && row - 1u != ui.menu_sel)             /* a rule from the row above (none beside the selected one) */
        cv_rect(MR_X + 6, y - 1, MR_W - 12, 1, ux.style ? T_RULE : T_LINE);
    GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row name centred up/down");
    {
        const char *label = menu_row_label(row);
        const aafont_t *lf = !large || text_w(&AF_M, label) > 170 ? &AF_S : &AF_M;
        int32_t ny = y + (lf == &AF_M ? CAP_IN(M, h) : CAP_IN(S, h));
        cv_text_on(MR_LX, ny, lf, label, fg, bg);
    }
    if (row >= MI_VALUES) {                            /* CALIBRATION, ABOUT, UPDATE: OCT+ opens them */
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row chevron centred up/down");
        cv_icon_in(MR_VR - 12, y, 0, h, 12, ICON_X_RIGHT, sel ? T_INK : T_MID, bg);
        return;
    }
    if (row == MI_HOLD) {                              /* "0.4" and its unit */
        char b[8] = "0.4";
        b[2] = (char)('0' + HOLD_MS[settings_hold % 4u] / 100u);
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row value centred up/down");
        GFX_HOOK_ALIGN(0, 0, 0, vy + AF_M.asc, AL_B, "menu row unit on its value's baseline");
        cv_text_r(cv_text_r(MR_VR, vy + AF_M.asc - AF_S.asc, &AF_S, "s", sel ? T_INK : T_MID, bg) - 3, vy, &AF_M, b, val, bg);
        return;
    }
    if (menu_n(row) == 2u && (str_eq(v, "ON") || str_eq(v, "OFF"))) {
        menu_switch(MR_VR, y, h, str_eq(v, "ON"), sel);
        return;
    }
    GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row value centred up/down");
    if (row != MI_COLOR) {
        cv_text_r(MR_VR, vy, &AF_M, v, val, bg);
        return;
    }
    {   /* COLOR: the palette's name and its five colours (BG SURF TEXT THEME ACCENT), live */
        uint32_t c;
        const uint16_t *tok = &T_BG;
        int32_t x0 = MR_VR - 74, sy = y + HALF_UP(h - 12);
        cv_text_r(x0 - 6, vy, &AF_M, v, val, bg);
        for (c = 0; c < 5u; c++) {
            cv_rrect(x0 + (int32_t)c * 15, sy, 12, 12, -3, T_RAISE, bg);       /* a rim for the dark ones */
            cv_rrect(x0 + 1 + (int32_t)c * 15, sy + 1, 10, 10, -2, tok[c], T_RAISE);
        }
    }
}
/* the rows' two bands meet at `split`: in the gap above a row (or below the last), each band at most 124 rows */
static uint32_t menu_split(uint32_t n)
{
    int32_t s = mr_y(n) - 1;
    uint32_t k = n;
    while (s > H_HEAD + 124 && k > 1u)
        s = mr_y(--k) - 1;
    return (uint32_t)(s < 240 - 124 ? 240 - 124 : s);
}
static void draw_menu(void)
{
    uint32_t pass, k, first, n, split, sig;
    int32_t pos0 = mt.pos;
    if (ui.menu == 1)
        mt_follow();
    sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u + settings_hold * 3511u +
          settings_leds * 6151u + ui_prefs * 4099u + ui_style * 257u + song.rec * 65537u + song.sel * 13u +
          favorites.factory[15][27] * 8191u + settings_metro * 12289u + settings_countin * 2053u +
          (ui.menu == 2 ? ui.menu_scroll * 48611u : 0u);
    if (!ui.force && sig == ui.menu_sig) {
        if (ui.menu == 1 && mt.pos != pos0) {          /* only the tabs slid: their strip */
            cv_begin(240, MP_Y - H_HEAD, T_BG);
            cv_oy = -H_HEAD;
            menu_tabs();
            cv_oy = 0;
            cv_blit(0, H_HEAD);
        }
        return;
    }
    ui.menu_sig = sig;
    menu_head();
    if (ui.menu >= 2) {
        int32_t scroll = ui.menu_scroll, max = menu_scroll_max();
        int32_t thumb = MENU_DOC_H * MENU_DOC_H / (max + MENU_DOC_H);
        if (thumb < 12) thumb = 12;
        for (pass = 0; pass < 2u; pass++) {
            cv_begin(240, pass ? MENU_DOC_H - 124u : 124u, T_BG);
            cv_oy = pass ? -124 : 0;
            cv_scroll = 1;
            menu_document(-scroll, 1);
            cv_scroll = 0;
            cv_rrect(235, 2, 3, MENU_DOC_H - 4, 1, T_LINE, T_BG);           /* the scroll bar */
            cv_rrect(235, 2 + (max ? scroll * (MENU_DOC_H - 4 - thumb) / max : 0), 3, thumb, 1, T_MID, T_LINE);
            cv_oy = 0;
            cv_blit(0, H_HEAD + pass * 124u);
        }
        cv_begin(240, 240 - (H_HEAD + MENU_DOC_H), T_BG);
        {   /* the pill, then what it does: K1 scrolls the page, OCT- leaves it */
            khint_t h[2];
            h[0] = (khint_t){KC_SELECT, "SCROLL"};
            h[1] = (khint_t){KC_OCTDN, "BACK"};
            cv_key_row(6, 234, (int32_t)(240 - (H_HEAD + MENU_DOC_H) - KC_H) / 2, h, 2, 3u, T_BG);
        }
        cv_blit(0, H_HEAD + MENU_DOC_H);
        return;
    }
    first = mtab_first(menu_tab());
    n = mtab_rows(menu_tab());
    split = menu_split(n);
    for (pass = 0; pass < 2u; pass++) {
        int32_t top_y = pass ? (int32_t)split : H_HEAD;
        cv_begin(240, pass ? 240u - split : split - H_HEAD, T_BG);
        cv_oy = -top_y;                               /* drawn in screen rows */
        if (!pass)
            menu_tabs();
        if (!ux.style)                                /* FLAT: the rows' panel */
            cv_rrect(4, MP_Y, 232, mr_y(n) - 2 + MP_PAD - MP_Y, 8, T_SURF, T_BG);
        for (k = 0; k < n; k++)
            if (mr_y(k) + mr_h() > top_y && mr_y(k) - 1 < top_y + (int32_t)cv_h)
                menu_row(k, first + k);
        if (pass) {
            khint_t h[3];
            h[0] = (khint_t){KC_SELECT, "LIST"};
            h[1] = (khint_t){KC_ALGO, "SET"};
            h[2] = ui.menu_sel >= MI_VALUES ? (khint_t){KC_OCTUP, "OPEN"} : (khint_t){KC_K1, "TAB"};
            cv_key_row(6, 234, MK_Y, h, 3, 7u, T_BG);
        }
        cv_oy = 0;
        cv_blit(0, (uint32_t)top_y);
    }
}
static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    settings_save();                                   /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* SMK-37: the three knobs you need sit next to each other.
 * Knob 1 moves the row, knob 2 sets it, knob 3 changes the tab.
 * OCT+ opens CALIBRATION / ABOUT / UPDATE. OCT- leaves ABOUT. Knob Bank closes.
 * Host tests keep PRESETS for the rows and knobs 3..6 plus OCT for the value. */
static int32_t menu_list_turn(void)
{
#if defined(SKM_ON_DEVICE) || defined(SKM_SMK_PANEL)
    /* Physical encoder 0 = KNOB 1 LIST (ignore panel role remap). */
    return fm1_enc_take(0);
#else
    return panel_enc(EN_PRESET);
#endif
}
static void menu_move(int32_t s)                       /* PRESETS: the next / previous row of the tab */
{
    uint32_t t = menu_tab(), f = mtab_first(t), n = mtab_rows(t), k = (ui.menu_sel - f + (s > 0 ? 1u : n - 1u)) % n;
    ui.menu_sel = (uint8_t)(f + k);
    ui.menu_row[t] = (uint8_t)k;
}
static void menu_tab_go(int32_t s)                     /* ALGORITHM: the next / previous tab, at its last row */
{
    uint32_t t = menu_tab(), k;
    ui.menu_row[t] = (uint8_t)(ui.menu_sel - mtab_first(t));
    t = (t + (s > 0 ? 1u : MTAB_COUNT - 1u)) % MTAB_COUNT;
    k = ui.menu_row[t] < mtab_rows(t) ? ui.menu_row[t] : 0u;
    ui.menu_sel = (uint8_t)(mtab_first(t) + k);
}
static void menu_input(uint32_t oct)                  /* oct: ui_input.c oct_taps */
{
    int32_t s;
    uint32_t k, up = (oct >> 1) & 1u, dn = oct & 1u;
    if (ui.menu >= 2) {                                /* ABOUT: PRESETS scrolls (bounded), OCT- back to the tabs */
        if (dn)
            ui.menu = 1, ui.force = 1;
        else if ((s = menu_list_turn()) != 0)
            ui.menu_scroll = (uint16_t)clamp((int32_t)ui.menu_scroll + clamp(s, -128, 128) * 18, 0, menu_scroll_max());
        enc_drop();
        return;
    }
    if ((s = menu_list_turn()) != 0)
        menu_move(s);
#if defined(SKM_ON_DEVICE) || defined(SKM_SMK_PANEL)
    /* Footer keymap: K1 LIST, K2 SET, K3 TAB — physical encoders 0,1,2. */
    s = fm1_enc_take(1);                               /* knob 2: SET */
    if ((k = (uint32_t)fm1_enc_take(2)) != 0)          /* knob 3: TAB */
        menu_tab_go((int32_t)k);
#else
    if ((s = panel_enc(EN_ALGO)) != 0)
        menu_tab_go(s);
    for (s = 0, k = 0; k < 4u; k++)                    /* host tests: knobs 3..6 */
        s += panel_enc(EN_K1 + k);
#endif
    if (ui.menu_sel < MI_VALUES) {
        /* COLOR previews the palette. Host tests also step with OCT. SMK / device: SET knob only. */
#if !defined(SKM_ON_DEVICE) && !defined(SKM_SMK_PANEL)
        s += (int32_t)up - (int32_t)dn;
#else
        (void)dn;
#endif
        if (s)
            menu_put(ui.menu_sel, menu_step(ui.menu_sel, s));
    } else if (up) {
        if (ui.menu_sel == MI_PANEL) {
            panel_setup();
            ui.force = 1;
        } else if (ui.menu_sel == MI_UPDATE) {
            menu_update();
        } else if (ui.menu_sel == MI_FLAPPY) {         /* P19 */
            sk_game_open(SK_GAME_FLAPPY);
        } else if (ui.menu_sel == MI_TETRIS) {         /* P20 */
            sk_game_open(SK_GAME_TETRIS);
        } else {                                       /* MI_ABOUT */
            ui.menu = 2;
            ui.menu_scroll = 0;
            ui.force = 1;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}
