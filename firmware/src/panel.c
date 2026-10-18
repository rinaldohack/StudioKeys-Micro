/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The matrix ids are known, the printed labels are not, so
 * the mapping is a table with a best guess, overridable by HARDWARE CALIBRATION
 * (hold OCT- and OCT+ while powering on): StudioKEYS Micro asks for each label in turn.
 * The learned table lives in .noinit and, with SKM_FLASH, in flash with
 * the settings (project.c); read it back with `fm1t memr` to bake it in. */
/* P6b: the real SMK-37 panel has 18 printed buttons, not the FM-1's 14. Twelve sit under the fader
 * (two rows of six) and six under the screen (two columns of three):
 *
 *   under the fader    ARP | NOTE REPEAT | SCALE | CHORD | GLOBAL | BT
 *                      PATCH | PARA | FX | SEQ | SEQ PLAY | SEQ REC
 *   under the screen   KNOB BANK | FADER BANK
 *                      <         | >
 *                      OCT-      | OCT+
 *
 * The button-id space is 20 wide: 0..13 are the matrix cells (the bits of fm1_in.buttons, as
 * hal/fm1_input.h scans them) and PANEL_X..PANEL_X+5 are the six extra matrix keys (`<` `>` FADER
 * BANK CHORD NOTE REPEAT BT, the bits of smk_extra_down). A label maps to its id; panel_down turns
 * one into "is it held" over both masks. ENV and LFO are not physical buttons any more: their pages
 * stay reachable and their LEDs land on PARA (FAM_BTN in ui_input.c). */
enum {
    /* under the fader, upper row */
    B_ARP, B_REPEAT, B_SCL, B_CHRD, B_GLO, B_BT,
    /* under the fader, lower row */
    B_PATCH, B_PARA, B_FX, B_SEQ, B_PLAY, B_REC,
    /* under the screen */
    B_KNOB, B_FADER, B_LEFT, B_RIGHT, B_OCTDN, B_OCTUP,
    NB
};
#define PANEL_X 14u                      /* first id of the six extra matrix keys */
#define PANEL_NID (PANEL_X + 6u)         /* the id space: 0..13 cells, PANEL_X..PANEL_X+5 extras */
/* the printed names that older code (and the layers) still asks for by their old symbol */
#define B_EDIT B_PARA                    /* the printed PARA button */
#define B_SAVE B_PATCH                   /* the printed PATCH button */
#define B_HOME B_KNOB                    /* the printed KNOB BANK button */
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"ARP", "NOTE REPEAT", "SCALE", "CHORD", "GLOBAL", "BT",
                                        "PATCH", "PARA", "FX", "SEQ", "SEQ PLAY", "SEQ REC",
                                        "KNOB BANK", "FADER BANK", "<", ">", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"KNOB 1", "KNOB 2", "KNOB 7", "KNOB 3", "KNOB 4",
                                        "KNOB 5", "KNOB 6"};
#define PANEL_MAGIC 0x50414E36u          /* "PAN6": P6b's 18-button map (was "PAN5", 14 labels) */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* button id (0..PANEL_NID-1) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {10, PANEL_X + 4u, 3, PANEL_X + 3u, 7, PANEL_X + 5u,          /* ARP REPEAT SCALE CHORD GLOBAL BT */
     9, 6, 2, 11, 12, 13,                                        /* PATCH PARA FX SEQ PLAY REC */
     8, PANEL_X + 2u, PANEL_X, PANEL_X + 1u, 0, 1},               /* KNOB FADER < > OCT- OCT+ */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 (printed labels) */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, buttons = 0, encoders = 0, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++) {
        ok = panel.btn[i] < PANEL_NID && !(buttons & (1u << panel.btn[i]));
        if (ok) buttons |= 1u << panel.btn[i];
    }
    for (i = 0; ok && i < NE; i++) {
        ok = panel.enc[i] < NE && !(encoders & (1u << panel.enc[i])) &&
             (panel.dir[i] == 1 || panel.dir[i] == -1);
        if (ok) encoders |= 1u << panel.enc[i];
    }
    if (!ok)
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

