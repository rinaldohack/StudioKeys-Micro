/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* P4 / P4b: the 16 drumpads as page-aware RGB action buttons.
 *
 * The keyboard has no scribble strips, so the pads carry their own legend: each pad has an action id
 * with a short name (the screen's legend cell, <= 4 chars) and a full name (the hot / hint line), a
 * colour language matching the WS2812 chain (yellow = level, green = on, red = off, amber = mid / Leslie
 * slow, blue = special) and an active test read from the same state the firmware plays. smk_pads_run()
 * turns the pad edges into the action; smk_pads_led() paints the chain, and the legend (ui_draw.c) is
 * generated from the same table so the screen and the RGB colours can never disagree.
 *
 * P5e: the WHEEL drawbar pads are also the instrument's virtual drawbars. Their value is the bar's
 * level 0..8 (not a flag), the LED's yellow brightens with it, and a press rides it: a tap steps,
 * holding ramps on the device, and the companion drags it with the mouse (sk_host_pad_level_*).
 *
 * STEP / DRUM keeps the musical sequencer grid: on a DRUM track the pads stay hits (grid_hit), on any
 * other track they are the 8 lane selectors + the 8 accent/page helpers.
 *
 * Included by ui_input.c (after ui.c and the engine files): it uses grid_on / grid_hit / drum_track /
 * ui_message / TSEL / track_desc. */

enum {
    PAD_NONE, PAD_STEP, PAD_LANE, PAD_HIT, PAD_ACC, PAD_PGDN, PAD_PGUP,
    PAD_FX_DIST, PAD_FX_CHOR, PAD_FX_DLY, PAD_FX_REV,
    PAD_WAVE0, PAD_WAVE1, PAD_PARAM0, PAD_PARAM1, PAD_PARAM2,
    PAD_TOGGLE,                  /* param: on / off enum, tap flips it */
    PAD_ARPMODE, PAD_OCTDN, PAD_OCTUP, PAD_FREEZE,
    PAD_RECALL, PAD_EDITJUMP, PAD_SPLIT, PAD_PANIC,
    PAD_SPL_LO, PAD_SPL_HI, PAD_SPL_NOTE,   /* P13: the SPLIT page's track and note picks */
    PAD_MOTION_ON, PAD_AUTOREC, PAD_MOTION_CLR,   /* P16: AUTOMATION page */
    PAD_QNT, PAD_CHRD, PAD_VOIC,  /* P17: the SCL / CHORD pages' direct picks */
    PAD_DBAR,                    /* P5: a drawbar pair: param = bar (0..8) | up (1) << 4 */
    PAD_LESLIE, PAD_PERC, PAD_PERCHAR, PAD_PDEC, PAD_CLICK   /* P5: WHEEL's switch half */
};

/* the colour language: index into PAD_RGB (R, G, B; pad_rgb_value packs it 0xRRGGBB for the
 * companion, and smk_pads_led hands the three bytes to smk_rgb_set) */
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

/* A pad the engine has no use for: still a cell (the legend never shows a hole), but inert. */
#define SPARE {PAD_NONE, PC_DIM, 0, "--", "SPARE"}
/* The tail every engine page shares (P5b): the four FX toggles, then OCT- / OCT+ / FREEZE / PANIC. */
#define PAD_TAIL \
    FX4, \
    {PAD_OCTDN, PC_AMB, 0, "OCT-", "OCTAVE DOWN"}, {PAD_OCTUP, PC_AMB, 0, "OCT+", "OCTAVE UP"}, \
    {PAD_FREEZE, PC_BLU, 0, "FRZ", "FREEZE"}, {PAD_PANIC, PC_RED, 0, "PNC", "PANIC"}

/* Per-engine maps (P5b). Only the shared FX4 is common; everything else is tailored to the
 * engine's real edit[] params. The layout is 4 + 4 + 4 + 4: the first four are the engine's own
 * shortcuts (cycles / toggles), then the STEP helpers, the four shared FX toggles and the octave pair.
 * PAD_PARAM0..2 / PAD_WAVE0..1 cycle a value; PAD_TOGGLE flips a two-state enum. */
static const pad_action_t PAD_ANALOG[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "WAV", "WAVE CYCLE"}, {PAD_PARAM0, PC_YEL, P_E4, "CUT", "CUTOFF"},
    {PAD_PARAM1, PC_YEL, P_E5, "RES", "RESONANCE"}, {PAD_PARAM2, PC_YEL, P_E6, "DRV", "DRIVE"},
    {PAD_PARAM0, PC_YEL, P_E3, "NOI", "NOISE"}, {PAD_PARAM1, PC_YEL, P_E1, "DTN", "DETUNE"},
    {PAD_PARAM2, PC_YEL, P_E2, "MIX", "MIX"}, {PAD_PARAM0, PC_YEL, P_E7, "KTR", "KEY TRACK"},
    PAD_TAIL,
};

