/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* StudioKEYS Micro core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of NVOICE sounding voices (voice.c). Drums are the DRUM
 * engine (General MIDI map) on any part; the SAMPLE engine's PERC set is retired (drum_from_perc).
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>

/* ------------------------------------------------------------ sizes --- */

/* The keyboard's voice pool and budget, in one place (voice_budget.h): VOICE_POOL is the arena the
 * four parts draw from, and the engine_t.cost weights decide how many of it each part gets (voice.c).
 * The engines whose per-voice side state is expensive cap themselves below it (PHYS 3, GR 3, FM6 6,
 * WHEEL/SLICE/DIGITAL 8). */
#include "voice_budget.h"

/* The pool is also each part's cap: the point of a shared arena is that one part may take all of it
 * (a 24-note pedalled piano), so NVOICE is just the pool's name where a loop means "every voice". */
#ifndef VOICE_POOL
#define VOICE_POOL SKM_VOICE_POOL     /* host Unleashed may raise this at compile time (128) */
#endif
#define NVOICE VOICE_POOL
/* The hardware's UNISON width. This stays 8 even when a host raises NVOICE for Unleashed: the
 * unison detune spread and level are part of the sound, so the companion must match the device. */
#define SK_UNISON 8
#define NPART 4                  /* synth parts: tracks 1..4 */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
#define NSTEP 64
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
#ifndef SKM_SLICE
#define SKM_SLICE 1          /* the SLICE engine (eng_slice.c), engine 13; SKM_SLICE=0 builds without it */
#endif
#ifndef SKM_FM4
#define SKM_FM4 0            /* the DIGITAL engine (eng_digital.c, four-operator FM): kept in the tree, not built
                                  * by default; replaced by FM6, its sounds convert (fm4_convert.c) */
#endif
#define NENGINES (13 + SKM_SLICE)   /* SLICE (13) comes last: the other engines keep their numbers */
#define ENGI_DIGITAL 1u          /* reserved without SKM_FM4: never selectable (eng_ok), its sounds load as FM6 */
#define NENG_SHOWN (NENGINES - !SKM_FM4)   /* the engines one can pick: PRESETS, the EDIT layer, the editor,
                                                * in the display order of engines.c ENGINE_ORDER */
/* UP_SLOTS has two ceilings and one design choice. The hard ceiling is RAM: a save runs with the flash
 * (and so XIP) unavailable, so the mirror it writes from lives in RAMTEXT with the flash driver:
 * UP_BANKS x (UP_PER_BANK x 192 + 8) = UP_BANKS x 3,080 B. RAMTEXT is 0x01C00000..0x01C06000
 * (24,576 B), .ram_text takes 2,924 of it, which leaves 21,652 B = 7 banks = 112 slots (upreset.c
 * asserts this). The FM6 patch store is a softer ceiling: UPF_BANKS is 4 (OBJ_UPFM6_0..3) at UPF_PER
 * 25, so the patch banks cover 100 slots. The flash sectors are not the limit at all (storage.c: the
 * bank region is fixed at 0xEE000..0xF7FFF, 13 sectors free below it).
 *
 * 90 is the UI number, not a memory one: the PERFORM page is 15 recall pads + 1 edit jump
 * (ui_pads.c perf_pad), and 6 pages is the wanted count, so 6 x 15 = 90.
 *
 * The change from 100 is deliberately surgical so an upgrade cannot corrupt data:
 *   - UP_BANKS drops 7 -> 6; the 7th bank's sector pair at 0xF7000 is simply never written again, and
 *     every object id / sector address in storage.c is unchanged (FL_PRE_HI..FL_UPF_BASE stay put).
 *   - UPF_PER stays 25 and UPF_BANKS stays 4, because 90 slots still span the same four banks
 *     (bank 3 covers 75..99). Slots 0..89 keep the bank and record index they already had, so an
 *     existing U01..U90 keeps its patch and its FM6 voice; only U91..U100 stop being addressable.
 *   - UP_FAV_WORDS stays 4 even though 90 needs only 3 (see its comment: it is part of the persisted
 *     settings blob's size, and shrinking it would invalidate every saved settings block). */
