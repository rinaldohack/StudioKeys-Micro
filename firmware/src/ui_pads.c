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
    PAD_TOGGLE,                  /* param: on / off enum, tap flips it */
    PAD_ARPMODE, PAD_OCTDN, PAD_OCTUP, PAD_FREEZE,
    PAD_RECALL, PAD_EDITJUMP, PAD_SPLIT, PAD_PANIC,
    PAD_SPL_LO, PAD_SPL_HI, PAD_SPL_NOTE,   /* P13: the SPLIT page's track and note picks */
    PAD_MOTION_ON, PAD_AUTOREC, PAD_MOTION_CLR,   /* P16: AUTOMATION page */
    PAD_QNT, PAD_CHRD, PAD_VOIC   /* P17: the SCL / CHORD pages' direct picks */
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

/* Per-engine maps (P5b). Only the shared FX4 is common; everything else is tailored to the
 * engine's real edit[] params. The layout is 4 + 4 + 4 + 4: the first four are the engine's own
 * shortcuts (cycles / toggles), then the STEP helpers, the four shared FX toggles and the octave pair.
 * PAD_PARAM0..2 / PAD_WAVE0..1 cycle a value; PAD_TOGGLE flips a two-state enum. */
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

static const pad_action_t PAD_PHASE[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "WAV", "WAVE CYCLE"}, {PAD_WAVE1, PC_AMB, P_E1, "WV2", "WAVE 2 CYCLE"},
    {PAD_PARAM0, PC_YEL, P_E5, "LIN", "LINE MIX / RING"}, {PAD_TOGGLE, PC_RED, P_E6, "SUB", "SUB OSC"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_LOFI[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "CHP", "CHIP CYCLE"}, {PAD_WAVE1, PC_AMB, P_E1, "WAV", "WAVE CYCLE"},
    {PAD_PARAM0, PC_YEL, P_E6, "ARP", "ARP CYCLE"}, {PAD_PARAM1, PC_YEL, P_E2, "DUT", "DUTY / WRAM"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_SAMPLE[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "SET", "SAMPLE SET"}, {PAD_TOGGLE, PC_RED, P_E3, "LP", "LOOP"},
    {PAD_PARAM0, PC_YEL, P_E2, "BIT", "BITS"}, {PAD_PARAM1, PC_YEL, P_E4, "CUT", "TONE / CUT"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_VOICE[16] = {
    {PAD_PARAM0, PC_YEL, P_E0, "VW1", "VOWEL 1"}, {PAD_PARAM1, PC_YEL, P_E1, "VW2", "VOWEL 2"},
    {PAD_PARAM2, PC_YEL, P_E7, "RND", "RANDOMIZE"}, {PAD_PARAM0, PC_YEL, P_E2, "TLK", "TALK / MORPH"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_TRIO[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "WAV", "WAVE CYCLE"}, {PAD_PARAM0, PC_YEL, P_E4, "MOD", "MODE CYCLE"},
    {PAD_PARAM1, PC_YEL, P_E1, "I2", "INT2 NUDGE"}, {PAD_PARAM2, PC_YEL, P_E2, "I3", "INT3 NUDGE"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_GRAIN[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "SRC", "SOURCE BANK"}, {PAD_PARAM0, PC_YEL, P_E1, "POS", "POSITION"},
    {PAD_PARAM1, PC_YEL, P_E3, "DNS", "DENSITY"}, {PAD_PARAM2, PC_YEL, P_E6, "RND", "SPRAY RANDOMIZE"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_PHYS[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "MDL", "MODEL CYCLE"}, {PAD_PARAM0, PC_YEL, P_E1, "STR", "STRUCTURE"},
    {PAD_PARAM1, PC_YEL, P_E4, "POS", "STRIKE POS"}, {PAD_PARAM2, PC_YEL, P_E6, "BOW", "BOW / SUSTAIN"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_NOISE[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "MOD", "MODE CYCLE"}, {PAD_PARAM0, PC_YEL, P_E1, "CLR", "COLOUR"},
    {PAD_PARAM1, PC_YEL, P_E5, "DNS", "LFSR / DENSITY"}, {PAD_PARAM2, PC_YEL, P_E7, "CLK", "CLOCK"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_SLICE[16] = {
    {PAD_WAVE0, PC_AMB, P_E1, "DIV", "DIVISION CYCLE"}, {PAD_PARAM0, PC_YEL, P_E4, "MOD", "MODE CYCLE"},
    {PAD_TOGGLE, PC_RED, P_E5, "REV", "REVERSE"}, {PAD_PARAM1, PC_YEL, P_E3, "PIT", "PITCH"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

/* WHEEL (organ): the 9 drawbars are the knobs + pad pairs (P5); here the first four pads carry the
 * config half - REG, PERC, DRV (enable), ROTR - so the switch half of the surface is reachable. */
static const pad_action_t PAD_WHEEL[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "REG", "REGISTRATION"}, {PAD_PARAM0, PC_YEL, P_E4, "PER", "PERCUSSION"},
    {PAD_TOGGLE, PC_RED, P_E6, "DRV", "OVERDRIVE"}, {PAD_PARAM1, PC_YEL, P_E7, "ROT", "ROTARY CYCLE"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

static const pad_action_t PAD_DRUM[16] = {
    {PAD_LANE, PC_GRN, 0, "BD", "KICK"}, {PAD_LANE, PC_GRN, 1, "SD", "SNARE"},
    {PAD_LANE, PC_GRN, 2, "CP", "CLAP"}, {PAD_LANE, PC_GRN, 3, "CH", "CLOSED HAT"},
    {PAD_LANE, PC_GRN, 4, "OH", "OPEN HAT"}, {PAD_LANE, PC_GRN, 5, "TM", "TOM"},
    {PAD_LANE, PC_GRN, 6, "RS", "RIM"}, {PAD_LANE, PC_GRN, 7, "CB", "BELL"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
};

/* P5d: Performance. 15 locked recall slots = the user bank's U01..U15 (up_load/up_store), so a live set
 * is just a curated map of sounds that already persist; pad 16 jumps to the selected sound's engine
 * page. Short press = recall, long press = the two-step save (arm, then the same pad again). */
static const pad_action_t PAD_PERF[16] = {
    {PAD_RECALL, PC_RED, 0, "U01", "RECALL U01"}, {PAD_RECALL, PC_RED, 1, "U02", "RECALL U02"},
    {PAD_RECALL, PC_RED, 2, "U03", "RECALL U03"}, {PAD_RECALL, PC_RED, 3, "U04", "RECALL U04"},
    {PAD_RECALL, PC_RED, 4, "U05", "RECALL U05"}, {PAD_RECALL, PC_RED, 5, "U06", "RECALL U06"},
    {PAD_RECALL, PC_RED, 6, "U07", "RECALL U07"}, {PAD_RECALL, PC_RED, 7, "U08", "RECALL U08"},
    {PAD_RECALL, PC_RED, 8, "U09", "RECALL U09"}, {PAD_RECALL, PC_RED, 9, "U10", "RECALL U10"},
    {PAD_RECALL, PC_RED, 10, "U11", "RECALL U11"}, {PAD_RECALL, PC_RED, 11, "U12", "RECALL U12"},
    {PAD_RECALL, PC_RED, 12, "U13", "RECALL U13"}, {PAD_RECALL, PC_RED, 13, "U14", "RECALL U14"},
    {PAD_RECALL, PC_RED, 14, "U15", "RECALL U15"},
    {PAD_EDITJUMP, PC_BLU, 0, "ED", "EDIT THE SOUND"},
};

/* P13: the SPLIT page. Pads 1-4 pick the LOW side's mixer track, 5-8 the HIGH side's, 9-12 a split
 * note around the keybed's middle and pad 16 arms / disarms the split. Tracks picked this way never
 * collide (the other side steps aside), so a pad press cannot switch the split off. */
static const pad_action_t PAD_SPLIT_PAGE[16] = {
    {PAD_SPL_LO, PC_GRN, 0, "L1", "LOW TRACK 1"}, {PAD_SPL_LO, PC_GRN, 1, "L2", "LOW TRACK 2"},
    {PAD_SPL_LO, PC_GRN, 2, "L3", "LOW TRACK 3"}, {PAD_SPL_LO, PC_GRN, 3, "L4", "LOW TRACK 4"},
    {PAD_SPL_HI, PC_GRN, 0, "H1", "HIGH TRACK 1"}, {PAD_SPL_HI, PC_GRN, 1, "H2", "HIGH TRACK 2"},
    {PAD_SPL_HI, PC_GRN, 2, "H3", "HIGH TRACK 3"}, {PAD_SPL_HI, PC_GRN, 3, "H4", "HIGH TRACK 4"},
    {PAD_SPL_NOTE, PC_AMB, 60, "C4", "SPLIT AT C4"}, {PAD_SPL_NOTE, PC_AMB, 65, "F4", "SPLIT AT F4"},
    {PAD_SPL_NOTE, PC_AMB, 72, "C5", "SPLIT AT C5"}, {PAD_SPL_NOTE, PC_AMB, 79, "G5", "SPLIT AT G5"},
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
    {PAD_PANIC, PC_RED, 0, "PNC", "PANIC"},
    {PAD_SPLIT, PC_GRN, 0, "SPL", "SPLIT ON/OFF"},
};

/* P16: AUTOMATION. Pads 1..3 are the page's own verbs (playback, AUTO REC, clear); the rest
 * keep the familiar step / FX / octave surface so the page never goes dark. Pad 2 blinks red
 * while armed: that is the legend for "knob moves are being captured", and the cards carry
 * MOTION's icon on every parameter that has an event (ui_draw.c card_mot_of). */
static const pad_action_t PAD_MOTION[16] = {
    {PAD_MOTION_ON, PC_GRN, 0, "PLAY", "MOTION PLAY"},
    {PAD_AUTOREC, PC_RED, 0, "AUTO", "AUTO REC"},
    {PAD_MOTION_CLR, PC_RED, 0, "CLR", "CLEAR AUTOMATION"},
    {PAD_STEP, PC_GRN, 0, "ST", "STEP"}, {PAD_STEP, PC_GRN, 1, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 2, "ST", "STEP"}, {PAD_STEP, PC_GRN, 3, "ST", "STEP"},
    {PAD_STEP, PC_GRN, 4, "ST", "STEP"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
    {PAD_FREEZE, PC_BLU, 0, "FRZ", "FREEZE"}, {PAD_PANIC, PC_RED, 0, "PNC", "PANIC"},
};

/* P17: SCL — the scale page's four knobs (ROOT, SCL, QNT, TRN) cycled from the first four pads, then
 * the key lock picked outright: OFF / SNAP / WHITE / SEQ. WHITE and SEQ are the musical ones (the key
 * bed follows the scale), so they are one tap away instead of a knob's several. */
static const pad_action_t PAD_SCL[16] = {
    {PAD_PARAM0, PC_YEL, P_ROOT, "ROOT", "ROOT NOTE"}, {PAD_PARAM0, PC_YEL, P_SCALE, "SCL", "SCALE CYCLE"},
    {PAD_PARAM0, PC_YEL, P_QUANT, "QNT", "KEY LOCK CYCLE"}, {PAD_PARAM0, PC_YEL, P_TRANS, "TRN", "TRANSPOSE"},
    {PAD_QNT, PC_GRN, 0, "OFF", "KEY LOCK OFF"}, {PAD_QNT, PC_GRN, 1, "SNAP", "SNAP TO THE SCALE"},
    {PAD_QNT, PC_GRN, 2, "WHT", "WHITE KEYS"}, {PAD_QNT, PC_GRN, 3, "SEQ", "FOLLOW THE SEQUENCE"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
    {PAD_FREEZE, PC_BLU, 0, "FRZ", "FREEZE"}, {PAD_PANIC, PC_RED, 0, "PNC", "PANIC"},
};

/* P17: CHORD — the chord type and the voicing cycled, then the picks a player reaches for: OFF, the
 * in-key triad the SCL button suggests, and the five voicings (CLOSE OPEN INV1 INV2 +OCT) outright. */
static const pad_action_t PAD_CHORD[16] = {
    {PAD_PARAM0, PC_YEL, P_CHRD, "CHRD", "CHORD TYPE CYCLE"}, {PAD_PARAM0, PC_YEL, P_VOIC, "VOIC", "VOICING CYCLE"},
    {PAD_CHRD, PC_GRN, 0, "OFF", "CHORDS OFF"}, {PAD_CHRD, PC_GRN, 1, "DIA3", "IN-KEY TRIADS"},
    {PAD_VOIC, PC_GRN, 0, "CLS", "VOICE CLOSE"}, {PAD_VOIC, PC_GRN, 1, "OPN", "VOICE OPEN"},
    {PAD_VOIC, PC_GRN, 2, "INV1", "INVERSION 1"}, {PAD_VOIC, PC_GRN, 3, "INV2", "INVERSION 2"},
    FX4,
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"},
    {PAD_VOIC, PC_GRN, 4, "+OCT", "+OCTAVE"}, {PAD_PANIC, PC_RED, 0, "PNC", "PANIC"},
};

/* P17: ARP — the four cards (MODE RATE OCT GATE) cycled, the modes a player switches between mid-song
 * (UP DN UPDN RND) plus OFF / RPT picked outright, and HOLD to latch the held chord. */
static const pad_action_t PAD_ARP[16] = {
    {PAD_PARAM0, PC_YEL, P_AMODE, "MODE", "ARP MODE"}, {PAD_PARAM0, PC_YEL, P_ARATE, "RATE", "ARP RATE"},
    {PAD_PARAM0, PC_YEL, P_AOCT, "OCT", "ARP OCTAVE"}, {PAD_PARAM0, PC_YEL, P_AGATE, "GATE", "ARP GATE"},
    {PAD_ARPMODE, PC_GRN, 1, "UP", "ARP UP"}, {PAD_ARPMODE, PC_GRN, 2, "DN", "ARP DOWN"},
    {PAD_ARPMODE, PC_GRN, 3, "UPDN", "ARP UP-DOWN"}, {PAD_ARPMODE, PC_GRN, 4, "RND", "ARP RANDOM"},
    FX4,
    {PAD_ARPMODE, PC_GRN, 0, "OFF", "ARP OFF"}, {PAD_ARPMODE, PC_GRN, 6, "RPT", "NOTE REPEAT"},
    {PAD_TOGGLE, PC_RED, P_AHOLD, "HOLD", "ARP HOLD"}, {PAD_PANIC, PC_RED, 0, "PNC", "PANIC"},
};

/* a page's map: the engine's tailored map, then PAD_DEF (P5b) */
static const pad_action_t *pad_engine_map(const engine_t *e)
{
    if (e == &ENG_ANALOG)  return PAD_ANALOG;
    if (e == &ENG_PHASE)   return PAD_PHASE;
    if (e == &ENG_LOFI)    return PAD_LOFI;
    if (e == &ENG_SAMPLE)  return PAD_SAMPLE;
    if (e == &ENG_FORMANT) return PAD_VOICE;
    if (e == &ENG_TRIO)    return PAD_TRIO;
    if (e == &ENG_GRAIN)   return PAD_GRAIN;
    if (e == &ENG_PHYS)    return PAD_PHYS;
    if (e == &ENG_NOISE)   return PAD_NOISE;
    if (e == &ENG_DRUM)    return PAD_DRUM;
    if (e == &ENG_WHEEL)   return PAD_WHEEL;
#if SKM_SLICE
    if (e == &ENG_SLICE)   return PAD_SLICE;
#endif
    if (e == &ENG_FM6)     return PAD_FM6;
    return PAD_DEF;
}

/* The engine's own map is the fallback for a page with no tailored map, so the legend is never blank. */
static const pad_action_t *pad_map(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    if (!ui.home && cur_page()->graph == GR_PERF)     /* P5d: the recall surface wins over the engine's map */
        return PAD_PERF;
    if (!ui.home && cur_page()->graph == GR_SPLIT)    /* P13: the key split's own picks */
        return PAD_SPLIT_PAGE;
    if (!ui.home && cur_page()->graph == GR_MOTION)   /* P16: playback, AUTO REC, clear */
        return PAD_MOTION;
    if (!ui.home && cur_page()->graph == GR_ARP)      /* P17: the arp's modes, rate, octave, gate, hold */
        return PAD_ARP;
    if (!ui.home && cur_page()->graph == GR_SCALE)    /* P17: root / scale / transpose + the key lock */
        return PAD_SCL;
    if (!ui.home && cur_page()->graph == GR_CHORD)    /* P17: chord type + the five voicings */
        return PAD_CHORD;
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
    case PAD_TOGGLE:
        return t->p[a->param] != 0;
    case PAD_FREEZE:
        return 0;
    case PAD_SPLIT:
        return split_note != 0;
    case PAD_SPL_LO:
        return split_note != 0 && split_lo == a->param;
    case PAD_SPL_HI:
        return split_note != 0 && split_hi == a->param;
    case PAD_SPL_NOTE:
        return split_note == a->param;
    case PAD_MOTION_ON:
        return motion_enabled(t);
    case PAD_AUTOREC:
        return motion_armed(t);
    case PAD_MOTION_CLR:
        return motion_count(t) != 0;
    case PAD_QNT:                                    /* P17: the key lock picked outright */
        return t->p[P_QUANT] == a->param;
    case PAD_CHRD:                                   /* P17: the chord type picked outright */
        return t->p[P_CHRD] == a->param;
    case PAD_VOIC:                                   /* P17: the voicing picked outright */
        return t->p[P_VOIC] == a->param;
    case PAD_ARPMODE:                                /* P17: the arp mode picked outright */
        return t->p[P_AMODE] == a->param;
    default:
        return 0;
    }
}

/* P5d: the armed slot (0..14, 0xFF none) of the two-step save. Any other input cancels it. */
static uint8_t pad_arm_slot = 0xFF;

/* the LED colour of a pad: active colour, else dim if the action can be pressed, else off */
static uint32_t pad_colour(const pad_action_t *a)
{
    if (a->act == PAD_NONE)
        return PC_OFF;
    if (a->act == PAD_RECALL) {                    /* P5d: empty dark, stored red, this track's green, armed blinks */
        if (pad_arm_slot == a->param)
            return ((fm1_ms / 250u) & 1u) ? PC_GRN : PC_OFF;
        if (!up_used(a->param))
            return PC_OFF;
        return TSEL->user == a->param + 1u ? PC_GRN : PC_RED;
    }
    if (a->act == PAD_EDITJUMP)
        return up_used(TSEL->user ? TSEL->user - 1u : 0u) ? PC_BLU : PC_DIM;
    if (a->act == PAD_AUTOREC)                     /* P16: armed blinks red, off stays red */
        return motion_armed(TSEL) ? (((fm1_ms / 250u) & 1u) ? PC_RED : PC_OFF) : PC_RED;
    if (pad_active(a))
        return a->col;
    if (a->col == PC_RED)                          /* a toggle that is off reads red */
        return PC_RED;
    return PC_DIM;
}

/* P5d: the recall slot (0..14) */
static uint32_t pad_recall_slot(uint32_t pad) { return pad & 15u; }

/* P5d: put slot k into the selected track (short press) */
static void pad_perf_recall(uint32_t slot)
{
    char l[4];
    pad_arm_slot = 0xFF;
    up_slot_label(l, slot);
    if (up_load(slot)) {
        ui_say(l, " EMPTY");
        ui.force = 1;
        return;
    }
    ui_say("RECALL ", l);
}

/* P5d: the two-step save - the first long press arms, the same pad again commits, anything else cancels */
static void pad_perf_save(uint32_t slot)
{
    char l[4];
    up_slot_label(l, slot);
    if (pad_arm_slot != slot) {
        pad_arm_slot = (uint8_t)slot;
        ui_say("SAVE ", l);
        str_cpy(ui.msg + str_len(ui.msg), "? HOLD AGAIN", sizeof ui.msg - str_len(ui.msg));
        ui.msg_t = 90;
        ui.force = 1;
        return;
    }
    pad_arm_slot = 0xFF;
    {   /* (an occupied slot keeps its name: a live set is curated, not re-typed) */
        char nm[13];
        if (up_used(slot))
            up_name(slot, nm);
        if (up_store(slot, up_used(slot) ? nm : 0)) {
            ui_say("SAVE FAILED ", l);
            return;
        }
    }
    TSEL->user = (uint8_t)(slot + 1u);              /* the recall surface follows the write */
    ui_say("SAVED ", l);
    ui.force = 1;
}

/* P5d: any other input cancels a pending save (a key, a button, another pad) */
static void pad_perf_cancel(void)
{
    if (pad_arm_slot != 0xFF) {
        pad_arm_slot = 0xFF;
        ui_message("SAVE CANCELLED");
        ui.force = 1;
    }
}

/* P17: a pad writes a track parameter the way its knob does, so the automation capture sees it */
static void pad_param_write(uint32_t id, int32_t v)
{
    TSEL->p[id] = (int16_t)v;
    motion_capture(TSEL, id, (int16_t)v);
}

/* P17: a value picked outright (the SCL / CHORD / ARP pages): the index is already in range */
static void pad_pick(uint32_t id, uint32_t idx, const char *const *names, const char *full)
{
    pad_param_write(id, (int32_t)idx);
    ui_message(names[idx] && names[idx][0] ? names[idx] : full);
    ui.force = 1;
}

/* one pad's action (its edge already latched, a pending PERFORM save already cancelled) */
static void pad_do(const pad_action_t *a)
{
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
        pad_param_write(a->param, on);
        ui_message(on ? a->full : "FX OFF");
        ui.force = 1;
        break;
    }
    case PAD_WAVE0: case PAD_WAVE1: case PAD_PARAM0: case PAD_PARAM1: case PAD_PARAM2: {
        const param_desc_t *d = track_desc(TSEL, a->param);
        pad_param_write(a->param, param_turn(d, TSEL->p[a->param], 1));
        ui_message(a->full);
        ui.force = 1;
        break;
    }
    case PAD_TOGGLE: {
        const param_desc_t *d = track_desc(TSEL, a->param);
        int32_t on = TSEL->p[a->param] != 0 ? d->min : d->max;   /* a two-state enum: flip it */
        pad_param_write(a->param, on);
        ui_message(a->full);
        ui.force = 1;
        break;
    }
    case PAD_QNT:                                /* P17: the key lock, picked outright */
        pad_pick(P_QUANT, a->param, N_QUANT, "KEY LOCK");
        break;
    case PAD_CHRD:                               /* P17: the chord type, picked outright */
        pad_pick(P_CHRD, a->param, N_CHRD, "CHORDS");
        break;
    case PAD_VOIC:                               /* P17: the voicing, picked outright */
        pad_pick(P_VOIC, a->param, N_VOIC, "VOICE");
        break;
    case PAD_ARPMODE:                            /* P17: the arp mode, picked outright */
        pad_pick(P_AMODE, a->param, N_AMODE, "ARP");
        break;
    case PAD_OCTDN: case PAD_OCTUP: {
        int32_t o = song.octave + (a->act == PAD_OCTDN ? -1 : 1);
        song.octave = (int8_t)clamp(o, -3, 3);
        ui.force = 1;
        break;
    }
    case PAD_RECALL:
        break;                                   /* (handled on release, in smk_pads_run) */
    case PAD_EDITJUMP:
        page_tap(B_EDIT);                        /* P5d: the selected sound's engine page (the PARA walk) */
        break;
    case PAD_SPLIT:                              /* P13 */
        split_pad_toggle();
        ui_message(split_note ? "SPLIT ON" : "SPLIT OFF");
        ui.force = 1;
        break;
    case PAD_SPL_LO:
        split_pad_track(0u, (uint32_t)a->param);
        ui_message(a->full);
        ui.force = 1;
        break;
    case PAD_SPL_HI:
        split_pad_track(1u, (uint32_t)a->param);
        ui_message(a->full);
        ui.force = 1;
        break;
    case PAD_SPL_NOTE:
        split_pad_note((uint32_t)a->param);
        ui_message(a->full);
        ui.force = 1;
        break;
    case PAD_MOTION_ON:                          /* P16: playback of the captured events */
        if (chain_busy()) { ui_message("STOP TO EDIT"); break; }
        motion_set_enabled(TSEL, !motion_enabled(TSEL));
        ui_message(motion_enabled(TSEL) ? "MOTION PLAY" : "MOTION PLAY OFF");
        ui.force = 1;
        break;
    case PAD_AUTOREC:                            /* P16: knob moves -> steps, notes untouched */
        motion_arm_set(TSEL, !motion_armed(TSEL));
        if (motion_armed(TSEL))
            ui_message(song.playing ? "AUTO REC ON" : "AUTO REC: PLAY");
        else
            ui_message("AUTO REC OFF");
        ui.force = 1;
        break;
    case PAD_MOTION_CLR:
        if (chain_busy()) { ui_message("STOP TO EDIT"); break; }
        if (!motion_count(TSEL)) { ui_message("NOTHING TO CLEAR"); break; }
        confirm_open(CF_CLEAR_MOTION, song.sel);
        break;
    case PAD_FREEZE: case PAD_PANIC: case PAD_PGDN:
    case PAD_PGUP:
        break;
    default:
        break;
    }
}

/* run the pad actions for the edges since the last pass */
static void smk_pads_run(void)
{
    static uint32_t pad_t0[16];                     /* P5d: press time | 1 (the recall pads act on release) */
    uint32_t rel = 0, ed = smk_pad_edges(&rel), i;
    int perf = !ui.home && cur_page()->graph == GR_PERF;
    for (i = 0; i < 16u; i++) {
        const pad_action_t *a = pad_get(i);
        if (perf && a->act == PAD_RECALL) {          /* P5d: short press loads, long press arms / saves */
            if ((ed >> i) & 1u)
                pad_t0[i] = fm1_ms | 1u;
            if (((rel >> i) & 1u) && pad_t0[i]) {
                uint32_t held = fm1_ms - (pad_t0[i] & ~1u);
                pad_t0[i] = 0;
                if (held >= 600u)
                    pad_perf_save((uint32_t)a->param);
                else
                    pad_perf_recall((uint32_t)a->param);
            }
            continue;
        }
        if (!((ed >> i) & 1u))
            continue;
        if (pad_arm_slot != 0xFF)                    /* any other pad cancels the pending save (silently: it acts) */
            pad_arm_slot = 0xFF;
        pad_do(a);
    }
    (void)pad_recall_slot;
}

/* paint the chain from the map (P4). Device only: the host has no WS2812 chain. */
static void smk_pads_led(void)
{
#ifdef SKM_ON_DEVICE
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
#ifdef SKM_ON_DEVICE
    smk_pads_led();
    smk_rgb_show();
#endif
}