static const pad_action_t PAD_FM6[16] = {
    {PAD_PARAM0, PC_GRN, P_E0, "ALG", "ALGORITHM"}, {PAD_PARAM1, PC_YEL, P_E1, "FB", "FEEDBACK"},
    {PAD_PARAM2, PC_YEL, P_E2, "MLV", "MOD LEVEL"}, {PAD_PARAM0, PC_YEL, P_E3, "MRT", "MOD RATIO"},
    {PAD_PARAM1, PC_YEL, P_E4, "MEG", "MOD EG"}, {PAD_PARAM2, PC_YEL, P_E5, "VMD", "VIB DEPTH"},
    {PAD_PARAM0, PC_YEL, P_E6, "DTN", "DETUNE"}, {PAD_FREEZE, PC_BLU, 0, "HLD", "FREEZE ENV"},
    PAD_TAIL,
};

static const pad_action_t PAD_PHASE[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "WAV", "WAVE CYCLE"}, {PAD_WAVE1, PC_AMB, P_E1, "WV2", "WAVE 2 CYCLE"},
    {PAD_PARAM0, PC_YEL, P_E2, "DCW", "DCW DEPTH"}, {PAD_PARAM1, PC_YEL, P_E3, "ENV", "ENV AMOUNT"},
    {PAD_PARAM2, PC_YEL, P_E4, "DTN", "DETUNE"}, {PAD_PARAM0, PC_YEL, P_E5, "LIN", "LINE / RING"},
    {PAD_TOGGLE, PC_RED, P_E6, "SUB", "SUB OSC"}, SPARE,
    PAD_TAIL,
};

static const pad_action_t PAD_LOFI[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "CHP", "CHIP CYCLE"}, {PAD_WAVE1, PC_AMB, P_E1, "WAV", "WAVE CYCLE"},
    {PAD_PARAM0, PC_YEL, P_E2, "DUT", "DUTY / WRAM"}, {PAD_PARAM1, PC_YEL, P_E3, "CRS", "CRUSH"},
    {PAD_PARAM2, PC_YEL, P_E4, "SWP", "SWEEP"}, {PAD_PARAM0, PC_YEL, P_E5, "VIB", "VIBRATO"},
    {PAD_PARAM1, PC_YEL, P_E6, "ARP", "ARP CYCLE"}, {PAD_PARAM2, PC_YEL, P_E7, "TNE", "TONE"},
    PAD_TAIL,
};

static const pad_action_t PAD_SAMPLE[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "SET", "SAMPLE SET"}, {PAD_PARAM0, PC_YEL, P_E1, "TUN", "TUNE"},
    {PAD_PARAM1, PC_YEL, P_E2, "BIT", "BITS"}, {PAD_TOGGLE, PC_RED, P_E3, "LP", "LOOP"},
    {PAD_PARAM2, PC_YEL, P_E4, "CUT", "TONE / CUT"}, {PAD_PARAM0, PC_YEL, P_E6, "DRV", "DRIVE"},
    SPARE, SPARE,
    PAD_TAIL,
};

static const pad_action_t PAD_VOICE[16] = {
    {PAD_PARAM0, PC_YEL, P_E0, "VW1", "VOWEL 1"}, {PAD_PARAM1, PC_YEL, P_E1, "VW2", "VOWEL 2"},
    {PAD_PARAM2, PC_YEL, P_E2, "TLK", "TALK / MORPH"}, {PAD_PARAM0, PC_YEL, P_E3, "SFT", "SHIFT"},
    {PAD_PARAM1, PC_YEL, P_E4, "BUZ", "BUZZ"}, {PAD_PARAM2, PC_YEL, P_E5, "BRH", "BREATH"},
    {PAD_PARAM0, PC_YEL, P_E6, "Q", "RESONANCE"}, {PAD_PARAM1, PC_YEL, P_E7, "RND", "RANDOMIZE"},
    PAD_TAIL,
};

static const pad_action_t PAD_TRIO[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "WAV", "WAVE CYCLE"}, {PAD_PARAM0, PC_YEL, P_E1, "I2", "INT2 NUDGE"},
    {PAD_PARAM1, PC_YEL, P_E2, "I3", "INT3 NUDGE"}, {PAD_PARAM2, PC_YEL, P_E3, "DTN", "DETUNE"},
    {PAD_PARAM0, PC_YEL, P_E4, "MOD", "MODE CYCLE"}, {PAD_PARAM1, PC_YEL, P_E5, "CUT", "CUTOFF"},
    {PAD_PARAM2, PC_YEL, P_E6, "RES", "RESONANCE"}, {PAD_PARAM0, PC_YEL, P_E7, "PW", "PULSE WIDTH"},
    PAD_TAIL,
};