#define UP_SLOTS 90u             /* user presets: 6 PERFORM pages of 15 (see the ceilings above) */
#define UP_PER_BANK 16u
#define UP_BANKS ((UP_SLOTS + UP_PER_BANK - 1u) / UP_PER_BANK)
/* The persisted favorites width is part of the settings blob's size (settings_persist.c keys
 * validity off sizeof(persist_t)), so it must NOT track UP_SLOTS: shrinking it when the slot count
 * drops would invalidate every saved settings block on upgrade. Fixed at 4 words (128 bits); the
 * bits above UP_SLOTS are simply never set (favorites_set rejects preset >= UP_SLOTS). Re-deriving
 * this from UP_SLOTS would need a size migration - see settings_persist.c PER4/PER5. */
#define UP_FAV_WORDS 4u                          /* favorites.c: one bit per user preset, persisted */
_Static_assert(UP_FAV_WORDS * 32u >= UP_SLOTS, "favorites: the persisted word count must cover UP_SLOTS");
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: unused, kept for the formats / protocol */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_E8, P_E9, P_E10, P_E11,                  /* engine params 9..12 (P6): WHEEL's 9 drawbars + REG/DRV/VIB; */
    P_COUNT                                    /* empty (default 0) until an engine's edit[] reads one */
};

/* P5: a HOME knob slot that is an action, not a stored parameter. WHEEL's K6 is the REG shortcut:
 * a turn writes the nine drawbars (eng_wheel.c wheel_reg_*), so it has no slot in track_t.p[]. */
#define P_VREG_REG 0xFFFEu

/* eng_wheel.c: the registration shortcut (P5) */
const char *wheel_reg_name(int32_t r);
const char *const *wheel_reg_names(void);
int32_t wheel_reg_match(const int16_t *p);
void wheel_reg_apply(int16_t *p, int32_t r);
/* P21: the easter eggs take incoming MIDI as input. The MIDI path (midi_control.c, in the audio
 * path) only posts the newest note-on here; game.c consumes it from the frame loop, so the sim
 * is still stepped from one place and the ISR never touches it. */
extern volatile uint8_t sk_gi_note;
extern volatile uint32_t sk_gi_seq;

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,   /* G_ROUTE: MIDI IN, 0 MULTI (ch 1 the pretend keyboard, 2..5 parts 1..4, 6..16 ignored), 1 SEL (seq.c)
                                        * G_INFO: the CPU meter on SYSTEM, and the way in to DIAGNOSTICS (OCT+, ui_diag.c) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page: the editor switches the engine with a SET of G_ENGSEL; G_ENGGO is
                                 * unused (ids are fixed by the formats and the protocol) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING (fx.c). Was G_DRCH, the GM drum part's MIDI
                                 * channel (inert since 1.0, never read); projects of formats before FUN7 load it
                                 * as ROOM (project.c proj_rtype_room) */
    G_DRLVL, G_DRREV,           /* inert (label "-", on no page): the GM drum part they set is gone; kept
                                 * because the ids and G_COUNT are fixed by the formats and the protocol (only
                                 * the import of an old project reads them: proj_drums_to_part) */
    G_COUNT
};

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - NE are
 * P_LEVEL.. in order and the last NE are the engine block (P_E0..). NE is 8 for every store that
 * predates P6 (engine params 8 -> 12): its engine values land in P_E0..P_E7 and P_E8..P_E11 take
 * def[]; a store written with 12 (np - 8 >= P_E0) carries all of them (user presets, projects). */
#define ENG_OLD 8u                              /* engine params a store before P6 carries */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, ne = np >= P_E0 + 12u ? 12u : ENG_OLD, nc = np - ne;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < ne; i++)
        out[P_E0 + i] = in[nc + i];
    for (i = ne; i < 12u; i++)
        out[P_E0 + i] = def[P_E0 + i];
}

