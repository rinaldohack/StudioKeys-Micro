/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* P4 / P4b: the 16 drumpads as page-aware RGB action buttons.
 *
 * The keyboard has no scribble strips, so the pads carry their own legend: each pad has an action id
 * with a short name (the screen's legend cell, <= 4 chars) and a full name (the hot / hint line), a
 * colour language matching the WS2812 chain (yellow = level, green = on, red = off, amber = mid / Leslie
 * slow, blue = special) and an active test read from the same state the firmware plays. smk_pads_run()
 * turns the pad edges into the action; smk_pads_led() paints the chain, and the legend (ui_draw.c) is
 * generated from the same table so the screen and the RGB colours can never disagree.
 *
 * STEP / DRUM keeps the musical sequencer grid: on a DRUM track the pads stay hits (grid_hit), on any
 * other track they are the 8 lane selectors + the 8 accent/page helpers.
 *
 * Included by ui_input.c (after ui.c and the engine files): it uses grid_on / grid_hit / drum_track /
 * ui_message / TSEL / track_desc. */

enum {
    PAD_NONE, PAD_STEP, PAD_LANE, PAD_ACC, PAD_PGDN, PAD_PGUP,
    PAD_FX_DIST, PAD_FX_CHOR, PAD_FX_DLY, PAD_FX_REV,
    PAD_WAVE0, PAD_WAVE1, PAD_PARAM0, PAD_PARAM1, PAD_PARAM2,
    PAD_ARPMODE, PAD_ARPHOLD, PAD_OCTDN, PAD_OCTUP, PAD_FREEZE,
    PAD_RECALL, PAD_EDITJUMP, PAD_SPLIT, PAD_PANIC
};

/* the colour language: index into PAD_RGB (G, R, B) */
enum { PC_OFF, PC_YEL, PC_GRN, PC_RED, PC_AMB, PC_BLU, PC_DIM };
static const uint8_t PAD_RGB[][3] = {
    {0, 0, 0},        /* OFF */
    {200, 200, 0},    /* YEL: level / drawbar */
    {0, 200, 0},      /* GRN: on / playing */
    {200, 0, 0},      /* RED: off / armed slot */
    {200, 120, 0},    /* AMB: mid (Leslie slow) */
    {0, 90, 200},     /* BLU: special (edit jump) */
    {24, 24, 24},     /* DIM: present but untouched */
};

typedef struct {
    uint8_t act;                 /* PAD_* */
    uint8_t col;                 /* colour when active */
    uint8_t param;               /* act's data: a param id, a lane, an offset */
    const char *abbr;            /* <= 4 chars: the legend cell */
    const char *full;            /* the hint line */
} pad_action_t;

/* Default: loose pages (a page with no map of its own) show the STEP helpers, the FX toggles and the
 * octave pair, so no pad is ever blank. */
static const pad_action_t PAD_DEF[16] = {
    {PAD_STEP,  PC_GRN, 0, "ST", "STEP"},  {PAD_STEP,  PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP,  PC_GRN, 2, "ST", "STEP"},  {PAD_STEP,  PC_GRN, 3, "ST", "STEP"},
    {PAD_STEP,  PC_GRN, 4, "ST", "STEP"},  {PAD_STEP,  PC_GRN, 5, "ST", "STEP"},
    {PAD_STEP,  PC_GRN, 6, "ST", "STEP"},  {PAD_STEP,  PC_GRN, 7, "ST", "STEP"},
    {PAD_FX_DIST, PC_RED, P_DIST, "DST", "DISTORTION"}, {PAD_FX_CHOR, PC_RED, P_CHOR, "CHR", "CHORUS"},
    {PAD_FX_DLY,  PC_RED, P_DLY, "DLY", "DELAY"},       {PAD_FX_REV, PC_RED, P_REV, "REV", "REVERB"},
    {PAD_OCTDN,  PC_AMB, 0, "OCT-", "OCTAVE DOWN"},      {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
    {PAD_FREEZE, PC_BLU, 0, "FRZ", "FREEZE"},            {PAD_PANIC, PC_RED, 0, "PNC", "PANIC"},
};

/* The four shared track FX toggles, reused by every engine map (P5b): DIST / CHOR / DLY / REV. */
#define FX4 \
    {PAD_FX_DIST, PC_RED, P_DIST, "DST", "DISTORTION"}, {PAD_FX_CHOR, PC_RED, P_CHOR, "CHR", "CHORUS"}, \
    {PAD_FX_DLY, PC_RED, P_DLY, "DLY", "DELAY"}, {PAD_FX_REV, PC_RED, P_REV, "REV", "REVERB"}

/* Per-engine maps (P5b). Only the shared FX4 is common; everything else is tailored. */
static const pad_action_t PAD_ANALOG[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "WAV", "WAVE CYCLE"}, {PAD_PARAM0, PC_YEL, P_E3, "NSE", "NOISE"},
    {PAD_PARAM1, PC_YEL, P_E6, "DRV", "DRIVE"}, {PAD_PARAM2, PC_YEL, P_E7, "KTR", "KEY TRACK"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_FM6[16] = {
    {PAD_PARAM0, PC_GRN, P_E0, "ALG", "ALGORITHM"}, {PAD_PARAM1, PC_YEL, P_E2, "FB", "FEEDBACK"},
    {PAD_PARAM2, PC_YEL, P_E3, "MIX", "MIX"}, {PAD_FREEZE, PC_BLU, 0, "HOLD", "FREEZE ENV"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

#define PAD_GENERIC_ENGINE \
    {PAD_PARAM0, PC_YEL, P_E0, "E1", "ENGINE 1"}, {PAD_PARAM1, PC_YEL, P_E1, "E2", "ENGINE 2"}, \
    {PAD_PARAM2, PC_YEL, P_E2, "E3", "ENGINE 3"}, {PAD_WAVE0, PC_AMB, P_E3, "MOD", "ENGINE 4"}, \
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"}, \
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"}, \
    FX4, \
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"}

static const pad_action_t PAD_PHASE[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_LOFI[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_SAMPLE[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_VOICE[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_TRIO[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_GRAIN[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_PHYS[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_NOISE[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_SLICE[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_WHEEL[16] = { PAD_GENERIC_ENGINE };
static const pad_action_t PAD_DRUM[16] = {
    {PAD_LANE, PC_GRN, 0, "BD", "KICK"}, {PAD_LANE, PC_GRN, 1, "SD", "SNARE"},
    {PAD_LANE, PC_GRN, 2, "CP", "CLAP"}, {PAD_LANE, PC_GRN, 3, "CH", "CLOSED HAT"},
    {PAD_LANE, PC_GRN, 4, "OH", "OPEN HAT"}, {PAD_LANE, PC_GRN, 5, "TM", "TOM"},
    {PAD_LANE, PC_GRN, 6, "RS", "RIM"}, {PAD_LANE, PC_GRN, 7, "CB", "BELL"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

/* a page's map: NULL falls back to the engine's, then PAD_DEF */
static const pad_action_t *pad_engine_map(const engine_t *e)
{
    if (e == &ENG_ANALOG) return PAD_ANALOG;
    if (e == &ENG_FM6)    return PAD_FM6;
    if (e == &ENG_DRUM)   return PAD_DRUM;
    return PAD_DEF;
}

/* The engine's own map is the fallback for a page with no tailored map, so the legend is never blank. */
static const pad_action_t *pad_map(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    return pad_engine_map(e);
}

static const pad_action_t *pad_get(uint32_t pad) { return &pad_map()[pad & 15u]; }

/* is the action currently "on"? The colour the LED and the legend dot use. */
static int pad_active(const pad_action_t *a)
{
    const track_t *t = TSEL;
    switch (a->act) {
    case PAD_FX_DIST: case PAD_FX_CHOR: case PAD_FX_DLY: case PAD_FX_REV:
        return t->p[a->param] > 0;
    case PAD_LANE:
        return (step_lanes(&seq_steps(t)[ui.cursor]) >> (a->param % NLANE)) & 1u;
    case PAD_STEP:
        return step_on(&seq_steps(t)[ui.bank * 16u + a->param]);
    case PAD_FREEZE:
        return 0;
    default:
        return 0;
    }
}

/* the LED colour of a pad: active colour, else dim if the action can be pressed, else off */
static uint32_t pad_colour(const pad_action_t *a)
{
    if (a->act == PAD_NONE)
        return PC_OFF;
    if (pad_active(a))
        return a->col;
    if (a->col == PC_RED)                          /* a toggle that is off reads red */
        return PC_RED;
    return PC_DIM;
}

/* P5d: the recall slot (0..14) in the current page-bank of 15 locked slots */
static uint32_t pad_recall_slot(uint32_t pad) { return pad & 15u; }

/* run the pad actions for the edges since the last pass */
static void smk_pads_run(void)
{
    uint32_t rel = 0, ed = smk_pad_edges(&rel), i;
    for (i = 0; i < 16u; i++) {
        const pad_action_t *a;
        if (!((ed >> i) & 1u))
            continue;
        a = pad_get(i);
        switch (a->act) {
        case PAD_STEP:
            if (grid_on())
                grid_hit(TSEL, ui.bank * 16u + a->param, ui.lane, 2);
            break;
        case PAD_LANE:
            if (grid_on()) {
                ui.lane = (uint8_t)(a->param % NLANE);
                ui.force = 1;
            }
            break;
        case PAD_FX_DIST: case PAD_FX_CHOR: case PAD_FX_DLY: case PAD_FX_REV: {
            int32_t on = TSEL->p[a->param] > 0 ? 0 : 64;
            TSEL->p[a->param] = (int16_t)on;
            ui_message(on ? a->full : "FX OFF");
            ui.force = 1;
            break;
        }
        case PAD_WAVE0: case PAD_WAVE1: case PAD_PARAM0: case PAD_PARAM1: case PAD_PARAM2: {
            const param_desc_t *d = track_desc(TSEL, a->param);
            int32_t v = param_turn(d, TSEL->p[a->param], 1);
            TSEL->p[a->param] = (int16_t)v;
            ui_message(a->full);
            ui.force = 1;
            break;
        }
        case PAD_OCTDN: case PAD_OCTUP: {
            int32_t o = song.octave + (a->act == PAD_OCTDN ? -1 : 1);
            song.octave = (int8_t)clamp(o, -3, 3);
            ui.force = 1;
            break;
        }
        case PAD_FREEZE: case PAD_PANIC: case PAD_SPLIT: case PAD_ARPMODE: case PAD_ARPHOLD: case PAD_PGDN:
        case PAD_PGUP: case PAD_RECALL: case PAD_EDITJUMP:
            break;
        default:
            break;
        }
    }
    (void)pad_recall_slot;
}

/* paint the chain from the map (P4). Device only: the host has no WS2812 chain. */
static void smk_pads_led(void)
{
#ifdef FELUCCA_ON_DEVICE
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        const pad_action_t *a = pad_get(i);
        const uint8_t *c = PAD_RGB[pad_colour(a)];
        smk_rgb_set(i, c[0], c[1], c[2]);
    }
#endif
}

/* device: paint the map onto the chain and latch it (once per UI frame) */
static void smk_pads_flush(void)
{
#ifdef FELUCCA_ON_DEVICE
    smk_pads_led();
    smk_rgb_show();
#endif
}