static const pad_action_t PAD_GRAIN[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "SRC", "SOURCE BANK"}, {PAD_PARAM0, PC_YEL, P_E1, "POS", "POSITION"},
    {PAD_PARAM1, PC_YEL, P_E2, "SIZ", "GRAIN SIZE"}, {PAD_PARAM2, PC_YEL, P_E3, "DNS", "DENSITY"},
    {PAD_PARAM0, PC_YEL, P_E4, "PIT", "PITCH"}, {PAD_PARAM1, PC_YEL, P_E5, "SPR", "SPRAY"},
    {PAD_PARAM2, PC_YEL, P_E6, "RND", "RANDOMIZE"}, {PAD_PARAM0, PC_YEL, P_E7, "TNE", "TONE"},
    PAD_TAIL,
};

static const pad_action_t PAD_PHYS[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "MDL", "MODEL CYCLE"}, {PAD_PARAM0, PC_YEL, P_E1, "STR", "STRUCTURE"},
    {PAD_PARAM1, PC_YEL, P_E2, "BRT", "BRIGHTNESS"}, {PAD_PARAM2, PC_YEL, P_E3, "DMP", "DAMPING"},
    {PAD_PARAM0, PC_YEL, P_E4, "POS", "STRIKE POS"}, {PAD_PARAM1, PC_YEL, P_E5, "ACC", "ACCENT"},
    {PAD_PARAM2, PC_YEL, P_E6, "BOW", "BOW / SUSTAIN"}, {PAD_PARAM0, PC_YEL, P_E7, "EXC", "EXCITER"},
    PAD_TAIL,
};

static const pad_action_t PAD_NOISE[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "MOD", "MODE CYCLE"}, {PAD_PARAM0, PC_YEL, P_E1, "CLR", "COLOUR"},
    {PAD_PARAM1, PC_YEL, P_E2, "FRQ", "FREQUENCY"}, {PAD_PARAM2, PC_YEL, P_E3, "RES", "RESONANCE"},
    {PAD_PARAM0, PC_YEL, P_E4, "TRK", "KEY TRACK"}, {PAD_PARAM1, PC_YEL, P_E5, "DNS", "LFSR / DENSITY"},
    {PAD_PARAM2, PC_YEL, P_E6, "DFT", "DRIFT"}, {PAD_PARAM0, PC_YEL, P_E7, "CRS", "CLOCK / CRUSH"},
    PAD_TAIL,
};

static const pad_action_t PAD_SLICE[16] = {
    {PAD_WAVE0, PC_AMB, P_E0, "SRC", "SOURCE CYCLE"}, {PAD_WAVE1, PC_AMB, P_E1, "DIV", "DIVISION CYCLE"},
    {PAD_PARAM0, PC_YEL, P_E2, "STA", "START"}, {PAD_PARAM1, PC_YEL, P_E3, "PIT", "PITCH"},
    {PAD_PARAM2, PC_YEL, P_E4, "MOD", "MODE CYCLE"}, {PAD_TOGGLE, PC_RED, P_E5, "REV", "REVERSE"},
    {PAD_PARAM0, PC_YEL, P_E6, "DCY", "DECAY"}, {PAD_PARAM1, PC_YEL, P_E7, "TNE", "TONE"},
    PAD_TAIL,
};

/* WHEEL (organ), P5: all nine drawbars live on 5 knobs (K1..K5 on SOUND) *and* four pad pairs — pad
 * columns 1..4, the TOP pad a step down and the BOTTOM pad a step up (the pull) — then the switch
 * half on pad columns 5..8: Leslie (cycles OFF/SLOW/FAST), overdrive (the track's DIST), percussion
 * enable, the percussion harmonic and decay, chorus, reverb and key click. That is the plan's map
 * (CFW_VST_UI_REVAMP_PLAN.md P5); the companion draws the same abbr / colour on its 16 pads
 * (SkPadControl, sk_host_pad_*).
 *
 * Rows: the wiring datasheet (genkigenki/fm1-to-smk37, 01-hardware.md 6.3, verified with `padtest`)
 * puts hardware pads 0..7 (pads 1..8) in the TOP row and 8..15 (9..16) in the BOTTOM row, left to
 * right. The companion draws them the same way, so the BOTTOM row is the one nearest the keybed —
 * the "pull" row a Nord's drawbar LEDs put under the fingers. */