/* WHEEL's engine params before the nine drawbars (P5): {REG, SUB, BODY, TOP, PERC, CLICK, DRV, ROTR}
 * -> today's nine bars + PERC/CLICK/ROTR (eng_wheel.c). np = the record's value count, as
 * params_by_count takes it: a record with 8 engine values is migrated, a 12-value one is left
 * alone. Projects (project.c proj_trk_from) and user presets (upreset.c up_values) */
static void wheel_migrate(int16_t *p, uint32_t np);

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_WHEEL 7u
#define ENGI_PHYS 9u
#define ENGI_DRUM 10u
/* PHYS MODEL DRUM (MODEL 4, before 1.0) -> the DRUM engine, its E values in place: {MODEL, TUNE, TONE, DECY,
 * SNAP, ACC, KICK 0..127, PERC 0..127} -> {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK 0..1, DRV 0}. 1 = it was one
 * (its engine is ENGI_DRUM now); projects (project.c) and user presets (upreset.c) */
static int drum_from_phys(uint32_t engine, int16_t *e)
{
    if (engine != ENGI_PHYS || e[0] != 4)
        return 0;
    e[0] = (int16_t)((e[7] < 0 ? 0 : e[7] > 127 ? 127 : e[7]) >> 5);   /* PERC -> KIT: STD HAND CYM H+CYM */
    e[6] = (int16_t)(e[6] >= 64);                                       /* KICK: PUNCH, ROUND */
    e[7] = 0;
    return 1;
}

/* SAMPLE SET 4 was PERC, the GM-mapped drum kit (tools/gen_samples.py); retired after 1.0.2: its index stays
 * (SET / GRAIN SRC 4 is an alias of the strings, USR1..3 keep 5..7). A sound that selected it is the DRUM engine with
 * its default kit (eng_drum.c DRUM_PRESETS[0]: the same GM key map, so its patterns still play as drums): its E
 * values become the kit's, the rest of the sound (mix, sends, matrix, ..) stays. 1 = it was one (its engine is
 * ENGI_DRUM now); projects (project.c proj_perc), user presets (upreset.c up_migrate), factory preset 4 and
 * favourites (ui.c, settings_persist.c). Idempotent */
#define ENGI_SAMPLE 4u
#define SMP_SET_PERC 4u
#define DRUM_KIT_E {0, 64, 70, 64, 64, 100, 0, 0}   /* {KIT STD, TUNE, TONE, DECY, SNAP, ACC, KICK PUNCH, DRV} */
static int drum_from_perc(uint32_t engine, int16_t *e)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    uint32_t i;
    if (engine != ENGI_SAMPLE || e[0] != (int16_t)SMP_SET_PERC)
        return 0;
    for (i = 0; i < 8u; i++)
        e[i] = KIT[i];
    return 1;
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
#define VOICE_FREE 0xFFu                       /* voice_t.part: not owned by any part */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    uint8_t part;                /* the part that owns it (pools: voice.c vpool), VOICE_FREE = none */
    uint8_t pi;                  /* its index within that part (what t->v[i] used to be): 0 is the lead */
    uint8_t sslot;               /* its slot in the engine's per-voice side state (PHYS, GRAIN, FM6, ..) */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    int16_t mrnd;
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend (1/4096) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[12];                /* P_E0..P_E11 (P6: 8 -> 12; signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t pat;                 /* suggested pattern (PATTERNS[pat - 1], 0 = none): only a hint. A sound load
                                  * never touches the steps; SEQ > PATTERNS loads a pattern (ui.c pat_load). The
                                  * arp is the track's too: a preset does not set it */
    uint8_t glide;               /* P_GLIDE: stored + 1, 0 = keep the track default after load */
    uint8_t glmode;              /* P_GLMODE: stored + 1, 0 = keep the track default after load */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define PAT(n) .pat = (n)