/* steps of a role, + = clockwise */
static int32_t panel_enc(uint32_t role)
{
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

/* Keep the retained settings layout fixed. palette: the index into UI_PALETTES (SET3 held an index
 * into the 20 palettes of earlier firmware) */
#define SETTINGS_MAGIC 0x53455434u              /* "SET4" */
#define SETTINGS_MAGIC_OLD 0x53455433u          /* "SET3" */
struct { uint32_t magic, palette, lowcut, zoom; } settings __attribute__((section(".noinit")));

static void settings_save(void);              /* project.c: flash copy (SKM_FLASH) */
static uint8_t *palette_ice_mark(void);       /* favorites.factory[15][28], after favorites.c */

/* HOLD (menu): how long a button is held before its layer opens (ui_input.c). Saved in the settings record's
 * retired bold field as HOLD_TAG | index; any other value there (0 or 1 from older firmware) is the default */
#define HOLD_TAG 0x484C4400u
#define HOLD_DEF 1u
static const uint16_t HOLD_MS[4] = {300, 400, 500, 600};
static uint8_t settings_hold = HOLD_DEF;
/* P14: MENU > AUDIO. METRO: 0 = off, 1..4 = the click's level; COUNT-IN: a bar of clicks before a take.
 * Saved in favorites.factory[15][26] (settings_persist.c); 0 there is every older record = both off. */
static uint8_t settings_metro;                     /* 0 = off */
static uint8_t settings_countin;                   /* 0/1 */
static uint32_t hold_from_stored(uint32_t v) { return (v & ~3u) == HOLD_TAG ? v & 3u : HOLD_DEF; }
static uint32_t hold_to_stored(uint32_t old, uint32_t i)
{
    return i % 4u != HOLD_DEF ? HOLD_TAG | (i & 3u) : old > 1u ? 0u : old;
}
static int hold_stored_ok(uint32_t v) { return v <= 1u || (v & ~3u) == HOLD_TAG; }

/* LEDS (menu): OFF, DIM LO, DIM HI, INV. DIM HI (the default), the idle buttons and keys glow dim (~1/30) and the
 * active ones are lit (#35); DIM LO the same, darker (~1/60, hal/fm1_input.h FM1_LED_DIM_LO_NS); OFF no glow,
 * the active ones lit (as 1.0); INV the idle ones lit and the active ones dark, as the stock firmware (ui_input.c
 * ui_leds). Saved in the settings record's retired zoom field as LEDS_TAG | mode, the modes append-only: DIM 0
 * (now DIM HI) and INV 1 keep their values, OFF 2 and DIM LO 3 are new; any other value there (0 or 1 from older
 * firmware) is DIM HI. LEDS_MENU: the menu's order, darkest first */
#define LEDS_TAG 0x4C454400u                    /* "LED" */
enum { LEDS_DIM, LEDS_INV, LEDS_OFF, LEDS_DIM_LO, LEDS_COUNT };
static const uint8_t LEDS_MENU[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
static const char *const LEDS_NAME[LEDS_COUNT] = {"DIM HI", "INV", "OFF", "DIM LO"};
static uint8_t settings_leds = LEDS_DIM;
static uint32_t leds_from_stored(uint32_t v) { return (v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT ? v & 3u : LEDS_DIM; }
static uint32_t leds_to_stored(uint32_t old, uint32_t m)
{
    return m != LEDS_DIM && m < LEDS_COUNT ? LEDS_TAG | m : old > 1u ? 0u : old;
}
static int leds_stored_ok(uint32_t v) { return v <= 1u || ((v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT); }
/* the menu's next / previous mode (KNOB 1 stops at the ends, OCT+ (s 0) cycles) */
static uint32_t leds_step(uint32_t m, int32_t s)
{
    uint32_t i = 0;
    while (i + 1u < LEDS_COUNT && LEDS_MENU[i] != m)
        i++;
    i = s > 0 ? (i + 1u < LEDS_COUNT ? i + 1u : i) : s < 0 ? (i ? i - 1u : 0u) : (i + 1u) % LEDS_COUNT;
    return LEDS_MENU[i];
}

static void settings_init(void)
{
    if (settings.magic == SETTINGS_MAGIC_OLD && settings.palette < 20u) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = palette_from_stored(settings.palette);
    }
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = UI_ICE_INDEX;       /* GLACIER: blue. ASH stays palette 0 */
        settings.lowcut = 0;
        settings.zoom = 0;                     /* (retired: the LEDS setting, settings_persist.c) */
    }
    /* A saved GREY (palette 0, valid magic) is the old default. factory[15][28] is 0 until this
     * runs once: switch that GREY to ICE and save the marker. 1 leaves the palette alone, so a
     * later choice of GREY survives. Any other palette only marks the byte; the next settings
     * save keeps it, and this boot does not write flash. */
    {
        uint8_t *mark = palette_ice_mark();
        if (*mark == 0 && settings.palette == UI_GREY_INDEX) {
            settings.palette = UI_ICE_INDEX;
            *mark = 1;
            settings_save();
        } else if (*mark == 0)
            *mark = 1;
    }
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut % 3u);
}