static const pad_action_t PAD_WHEEL[16] = {
    /* row 1 = the TOP pads (1..8): the pairs' DOWN half, then cols 5..8 */
    {PAD_DBAR, PC_YEL, 5u | 0x00u, "2'", "2' BAR DOWN"},
    {PAD_DBAR, PC_YEL, 6u | 0x00u, "1.6'", "1 3/5' BAR DOWN"},
    {PAD_DBAR, PC_YEL, 7u | 0x00u, "1.3'", "1 1/3' BAR DOWN"},
    {PAD_DBAR, PC_YEL, 8u | 0x00u, "1'", "1' BAR DOWN"},
    {PAD_LESLIE, PC_AMB, 0, "ROT", "LESLIE"},
    {PAD_PERC, PC_GRN, 0, "PER", "PERCUSSION"},
    {PAD_PDEC, PC_AMB, 0, "DEC", "PERC DECAY"},
    {PAD_FX_REV, PC_RED, P_REV, "REV", "REVERB"},
    /* row 2 = the BOTTOM pads (9..16): the pairs' UP half — the pull that opens the bar — then
     * cols 5..8 */
    {PAD_DBAR, PC_YEL, 5u | 0x10u, "2'", "2' BAR UP"},
    {PAD_DBAR, PC_YEL, 6u | 0x10u, "1.6'", "1 3/5' BAR UP"},
    {PAD_DBAR, PC_YEL, 7u | 0x10u, "1.3'", "1 1/3' BAR UP"},
    {PAD_DBAR, PC_YEL, 8u | 0x10u, "1'", "1' BAR UP"},
    {PAD_FX_DIST, PC_RED, P_DIST, "DRV", "OVERDRIVE"},
    {PAD_PERCHAR, PC_AMB, 0, "HRM", "PERC HARMONIC"},
    {PAD_FX_CHOR, PC_RED, P_CHOR, "CHR", "CHORUS"},
    {PAD_CLICK, PC_RED, 0, "CLK", "KEY CLICK"},
};

/* DRUM (P5b): both rows are playable. Row 1 = the eight sequencer lanes (DRUM_LANE_NOTE), row 2 = eight
 * more GM voices. A tap strikes the selected track's kit and also picks the lane for the grid. */
static const pad_action_t PAD_DRUM[16] = {
    {PAD_LANE, PC_GRN, 0, "BD", "KICK"}, {PAD_LANE, PC_GRN, 1, "SD", "SNARE"},
    {PAD_LANE, PC_GRN, 2, "CP", "CLAP"}, {PAD_LANE, PC_GRN, 3, "CH", "CLOSED HAT"},
    {PAD_LANE, PC_GRN, 4, "OH", "OPEN HAT"}, {PAD_LANE, PC_GRN, 5, "TM", "TOM"},
    {PAD_LANE, PC_GRN, 6, "RS", "RIM"}, {PAD_LANE, PC_GRN, 7, "CB", "BELL"},
    {PAD_HIT, PC_GRN, 35, "PUN", "PUNCH"}, {PAD_HIT, PC_GRN, 41, "TML", "LOW TOM"},
    {PAD_HIT, PC_GRN, 43, "TOM", "HIGH TOM"}, {PAD_HIT, PC_GRN, 49, "CRS", "CRASH"},
    {PAD_HIT, PC_GRN, 51, "RID", "RIDE"}, {PAD_HIT, PC_GRN, 54, "TMB", "TAMB"},
    {PAD_HIT, PC_GRN, 75, "CLV", "CLAVE"}, {PAD_HIT, PC_GRN, 53, "BEL", "BELL"},
};

/* P5d: Performance. Paginated recall surface covering all 100 user slots (UP_SLOTS).
 * Each page shows 15 recall pads + 1 edit jump.  Page 0 = U01..U15, page 1 = U16..U30, ...
 * page 6 = U91..U100 (slots past 100 are PAD_NONE).  The < and > buttons navigate pages
 * instead of switching tracks while the PERFORM page is active.
 * PERF_PER_PAGE, PERF_PAGES, perf_page_idx live in ui.c (visible to ui_graph.c too). */

/* The perf map is ONE scratch entry, not a 16-pad table: the table plus its label buffers cost
 * 459 B of .bss, and the RAM region has none to spare (the 16-pad const map it replaced held
 * 256 B of XIP, which this keeps free too).  perf_pad() is pure in (page, pad), so a caller that
 * re-fetches the same pad gets identical bytes - that is why the re-reads below are safe. */
static pad_action_t perf_one;
static char perf_lbl[5];                /* "U01" .. "U100" (max 4 chars + NUL) */

static const pad_action_t *perf_pad(uint32_t pad)
{
    uint32_t slot = (uint32_t)perf_page_idx * PERF_PER_PAGE + pad;
    if (pad >= PERF_PER_PAGE)                   /* pad 16: the edit jump */
        perf_one = (pad_action_t){PAD_EDITJUMP, PC_BLU, 0, "ED", "EDIT THE SOUND"};
    else if (slot < UP_SLOTS) {                 /* page 7 runs out at U100: the rest are holes */
        up_slot_label(perf_lbl, slot);
        perf_one = (pad_action_t){PAD_RECALL, PC_RED, (uint8_t)slot, perf_lbl, perf_lbl};
    } else {
        perf_one = (pad_action_t){PAD_NONE, PC_OFF, 0, "--", ""};
    }
    return &perf_one;
}

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

static const pad_action_t *pad_get(uint32_t pad)
{
    if (!ui.home && cur_page()->graph == GR_PERF)   /* P5d: the recall surface builds its pad on demand */
        return perf_pad(pad & 15u);
    return &pad_map()[pad & 15u];
}

/* P5e: a drawbar pad is a virtual drawbar. The pad's value is its bar's level 0..8, so the same
 * number the engine reads (P_E0..P_E8) is the one the LED bar, the legend and the companion draw.
 * A tap steps it once; holding it ramps (smk_pads_run); the companion rides it outright with the
 * mouse (sk_host_pad_level_set) — the "press and drag" the eight LEDs on a Nord's drawbar give. */