#define GLIDE(n) .glide = (n) + 1
#define GLMODE(n) .glmode = (n) + 1

struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[12];       /* P_E0..P_E11 (P6: 8 -> 12; engines with fewer leave the tail empty) */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (DRUM): always POLY, no glide; amp ends its voices (eng_drum.c) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional: an engine whose sound has a stereo image of its own (WHEEL's rotary). It fills out
     * (M = L + R) and side (S = L - R) instead of render; mix_part (fx.c) pans M +- S, so a mono
     * fold-down is exactly out and PAN / the sends still work on the whole part. */
    void (*render_stereo)(struct track *t, voice_t *v, int32_t *out, int32_t *side, uint32_t n,
                          const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
    /* the voice's cost, in host instructions per 44.1 kHz sample (tests/cpu_baseline.txt, idle
     * subtracted, per voice). The shared budget weighs every voice by it, so the same CPU buys 30
     * SAMPLE voices or 12 PHASE ones: one number per engine instead of a hand-set cap per engine. */
    uint16_t cost;
    /* optional: the cost of THIS part's voice (ANALOG's seven-saw waves cost 4x its two-oscillator
     * ones). 0 = engine_t.cost */
    uint16_t (*cost_of)(const struct track *t);
} engine_t;

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RATCH_SH 3u                   /* RATCH: the hits of a NOTE step - 1 (0..3: x1..x4, seq.c seq_ratchet); */
#define SF_RATCH (3u << SF_RATCH_SH)     /* bits 3..4, so a user preset's pattern (flag 4 = tie) carries it too */
#define NLANE 8                  /* drum lanes of a step (the DRUM engine's: eng_drum.c DRUM_LANE_NOTE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE | SF_RATCH */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its GM note, on any engine): the DRUM grid */
    uint8_t acc;                 /* bit l: that hit is accented (velocity 127) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent */
} step_t;
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
static uint32_t step_chance(const step_t *s) { return !s->probability ? 100u : s->probability <= 100u ? s->probability : 0u; }
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
/* RATCH: a NOTE step plays its notes and hits this many times (1..4), in equal parts of the step; every older
 * pattern holds 0 there, x1 */
static uint32_t step_ratchet(const step_t *s) { return ((s->flags & SF_RATCH) >> SF_RATCH_SH) + 1u; }
static void step_set_ratchet(step_t *s, uint32_t hits)
{
    s->flags = (uint8_t)((s->flags & ~SF_RATCH) | ((hits < 1u ? 0u : hits > 4u ? 3u : hits - 1u) << SF_RATCH_SH));
}
#define MOTION_MAX 64u
/* Four tracks x64 steps fit one byte. Values retain their signed parameter range. */
typedef struct { uint8_t place, param; int16_t value; } motion_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 260u, "motion disk layout");
#define CHAIN_ROWS 16u
typedef struct { uint8_t slot, repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    /* its voices live in the shared vpool (core.h): voice_at(), voice_lead() */
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint8_t rat_left;            /* RATCH: repeats of the playing step still to come (seq.c seq_ratchet) */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[12];          /* P_E0..P_E11 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
} track_t;

/* eng_wheel.c: the high rotor as the audio is playing it (P5g: the Leslie pad breathes one blink a
 * revolution). Phase is the horn's own 32-bit angle, and the AM is at unity at phase 0 (the horn
 * facing the mic, the loudest point of the turn); the speed is the eased one the block is using right
 * now, not P_E11's target, so the pad ramps with the ear instead of snapping to the new mode's rate. */
uint32_t wheel_horn_phase(const track_t *t);
int32_t wheel_horn_mhz(const track_t *t);

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: four parts */
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */

/* ---------------------------------------------------- the voice pool --- */
/* The arena voice.c allocates from, and the accessors that replace the old per-part t->v[i]. A voice
 * is owned by one part while it sounds (part == its index) and given back when that part needs the
 * memory for another part: the whole point is that the four parts share one pool instead of each
 * reserving NVOICE (see NVOICE / VOICE_POOL at the top). */
static voice_t vpool[VOICE_POOL];

/* the part's voice whose pi is k (0 = the part has none there) */
static voice_t *voice_at(const track_t *t, uint32_t k)
{
    uint32_t p = (uint32_t)(t - trk), i;
    for (i = 0; i < VOICE_POOL; i++)
        if (vpool[i].part == p && vpool[i].pi == k)
            return &vpool[i];
    return 0;
}

/* the part's lead voice (pi 0: MONO / LEGATO / UNISON), 0 = none */
static voice_t *voice_lead(const track_t *t)
{
    voice_t *v = voice_at(t, 0);
    return v && v->part != VOICE_FREE ? v : 0;
}

/* the part's voice that owns side-state slot s (engines index their tables with it) */
static voice_t *voice_sslot_at(const track_t *t, uint32_t s)
{
    uint32_t p = (uint32_t)(t - trk), i;
    for (i = 0; i < VOICE_POOL; i++)
        if (vpool[i].part == p && vpool[i].sslot == s)
            return &vpool[i];
    return 0;
}

/* how many voices the part owns */
static uint32_t voice_owned(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), i, n = 0;
    for (i = 0; i < VOICE_POOL; i++)
        n += vpool[i].part == p;
    return n;
}

/* voice.c: the arena's power-on / panic state (main.c skm_init, the console), 1 when one more voice of
 * the part does not fit the load budget (the tests fill the pool with it), and the budget's feedback
 * from audio.c (skm_diag_read's CPU load) */
void voices_reset(void);
int voice_full(const track_t *t);
uint32_t voice_budget_cap(void);
void voice_budget_shed(void);
void voice_budget_rest(void);

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */

/* The engine's input mix (audio_in.c): sound from outside the synth. SKM_UAC_PLAY adds the USB audio
 * playback stream (usb.c). Off by default: the playback endpoint needs a hardware pass (see usb.c). */
#ifndef SKM_UAC_PLAY
#define SKM_UAC_PLAY 0
#endif

/* MENU > SYSTEM > TEST MODE, the panel's QA screen (ui_input.c). On by default
 * so hardware testing is always available on demand. */
#ifndef SKM_TEST_MODE
#define SKM_TEST_MODE 1
#endif

/* The 24 px icon atlas (icons.c, tools/gen_aa_icons.py --sizes): 4,228 B for the header battery / USB
 * plug and the FX map's effect cells, which fall back to 16 px when it is clear (SPACE_BUDGET.md §4 C4). */
#ifndef SKM_ICON24
#define SKM_ICON24 0
#endif
#ifndef SKM_AUDIO_IN
#define SKM_AUDIO_IN SKM_UAC_PLAY
#endif
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));

/* One copy of what the DIAG page shows (ui_diag.c), filled by audio.c skm_diag_read: the rate lock, the
 * render's health and where the last boot died. A struct and not globals so the page can be drawn from a
 * snapshot the audio side never writes under it. */
typedef struct {
    uint32_t rate_mhz;          /* I2S frames measured against TIMER4, milli-Hz; 0 = not measured yet */
    int32_t ppm;                /* the same rate against 44,100, ppm */
    uint32_t trim_q16;          /* skm_pitch_trim: what dsp.c scales the increments by */
    uint32_t wins;              /* accepted measurement windows since boot */
    uint32_t cpu_q8;            /* audio ISR load, 1/256 */
    uint32_t max_us;            /* the longest render since boot */
    uint32_t late;              /* halves the ISR fell behind in */
    uint32_t halves;            /* halves rendered since boot */
    uint32_t boots;             /* boots since the counters were cleared */
    uint32_t boot_failed;       /* bootguard: deaths in the first 30 s, this boot */
    uint32_t prev_stage;        /* breadcrumbs as the last boot left them */
    uint32_t prev_page, prev_home, prev_rst;
} skm_diag_t;