static int pad_level_id(uint32_t pad)
{
    const pad_action_t *a = pad_get(pad);
    return a->act == PAD_DBAR ? (int)(P_E0 + (a->param & 15u)) : -1;
}
static int pad_level_get(uint32_t pad)
{
    int id = pad_level_id(pad);
    return id < 0 ? -1 : (int)clamp(TSEL->p[id], 0, 8);
}

/* is the action currently "on"? The colour the LED and the legend dot use. */
static int pad_active(const pad_action_t *a)
{
    const track_t *t = TSEL;
    switch (a->act) {
    case PAD_FX_DIST: case PAD_FX_CHOR: case PAD_FX_DLY: case PAD_FX_REV:
        return t->p[a->param] > 0;
    case PAD_LANE:
        return (step_lanes(&seq_steps(t)[ui.cursor]) >> (a->param % NLANE)) & 1u;
    case PAD_HIT:
        return 0;
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
    case PAD_DBAR:                                   /* P5: a drawbar pair lights at level > 0 */
        return t->p[P_E0 + (a->param & 15u)] > 0;
    case PAD_LESLIE:
        return t->p[P_E11] != 0;
    case PAD_PERC:
        return t->p[P_E9] != 0;
    case PAD_PERCHAR:                                /* 3rd: 2, 4, 6 */
        return (t->p[P_E9] == 2 || t->p[P_E9] == 4 || t->p[P_E9] == 6);
    case PAD_PDEC:                                   /* slow: 5, 6 */
        return (t->p[P_E9] == 5 || t->p[P_E9] == 6);
    case PAD_CLICK:
        return t->p[P_E10] > 0;
    default:
        return 0;
    }
}

/* P5d: the armed slot (0..14, 0xFF none) of the two-step save. Any other input cancels it. */
static uint8_t pad_arm_slot = 0xFF;

static void perf_page_set(uint32_t p)
{
    if (p >= PERF_PAGES) p = PERF_PAGES - 1u;
    perf_page_idx = (uint8_t)p;
    pad_arm_slot = 0xFF;
    ui.force = 1;
}

/* P5e: an on/off switch (the FX toggles, percussion, click, the ARP hold, a page's toggle) reads
 * green when it is on and red when it is off. Before this a toggle's "active" colour was its own
 * table entry — and every FX entry's was PC_RED — so DIST / CHOR / DLY / REV were red whether they
 * were engaged or not, and the WHEEL's OVERDRIVE pad never showed the DRV knob. */
static int pad_is_switch(uint8_t act)
{
    switch (act) {
    case PAD_FX_DIST: case PAD_FX_CHOR: case PAD_FX_DLY: case PAD_FX_REV:
    case PAD_PERC: case PAD_CLICK:
    case PAD_TOGGLE: case PAD_SPLIT:
        return 1;
    default:
        return 0;
    }
}

/* the LED colour of a pad: the plan's language (P4b) — yellow = level, green = on, red = off,
 * amber = mid (Leslie slow), blue = special. A switch is never dim: its off state is red. */
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
    if (a->act == PAD_LESLIE) {                    /* P5: red off, amber chorale, green tremolo */
        int32_t r = TSEL->p[P_E11];
        return r == 1 ? PC_AMB : r ? PC_GRN : PC_RED;
    }
    if (a->act == PAD_DBAR)                        /* P5: a bar's pad is yellow, dim at level 0 */
        return clamp(TSEL->p[P_E0 + (a->param & 15u)], 0, 8) > 0 ? PC_YEL : PC_DIM;
    if (a->act == PAD_PERCHAR)                     /* P5: the harmonic half of the percussion switch */
        return (TSEL->p[P_E9] == 2 || TSEL->p[P_E9] == 4 || TSEL->p[P_E9] == 6) ? PC_GRN : PC_RED;
    if (a->act == PAD_PDEC)                        /* P5: the decay half */
        return (TSEL->p[P_E9] == 5 || TSEL->p[P_E9] == 6) ? PC_GRN : PC_RED;
    if (pad_is_switch(a->act))
        return pad_active(a) ? PC_GRN : PC_RED;
    if (pad_active(a))
        return a->col;
    if (a->col == PC_RED)                          /* a toggle that is off reads red */
        return PC_RED;
    return PC_DIM;
}

/* P5f: a recall pad's second line — the patch the slot holds. The CFW's OLED legend keeps the four
 * character U label (a cell is 27 px wide); the companion draws this under it, so a live set reads
 * as patch names instead of slot numbers. Empty slots say so; an armed slot says what to do. */
static char pad_sub_buf[16];
static const char *pad_sub_label(uint32_t pad)
{
    const pad_action_t *a = pad_get(pad);
    if (a->act != PAD_RECALL)
        return "";
    if (pad_arm_slot == a->param)
        return "HOLD TO SAVE";
    if (!up_used((uint32_t)a->param))
        return "EMPTY";
    up_name((uint32_t)a->param, pad_sub_buf);       /* upper case, 12 chars + NUL */
    return pad_sub_buf;
}

/* P6f: the boot animation's own colours (bootanim.c boot_pads). While it runs the 16 pads are a
 * loading bar, not the page's map: pad_rgb_value returns the table (so the companion's pad grid
 * and the legend strip paint the same bar the chain shows), the legends are blank, and the
 * drawbar rows are not bars. pad_boot_clear() hands the map back at the end of the 1 s. */
static uint32_t pad_boot_rgb[16];
static uint8_t pad_boot_on;
static void pad_boot_set(uint32_t pad, uint32_t rgb)
{
    pad_boot_rgb[pad & 15u] = rgb;
    pad_boot_on = 1;
}
static void pad_boot_clear(void)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        pad_boot_rgb[i] = 0;
    pad_boot_on = 0;
}
static uint32_t pad_boot_active(void) { return pad_boot_on; }
/* device: the table onto the WS2812 chain and latch it (smk_rgb_set's bytes are G, R, B) */
static void pad_boot_paint(void)
{
#ifdef SKM_ON_DEVICE
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        const uint32_t c = pad_boot_rgb[i];
        smk_rgb_set(i, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu, c & 0xFFu);
    }
    smk_rgb_show();
#endif
}

/* P5e: the pad's RGB as 0xRRGGBB, the one value the WS2812 chain and the companion both paint.
 * A drawbar's yellow brightens with its bar (level 1 about half of level 8), so the four pad pairs
 * read as four drawbars: pull a bar and its two pads climb out of the dark. */
static uint32_t pad_rgb_value(uint32_t pad)
{
    const pad_action_t *a = pad_get(pad);
    const uint8_t *c = PAD_RGB[pad_colour(a)];
    uint32_t k = 128u, lv;
    if (pad_boot_on)
        return pad_boot_rgb[pad & 15u];
    if (a->act == PAD_DBAR) {
        lv = (uint32_t)pad_level_get(pad);          /* 0..8 */
        if (lv)                                     /* 0 is PC_DIM already */
            k = 56u + lv * 9u;                      /* 65 .. 128 */
    }
    /* P5g: the Leslie pad breathes with the rotor - one revolution is one blink, taken from the horn's
     * own phase, so the pulse slows and speeds up with the ramp the ear hears (a ROTR flip eases over
     * ~0.4 s, not instantly). STOP is the only solid state (red): the rotor coasts, the pad keeps
     * blinking slower until the horn is under 0.3 Hz, then it is solid. */
    if (a->act == PAD_LESLIE) {
        int32_t mhz = wheel_horn_mhz(TSEL);                     /* the live ramp, milli-Hz (eng_wheel.c) */
        if (mhz >= 300) {                                       /* still turning: one blink a revolution */
            /* the horn's AM (eng_wheel.c: c = sine(rph + 90 deg)) is at unity at phase 0 - the horn
             * facing the mic - so the blink peaks there: fold the phase round 0 (0 = loudest,
             * 0x80000000 = the far side) and read it as a triangle. */
            uint32_t ph = wheel_horn_phase(TSEL);
            uint32_t tri;
            if (ph & 0x80000000u)
                ph = 0u - ph;                                   /* the far half, folded back */
            tri = ph >> 21;                                     /* 0 at the peak .. 1023 opposite */
            if (tri > 1023u)
                tri = 1023u;
            k = 44u + ((1023u - tri) * 84u) / 1023u;            /* 44 .. 128 */
        }
    }
    return (((uint32_t)c[0] * k) >> 7) << 16 | (((uint32_t)c[1] * k) >> 7) << 8 | (((uint32_t)c[2] * k) >> 7);
}

/* P5d: the recall slot (0..14) */
static uint32_t pad_recall_slot(uint32_t pad) { return pad & 15u; }

/* P5d: put slot k into the selected track (short press) */
static void pad_perf_recall(uint32_t slot)
{
    char l[5];
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
    char l[5];
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
    {   /* named so the recall surface reads as a live set: an occupied slot keeps its name (curated,
         * not re-typed), a fresh one takes the patch it was saved from (the user slot it holds, then
         * the factory preset, then the automatic "ENGINE nn"). P5f: that name is what the companion
         * prints under the pad, so a blank name would leave the pad unreadable. */
        char nm[13];
        nm[0] = 0;
        if (up_used(slot))
            up_name(slot, nm);
        else if (TSEL->user && up_used((uint32_t)TSEL->user - 1u))
            up_name((uint32_t)TSEL->user - 1u, nm);
        else {
            const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
            if (e && e->presets && TSEL->preset < e->npresets)
                str_cpy(nm, e->presets[TSEL->preset].name, sizeof nm);
        }
        if (up_store(slot, nm[0] ? nm : 0)) {
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

/* P5e: ride a drawbar from the pad (a drag in the companion, a hold's ramp on the device). The write
 * goes through pad_param_write, so a drag is captured like a knob sweep when AUTO REC is armed. */
static void pad_level_set(uint32_t pad, int32_t lv)
{
    int id = pad_level_id(pad);
    if (id < 0)
        return;
    lv = (int32_t)clamp(lv, 0, 8);
    if (TSEL->p[id] == lv)
        return;
    pad_param_write((uint32_t)id, lv);
    ui.force = 1;
}
static void pad_level_nudge(uint32_t pad, int32_t steps)
{
    int lv = pad_level_get(pad);
    if (lv >= 0 && steps)
        pad_level_set(pad, lv + steps);
}

/* P17: a value picked outright (the SCL / CHORD / ARP pages): the index is already in range */
static void pad_pick(uint32_t id, uint32_t idx, const char *const *names, const char *full)
{
    pad_param_write(id, (int32_t)idx);
    ui_message(names[idx] && names[idx][0] ? names[idx] : full);
    ui.force = 1;
}

/* A playable pad (P5b: DRUM's two rows) strikes the selected track directly. The pads no longer push
 * their own MIDI notes (P4: a pad is the page action), so the hit lives here; its release follows the
 * pad's, in smk_pads_run(). */
static uint8_t pad_held_note[16];
static uint32_t pad_do_idx;             /* the pad whose action is running (pad_do is also called by tests) */
#define PAD_HIT_VEL 100u
static void pad_strike(uint32_t pad, track_t *t, uint32_t note)
{
    pad_held_note[pad & 15u] = (uint8_t)note;
    input_on(t, note, PAD_HIT_VEL);
}

/* one pad's action (its edge already latched, a pending PERFORM save already cancelled) */
static void pad_do(const pad_action_t *a)
{
    uint32_t pad = pad_do_idx;
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
        pad_strike(pad, TSEL, DRUM_LANE_NOTE[a->param % NLANE]);
        break;
    case PAD_HIT:
        pad_strike(pad, TSEL, a->param);
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
    case PAD_DBAR: {                             /* P5: one drawbar step, the pair's top = down */
        uint32_t up = (a->param >> 4) & 1u;
        pad_level_nudge(pad, up ? 1 : -1);
        ui_message(a->full);
        ui.force = 1;
        break;
    }
    case PAD_LESLIE: {                           /* P5: OFF -> SLOW -> FAST */
        int32_t r = ((int32_t)TSEL->p[P_E11] + 1) % 3;
        pad_param_write(P_E11, r);
        ui_message(r == 0 ? "LESLIE OFF" : r == 1 ? "LESLIE SLOW" : "LESLIE FAST");
        ui.force = 1;
        break;
    }
    case PAD_PERC: {                             /* P5: percussion on (2nd, normal) / off */
        int32_t on = TSEL->p[P_E9] ? 0 : 1;
        pad_param_write(P_E9, on);
        ui_message(on ? "PERC ON" : "PERC OFF");
        ui.force = 1;
        break;
    }
    case PAD_PERCHAR: {                          /* P5: 2nd <-> 3rd (enables as 3rd when off) */
        int32_t cur = TSEL->p[P_E9], nx;
        if (cur == 0 || cur == 1)      nx = 2;
        else if (cur == 2)             nx = 1;
        else if (cur == 3)             nx = 4;
        else if (cur == 4)             nx = 3;
        else if (cur == 5)             nx = 6;
        else                           nx = 5;
        pad_param_write(P_E9, nx);
        ui_message(nx == 2 || nx == 4 || nx == 6 ? "PERC 3RD" : "PERC 2ND");
        ui.force = 1;
        break;
    }
    case PAD_PDEC: {                             /* P5: fast <-> slow (enables as fast when off) */
        int32_t cur = TSEL->p[P_E9], nx;
        if (cur == 0 || cur == 1)      nx = 5;
        else if (cur == 5)             nx = 1;
        else if (cur == 2)             nx = 6;
        else if (cur == 6)             nx = 2;
        else if (cur == 3)             nx = 5;
        else                           nx = 6;
        pad_param_write(P_E9, nx);
        ui_message(nx == 5 || nx == 6 ? "PERC SLOW" : "PERC FAST");
        ui.force = 1;
        break;
    }
    case PAD_CLICK: {                            /* P5: key click on / off (40 = the preset level) */
        int32_t on = TSEL->p[P_E10] ? 0 : 40;
        pad_param_write(P_E10, on);
        ui_message(on ? "CLICK ON" : "CLICK OFF");
        ui.force = 1;
        break;
    }
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
    if (ui.game) {                                  /* P19/P20: the pads belong to the game */
        for (i = 0; i < 16u; i++)
            if ((ed >> i) & 1u)
                sk_game_pad(i);
        return;
    }
    for (i = 0; i < 16u; i++) {
        const pad_action_t *a = pad_get(i);
        if (((rel >> i) & 1u) && pad_held_note[i]) {  /* P5b: a struck drum pad lets go with the pad */
            input_off(TSEL, pad_held_note[i]);
            pad_held_note[i] = 0;
        }
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
        pad_do_idx = i;
        pad_do(a);
    }
    (void)pad_recall_slot;

    /* P5e: a held drawbar pad ramps — the device's "press and drag". The pads are switches, so the
     * gesture is press and hold: the edge stepped it once (pad_do), then after 350 ms it takes a
     * step every 110 ms until the pad is let go (or the bar hits an end). Without this a bar had to
     * be tapped up to eight times to go from 0 to 8. */
    {
        static uint32_t ramp_t0[16];                 /* press time | 1 */
        static uint32_t ramp_next[16];               /* the next repeat, fm1_ms */
        uint32_t down = smk_pad_down_mask();
        for (i = 0; i < 16u; i++) {
            const pad_action_t *a = pad_get(i);
            if (a->act != PAD_DBAR) {
                ramp_t0[i] = 0;
                continue;
            }
            if ((ed >> i) & 1u) {                    /* the edge just stepped it */
                ramp_t0[i] = fm1_ms | 1u;
                ramp_next[i] = 0;
                continue;
            }
            if (!((down >> i) & 1u)) {               /* released: forget the hold */
                ramp_t0[i] = 0;
                continue;
            }
            if (!ramp_t0[i]) {
                ramp_t0[i] = fm1_ms | 1u;
                continue;
            }
            if (fm1_ms - (ramp_t0[i] & ~1u) < 350u)
                continue;
            if (!ramp_next[i])
                ramp_next[i] = fm1_ms;
            else if (fm1_ms < ramp_next[i])
                continue;
            ramp_next[i] = fm1_ms + 110u;
            {
                int32_t before = pad_level_get(i);
                pad_level_nudge(i, ((a->param >> 4) & 1u) ? 1 : -1);
                if (pad_level_get(i) != before)
                    ui_message(a->full);             /* the bar's own name while it rides */
            }
        }
    }
}

/* paint the chain from the map (P4). Device only: the host has no WS2812 chain. */
static void smk_pads_led(void)
{
#ifdef SKM_ON_DEVICE
    uint32_t i;
    if (ui.game) {                                  /* P19/P20: the chain is the game's display */
        sk_game_pads();
        return;
    }
    for (i = 0; i < 16u; i++) {
        uint32_t c = pad_rgb_value(i);
        smk_rgb_set(i, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
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

/* P5g: the pads on the screen. 16 cells in two rows of eight — the pads' own order — each filled with
 * the colour its LED has this frame and labelled with the map's short name, so what a pad does and what
 * it is telling you are both on the display at a glance. ui_draw.c blits it at the bottom of the HOME
 * panel (SMK_PGUIDE). The cells are 28 x 10: AF_XS's four characters fit, and the ink flips to dark on a
 * bright LED (yellow / green / amber) and light on a dark one (off, blue) so the label stays legible. */
static void smk_pads_guide(int32_t y, uint32_t h)
{
    const uint32_t cw = 28u, ch = 10u;
    const uint32_t pal = settings.palette % NPALETTES;
    const uint8_t mono = (uint8_t)(pal == UI_GREY_INDEX || pal == UI_BW_INDEX);   /* the two colour-free palettes */
    uint32_t i;
    cv_begin(240u, h, T_BG);
    cv_rect(3, 0, 234, (int32_t)h, T_SURF);
    cv_bg = T_SURF;
    for (i = 0; i < 16u; i++) {
        const pad_action_t *a = pad_get(i);
        const uint32_t rgb = pad_rgb_value(i);
        const int32_t r = (int32_t)((rgb >> 16) & 0xFFu), g = (int32_t)((rgb >> 8) & 0xFFu), b = (int32_t)(rgb & 0xFFu);
        const int32_t x = 4 + (int32_t)(i & 7u) * 29, yy = 2 + (int32_t)(i >> 3) * 11;
        const int32_t lum = (r * 77 + g * 150 + b * 29) >> 8;   /* perceived brightness, 0..255 */
        const uint8_t lit = (uint8_t)((r | g | b) != 0);
        uint16_t fill, ink;
        if (mono) {                                    /* GREY / MONO: the state on the theme's ramp, never a hue */
            fill = lit ? ux_mix(ux.surf, ux.text, 26 + lum * 74 / 255) : T_RAISE;
        } else {
            fill = lit ? (uint16_t)RGB(r, g, b) : T_RAISE;   /* off: an empty slot, not a hole in the panel */
        }
        ink = (lit && lum > 96) ? (mono ? ux.bg : T_BG) : T_TEXT;
        GFX_HOOK_ALIGN(x, yy, x + (int32_t)cw, yy + (int32_t)ch, AL_HV, "pad guide cell");
        cv_rrect(x, yy, (int32_t)cw, (int32_t)ch, 2, fill, T_SURF);
        cv_text_in(x, yy, (int32_t)cw, &AF_XS, a->abbr, ink, fill);
    }
    cv_blit(0, (uint32_t)y);
}
