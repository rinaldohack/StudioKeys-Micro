/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* P19 / P20 — the two easter eggs, FLAPPY and TETRIS.
 *
 * One deterministic simulation each, shared by the CFW (it draws on the 240x240 panel and
 * repurposes the 16 drumpad LEDs) and the companion (which renders the same state in a bigger
 * window). Nothing is stored: no flash record, no parameter, no game audio.
 *
 * The point is the mute: opening either game silences the instrument (audio.c checks
 * sk_game_mute()). jack and USB take the same zeroed master, so every output is silent.
 *
 * While the cable-3 link is locked the COLLAR (the plug-in) is the authority: it owns the one
 * simulation and ships the state; the keyboard is a pure mirror (it never steps its own copy),
 * so the two frames cannot drift. Either side may input: the keyboard forwards the op and the
 * authority applies it and reflects the new state back. See sk_link_lock.c / host/sk_host.c.
 *
 * Everything here is integer math (no floats) so a state shipped over the wire lands on the
 * same picture on both sides. Sim ticks are 16 ms; the frame loop accumulates the real time,
 * so a slow panel redraw never bends the simulation's speed. */

#ifndef SK_GAME_FLAPPY
#ifndef SK_GAME_NONE
#define SK_GAME_NONE 0
#define SK_GAME_FLAPPY 1
#define SK_GAME_TETRIS 2
#endif
#endif

/* what an input/op packet carries (also the link's op byte). SK_GIN_* live in ui.c, above. */
/* link packet kinds */
enum {
    SK_GPKT_OPEN = 0,  /* blob only: request / announce */
    SK_GPKT_STATE_F,   /* flappy: the whole state */
    SK_GPKT_INPUT,     /* one op byte */
    SK_GPKT_STATE_T0,  /* tetris: header + rows 0..8 */
    SK_GPKT_STATE_T1,  /* tetris: header + rows 9..17 */
    SK_GPKT_CLOSE      /* blob only: the game was closed; the other side closes too */
};

/* ------------------------------------------------------------ flappy --- */
#define FL_TICK_MS 16u
#define FL_BIRD_X 58
#define FL_BIRD_HW 7
#define FL_BIRD_HH 6
#define FL_GATE_W 34
#define FL_GATE_STEP 96
#define FL_GRAV 48          /* Q8 px / tick^2 */
#define FL_FLAP (-930)      /* Q8 px / tick */
#define FL_VMAX 1100        /* Q8 px / tick */
#define FL_SPEED 256        /* Q8 px / tick: 1 px a tick */
#define FL_GROUND 232

typedef struct {
    int32_t y, vy;          /* Q8: the bird's centre */
    int16_t spawn_x;        /* px: where the next gate appears (the link's copy; the sim recycles a gate in place) */
    int16_t gx[3];          /* px: negative once a gate leaves the left edge, never past ~245 */
    int16_t gy[3];          /* px: the gap's centre */
    uint8_t gh[3];          /* px: the gap's height */
    uint8_t scored[3];
    uint16_t score;
    uint8_t alive, started;
} flappy_t;

/* ------------------------------------------------------------ tetris --- */
#define TET_COLS 10u
#define TET_ROWS 18u
#define TET_CELLS (TET_COLS * TET_ROWS)
#define TET_TICK_MS 16u
#define TET_START_MS 420u       /* gravity at level 1 */
#define TET_MIN_MS 90u

/* the spawn shapes, 4x4 bitmaps (bit y*4 + x) */
static const uint16_t TET_SHAPE[7] = {
    0x0F00,  /* I */
    0x0660,  /* O */
    0x0E40,  /* T */
    0x00E1,  /* J */
    0x00E2,  /* L */
    0x00C6,  /* S */
    0x006C   /* Z */
};
/* the pads' colour per shape (GRB, roughly the usual tetromino hues) */
static const uint8_t TET_RGB[7][3] = {
    {0xE0, 0x20, 0x20},   /* I cyan */
    {0xC0, 0xC0, 0x10},   /* O yellow */
    {0xA0, 0x10, 0xA0},   /* T purple */
    {0xC0, 0x30, 0x10},   /* J blue */
    {0x10, 0x60, 0xC0},   /* L orange */
    {0x20, 0xB0, 0x20},   /* S green */
    {0x10, 0x10, 0xC0}    /* Z red */
};

typedef struct {
    uint8_t board[TET_CELLS];
    /* P21: px is SIGNED. A 4x4 shape with an empty left column (the I bar stood up, the S/Z) may
     * move one column past x=0 while all of its cells stay on the board; as a uint8 that move
     * wrapped to 255, and tet_draw / the companion skipped every cell as out of range — the
     * falling block simply disappeared. py is signed for the same reason (a shape's empty top
     * rows), and both are packed as px+1 so a negative value still fits a 7-bit byte. */
    int8_t px, py;
    uint8_t piece, rot, next0, next1;
    uint8_t over;
    uint16_t score, lines;
} tetris_t;

/* ------------------------------------------------------------- state --- */
static struct {
    uint8_t kind;          /* SK_GAME_* */
    uint8_t mirror;        /* 1: the link's authority owns the sim */
    uint8_t dirty;         /* redraw needed (tetris: only on change) */
    uint8_t pad_flash;     /* frames the pads stay lit after a flap */
    uint32_t last_ms;
    uint32_t accum;
    uint32_t rng;
    uint32_t next_drop;
    uint32_t rev;          /* bumped on every state change, for the link */
    flappy_t fl;
    tetris_t tet;
} gm;

/* a fixed seed: both sides start from the same sequence when they open together */
#define GM_SEED 0x51ED270Bu

static uint32_t gm_rand(void)
{
    uint32_t x = gm.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    gm.rng = x;
    return x;
}

/* ---------------------------------------------------------- link hook --- */
#if SKM_OTA
static void skl_game_tx(const uint8_t *d, uint32_t n);   /* sk_link_lock.c */
static void sk_game_tx(const uint8_t *d, uint32_t n) { skl_game_tx(d, n); }
#elif defined(SKM_SMK_PANEL)
void sk_host_game_tx(const uint8_t *d, uint32_t n);      /* host/sk_host.c */
static void sk_game_tx(const uint8_t *d, uint32_t n) { sk_host_game_tx(d, n); }
#else
static void sk_game_tx(const uint8_t *d, uint32_t n) { (void)d; (void)n; }
#endif

uint8_t sk_game_mute(void) { return gm.kind != SK_GAME_NONE; }
uint8_t sk_game_kind(void) { return gm.kind; }
uint32_t sk_game_rev(void) { return gm.rev; }
void sk_game_set_mirror(uint8_t on)
{
    if (gm.mirror == (on ? 1u : 0u))
        return;
    gm.mirror = on ? 1u : 0u;
    if (gm.mirror)                                  /* the mirror's clock is the authority's */
        gm.accum = 0;
    gm.dirty = 1;
}

/* --------------------------------------------------------------- flappy --- */
static uint8_t fl_gap_h(void)
{
    int32_t h = 78 - (int32_t)gm.fl.score * 2;
    return (uint8_t)(h < 54 ? 54 : h);
}

static int16_t fl_gap_y(uint8_t gh)
{
    int32_t lo = gh / 2 + 18, hi = 240 - gh / 2 - 18;
    int32_t span = hi - lo + 1;
    return (int16_t)(lo + (int32_t)(gm_rand() % (uint32_t)span));
}

static void fl_reset(void)
{
    uint32_t i;
    gm.rng = GM_SEED;
    memset(&gm.fl, 0, sizeof gm.fl);
    gm.fl.y = 120 << 8;
    gm.fl.alive = 1;
    gm.fl.spawn_x = 240 + FL_GATE_STEP * 3;
    for (i = 0; i < 3u; i++) {
        gm.fl.gx[i] = (int16_t)(240 + (int32_t)i * FL_GATE_STEP);
        gm.fl.gh[i] = fl_gap_h();
        gm.fl.gy[i] = fl_gap_y(gm.fl.gh[i]);
        /* P21: every gate starts AHEAD of the bird, so every gate is worth a point. These used to
         * be flagged as already scored ("the first three are off-screen"), which silently ate the
         * first three pipes of every round: the bird clearly passed them and the counter stayed 0
         * until a gate had wrapped around once. */
        gm.fl.scored[i] = 0;
    }
    gm.rev++;
    gm.dirty = 1;
}

static void fl_flap(void)
{
    flappy_t *f = &gm.fl;
    if (!f->alive) {
        fl_reset();
        return;
    }
    f->started = 1;
    f->vy = FL_FLAP;
    gm.pad_flash = 6;
    gm.rev++;
    gm.dirty = 1;
}

static void fl_step(void)
{
    flappy_t *f = &gm.fl;
    int32_t by0, by1;
    uint32_t i;
    if (!f->alive)
        return;
    f->vy += FL_GRAV;
    if (f->vy > FL_VMAX)
        f->vy = FL_VMAX;
    f->y += f->vy;
    gm.rev++;                                  /* every tick moves the bird: the mirror repaints */
    by0 = (f->y >> 8) - FL_BIRD_HH;
    by1 = by0 + FL_BIRD_HH * 2;
    if (by0 < 0 || by1 > FL_GROUND) {
        f->alive = 0;
        gm.rev++;
        gm.dirty = 1;
        return;
    }
    for (i = 0; i < 3u; i++) {
        int32_t g = f->gx[i];
        int32_t gap_top, gap_bot;
        f->gx[i] = (int16_t)(g - (FL_SPEED >> 8));
        if (f->gx[i] < -(FL_GATE_W + 8)) {         /* gone: back three gate widths right (96 px apart) */
            f->gx[i] = (int16_t)(f->gx[i] + FL_GATE_STEP * 3);
            f->spawn_x = (int16_t)(f->gx[i] + FL_GATE_STEP);
            f->gh[i] = fl_gap_h();
            f->gy[i] = fl_gap_y(f->gh[i]);
            f->scored[i] = 0;
            g = f->gx[i];
        }
        gap_top = (int32_t)f->gy[i] - f->gh[i] / 2;
        gap_bot = (int32_t)f->gy[i] + f->gh[i] / 2;
        if (g < FL_BIRD_X + FL_BIRD_HW && g + FL_GATE_W > FL_BIRD_X - FL_BIRD_HW) {
            if (by0 < gap_top || by1 > gap_bot) {
                f->alive = 0;
                gm.rev++;
                gm.dirty = 1;
                return;
            }
        }
        if (!f->scored[i] && g + FL_GATE_W < FL_BIRD_X - FL_BIRD_HW) {
            f->scored[i] = 1;
            f->score++;
            gm.rev++;
            gm.dirty = 1;
        }
    }
    gm.dirty = 1;
}

/* --------------------------------------------------------------- tetris --- */
static uint16_t tet_rot(uint16_t m)
{
    uint16_t r = 0;
    uint32_t x, y;
    for (y = 0; y < 4u; y++)
        for (x = 0; x < 4u; x++)
            if (m & (1u << (y * 4u + x)))
                r |= (uint16_t)(1u << (x * 4u + (3u - y)));
    return r;
}

static uint16_t tet_mask(void)
{
    uint16_t m = TET_SHAPE[gm.tet.piece % 7u];
    uint32_t i;
    for (i = 0; i < gm.tet.rot % 4u; i++)
        m = tet_rot(m);
    return m;
}

static int tet_hit(int32_t x, int32_t y, uint16_t m)
{
    uint32_t dx, dy;
    for (dy = 0; dy < 4u; dy++)
        for (dx = 0; dx < 4u; dx++) {
            int32_t bx, by;
            if (!(m & (1u << (dy * 4u + dx))))
                continue;
            bx = x + (int32_t)dx;
            by = y + (int32_t)dy;
            if (bx < 0 || bx >= (int32_t)TET_COLS || by >= (int32_t)TET_ROWS)
                return 1;
            if (by >= 0 && gm.tet.board[by * TET_COLS + bx])
                return 1;
        }
    return 0;
}

static void tet_spawn(void)
{
    gm.tet.piece = gm.tet.next0;
    gm.tet.next0 = gm.tet.next1;
    gm.tet.next1 = (uint8_t)(gm_rand() % 7u);
    gm.tet.rot = 0;
    gm.tet.px = 3;
    gm.tet.py = 0;
    if (tet_hit(3, 0, tet_mask()))
        gm.tet.over = 1;
    gm.rev++;
    gm.dirty = 1;
}

static void tet_reset(void)
{
    gm.rng = GM_SEED;
    memset(&gm.tet, 0, sizeof gm.tet);
    gm.tet.next0 = (uint8_t)(gm_rand() % 7u);
    gm.tet.next1 = (uint8_t)(gm_rand() % 7u);
    gm.next_drop = 0;
    tet_spawn();
}

static uint32_t tet_level(void) { return 1u + gm.tet.lines / 10u; }

static uint32_t tet_drop_ms(void)
{
    int32_t ms = (int32_t)TET_START_MS - (int32_t)(tet_level() - 1u) * 34;
    return (uint32_t)(ms < (int32_t)TET_MIN_MS ? TET_MIN_MS : ms);
}

static void tet_lock(void)
{
    uint16_t m = tet_mask();
    uint32_t dx, dy, row, col, full, k;
    for (dy = 0; dy < 4u; dy++)
        for (dx = 0; dx < 4u; dx++) {
            int32_t bx = gm.tet.px + (int32_t)dx, by = gm.tet.py + (int32_t)dy;
            if ((m & (1u << (dy * 4u + dx))) && by >= 0)
                gm.tet.board[by * TET_COLS + bx] = (uint8_t)(gm.tet.piece + 1u);
        }
    for (row = TET_ROWS; row-- > 0u; ) {
        full = 1;
        for (col = 0; col < TET_COLS; col++)
            if (!gm.tet.board[row * TET_COLS + col])
                full = 0;
        if (!full)
            continue;
        for (k = row; k-- > 0u; )                /* the rows above fall one line: back to front */
            memcpy(&gm.tet.board[(k + 1u) * TET_COLS], &gm.tet.board[k * TET_COLS], TET_COLS);
        memset(&gm.tet.board[0], 0, TET_COLS);
        gm.tet.lines++;
        gm.tet.score = (uint16_t)(gm.tet.score + 100u);
        /* P21: re-check THIS row. The rows above just fell into it, so two or three full rows at
         * once (a well-built I piece) clear in one lock instead of leaving a full row behind and
         * stacking the next piece on a board that can never clear it again. */
        row++;
    }
    gm.rev++;
    gm.dirty = 1;
    tet_spawn();
}

/* one op on the local simulation (the authority, or a stand-alone side).
 * Every op that changes the picture bumps gm.rev: the companion repaints on it and, while locked,
 * ships the state on it — a move that left rev alone was drawn nowhere until gravity ticked. */
static void tet_op(uint8_t op)
{
    uint16_t m;
    int moved = 0;
    if (gm.tet.over) {
        if (op == SK_GIN_FLAP || op == SK_GIN_DROP)
            tet_reset();
        return;
    }
    m = tet_mask();
    switch (op) {
    case SK_GIN_LEFT:
        if (!tet_hit(gm.tet.px - 1, gm.tet.py, m))
            gm.tet.px--, moved = 1;
        break;
    case SK_GIN_RIGHT:
        if (!tet_hit(gm.tet.px + 1, gm.tet.py, m))
            gm.tet.px++, moved = 1;
        break;
    case SK_GIN_ROT: {
        uint8_t rot = (uint8_t)((gm.tet.rot + 1u) & 3u);
        uint16_t nm = TET_SHAPE[gm.tet.piece % 7u];
        uint32_t i;
        for (i = 0; i < rot; i++)
            nm = tet_rot(nm);
        if (!tet_hit(gm.tet.px, gm.tet.py, nm))
            gm.tet.rot = rot, moved = 1;
        break;
    }
    case SK_GIN_DROP:                              /* soft drop: one row, or lock */
    case SK_GIN_GRAV:
        if (!tet_hit(gm.tet.px, gm.tet.py + 1, m))
            gm.tet.py++, moved = 1;
        else
            tet_lock();                            /* bumps rev through tet_spawn */
        break;
    default:
        return;
    }
    if (moved)
        gm.rev++;
    gm.dirty = 1;
}

/* ------------------------------------------------------------- open/close --- */
static uint8_t gm_back_menu;   /* what the page returns to when the game closes */

/* P21: the MIDI path posts note-ons to sk_gi_note / sk_gi_seq (defined in params.c, declared in
 * core.h); the frame loop turns them into game input. */
static uint32_t gm_midi_done;

static void game_open(uint8_t kind)
{
    if (gm.kind == kind && kind != SK_GAME_NONE) {
        gm.dirty = 1;
        return;
    }
    if (gm.kind == SK_GAME_NONE)
        gm_back_menu = ui.menu;                    /* (0 = HOME, 1 = the MENU list) */
    gm.kind = kind;
    ui.game = kind;
    ui.menu = 0;
    ui.home = 0;
    ui.confirm = 0;
    ui.force = 1;
    gm.dirty = 1;
    gm.pad_flash = 0;
    gm.accum = 0;
    gm.last_ms = fm1_ms;
    /* rev stays monotonic across sessions: it is also the id of a tetris snapshot, and the mirror
     * drops an id it has already committed. Resetting it to 0 made a new game reuse the ids of the
     * old one, so the mirror threw the first board away and only caught up on the next tick. */
    if (kind == SK_GAME_FLAPPY)
        fl_reset();
    else if (kind == SK_GAME_TETRIS)
        tet_reset();
    gm.rev++;
}

static void sk_game_open(uint8_t kind)
{
    game_open(kind);
    if (gm.kind != SK_GAME_NONE) {
        uint8_t b[2];
        b[0] = SK_GPKT_OPEN;
        b[1] = gm.kind;
        sk_game_tx(b, 2);                          /* tell the authority, if there is one */
    }
}

/* the state change alone: applying a remote close never sends one back */
static void game_close_local(void)
{
    if (gm.kind == SK_GAME_NONE)
        return;
    gm.kind = SK_GAME_NONE;
    ui.game = SK_GAME_NONE;
    gm.mirror = 0;
    gm.dirty = 1;
    ui.menu = gm_back_menu;
    ui.home = gm_back_menu ? 0u : 1u;
    ui.force = 1;
}

static void sk_game_close(void)
{
    if (gm.kind == SK_GAME_NONE)
        return;
    {
        uint8_t b[1];
        b[0] = SK_GPKT_CLOSE;
        sk_game_tx(b, 1);                          /* the other side closes too (a no-op when alone) */
    }
    game_close_local();
}

/* --------------------------------------------------------------- inputs --- */
static void sk_game_input(uint8_t op)
{
    if (gm.kind == SK_GAME_NONE)
        return;
    if (gm.mirror) {                               /* the authority applies it and sends the state */
        uint8_t b[2];
        b[0] = SK_GPKT_INPUT;
        b[1] = op;
        sk_game_tx(b, 2);
        return;
    }
    if (gm.kind == SK_GAME_FLAPPY) {
        if (op == SK_GIN_FLAP || op == SK_GIN_DROP)
            fl_flap();
    } else {
        tet_op(op);
    }
}

static void sk_game_pad(uint32_t pad)
{
    if (gm.kind == SK_GAME_TETRIS) {
        switch (pad & 3u) {
        case 0: sk_game_input(SK_GIN_LEFT); break;
        case 1: sk_game_input(SK_GIN_RIGHT); break;
        case 2: sk_game_input(SK_GIN_ROT); break;
        default: sk_game_input(SK_GIN_DROP); break;
        }
        return;
    }
    sk_game_input(SK_GIN_FLAP);
}

/* P21: a MIDI note is input, the same way a panel key is (ui_input.c maps its 27 keys). FLAPPY:
 * any note-on flaps. TETRIS: the note's octave picks the op, low to high = left, rotate, right,
 * drop, so a MIDI keyboard plays it without a mapping table. */
static void sk_game_note(uint8_t note)
{
    if (gm.kind == SK_GAME_NONE)
        return;
    if (gm.kind == SK_GAME_FLAPPY) {
        sk_game_input(SK_GIN_FLAP);
        return;
    }
    sk_game_input(note < 55u ? SK_GIN_LEFT
                  : note < 67u ? SK_GIN_ROT
                  : note < 79u ? SK_GIN_RIGHT
                               : SK_GIN_DROP);
}

/* ------------------------------------------------------------- stepping --- */
static void sk_game_frame(void)
{
    uint32_t now = fm1_ms, dt = now - gm.last_ms;
    gm.last_ms = now;
    if (gm.pad_flash)
        gm.pad_flash--;
    if (gm.kind == SK_GAME_NONE)
        return;
    /* P21: incoming MIDI (USB or TRS) is input too, consumed here on the frame loop: the MIDI
     * path only bumps the counter. A mirrored game forwards the op like any other input. */
    {
        uint32_t seq = sk_gi_seq;
        if (seq != gm_midi_done) {
            gm_midi_done = seq;
            sk_game_note(sk_gi_note);
        }
    }
    if (gm.mirror)
        return;
    if (gm.kind == SK_GAME_FLAPPY) {
        if (dt > 250u)
            dt = 250u;                             /* a stall never teleports the bird */
        gm.accum += dt;
        while (gm.accum >= FL_TICK_MS) {
            gm.accum -= FL_TICK_MS;
            fl_step();
            if (!gm.fl.alive)
                break;
        }
    } else if (gm.kind == SK_GAME_TETRIS && !gm.tet.over) {
        if (!gm.next_drop || (int32_t)(now - gm.next_drop) >= 0) {
            gm.next_drop = now + tet_drop_ms();
            tet_op(SK_GIN_GRAV);
        }
    }
}

/* ============================================================== the link ==
 * Packets are 7-bit safe (every byte <= 0x7F), so they ride SKL_EVENT's payload as-is.
 *   0 op | … fields …                                        */
static void pk7(uint8_t *d, uint32_t off, uint32_t v) { d[off] = (uint8_t)(v & 0x7Fu); }
static uint32_t upk7(const uint8_t *d, uint32_t off) { return d[off]; }

static uint32_t pack_flappy(uint8_t *d)
{
    flappy_t *f = &gm.fl;
    uint32_t i, n = 0;
    d[n++] = SK_GPKT_STATE_F;
    d[n++] = (uint8_t)((f->alive ? 1u : 0u) | (f->started ? 2u : 0u));
    d[n++] = (uint8_t)(f->score & 0x7Fu);
    d[n++] = (uint8_t)((f->y >> 14) & 0x7Fu);
    d[n++] = (uint8_t)((f->y >> 7) & 0x7Fu);
    d[n++] = (uint8_t)(f->y & 0x7Fu);
    d[n++] = (uint8_t)(((f->vy + 2048) >> 7) & 0x7Fu);
    d[n++] = (uint8_t)((f->vy + 2048) & 0x7Fu);
    d[n++] = (uint8_t)(((f->spawn_x + 512) >> 7) & 0x7Fu);
    d[n++] = (uint8_t)((f->spawn_x + 512) & 0x7Fu);
    for (i = 0; i < 3u; i++) {
        d[n++] = (uint8_t)(((f->gx[i] + 512) >> 7) & 0x7Fu);
        d[n++] = (uint8_t)((f->gx[i] + 512) & 0x7Fu);
        d[n++] = (uint8_t)(f->gy[i] & 0x7Fu);
        d[n++] = (uint8_t)(f->gh[i] & 0x7Fu);
        d[n++] = (uint8_t)(f->scored[i] ? 1u : 0u);
    }
    return n;
}

static void unpack_flappy(const uint8_t *d, uint32_t n)
{
    flappy_t *f = &gm.fl;
    uint32_t i, p = 1;
    if (n < 25u)
        return;
    f->alive = (uint8_t)(d[p] & 1u);
    f->started = (uint8_t)((d[p] >> 1) & 1u);
    p++;
    f->score = (uint16_t)d[p++];
    f->y = (int32_t)((uint32_t)d[p] << 14 | (uint32_t)d[p + 1u] << 7 | (uint32_t)d[p + 2u]);
    p += 3;
    f->vy = (int32_t)((uint32_t)d[p] << 7 | (uint32_t)d[p + 1u]) - 2048;
    p += 2;
    f->spawn_x = (int16_t)((int32_t)((uint32_t)d[p] << 7 | (uint32_t)d[p + 1u]) - 512);
    p += 2;
    for (i = 0; i < 3u; i++) {
        f->gx[i] = (int16_t)((int32_t)((uint32_t)d[p] << 7 | (uint32_t)d[p + 1u]) - 512);
        p += 2;
        f->gy[i] = (int16_t)d[p++];
        f->gh[i] = (uint8_t)d[p++];
        f->scored[i] = (uint8_t)(d[p++] & 1u);
    }
    gm.rev++;                                  /* the view's counter: a mirror repaints on it too */
    gm.dirty = 1;
}

/* tetris: two frames, rows 0..8 and 9..17, each with the same header and snapshot id.
 * The id is gm.rev — the counter tet_op/tet_spawn bump — so both halves of one snapshot share it
 * and no two snapshots ever do (the old id was tetris_t.rev, bumped nowhere: always 0, so the
 * receiver could not tell a snapshot from its predecessor and committed a torn board). */
static uint32_t pack_tetris(uint8_t *d, uint8_t part)
{
    tetris_t *t = &gm.tet;
    uint32_t i, row, n = 0, id = gm.rev & 0x1FFFFFu;
    d[n++] = part ? SK_GPKT_STATE_T1 : SK_GPKT_STATE_T0;
    d[n++] = (uint8_t)((id >> 14) & 0x7Fu);
    d[n++] = (uint8_t)((id >> 7) & 0x7Fu);
    d[n++] = (uint8_t)(id & 0x7Fu);
    d[n++] = t->piece;
    d[n++] = t->rot;
    d[n++] = (uint8_t)(t->px + 1);     /* signed box: -1..7 -> 0..8, still 7-bit */
    d[n++] = (uint8_t)(t->py + 1);
    d[n++] = t->next0;
    d[n++] = t->next1;
    d[n++] = (uint8_t)((t->over ? 1u : 0u) | ((t->lines & 0x3Fu) << 1));
    d[n++] = (uint8_t)(t->score & 0x7Fu);
    d[n++] = (uint8_t)((t->score >> 7) & 0x7Fu);
    row = part ? 9u : 0u;
    for (i = 0; i < 9u; i++, row++) {          /* 10 cells a row, two a byte: 5 bytes a row */
        uint32_t col;
        for (col = 0; col < TET_COLS; col += 2u) {
            uint8_t a = (uint8_t)(t->board[row * TET_COLS + col] & 7u);
            uint8_t b = (uint8_t)(t->board[row * TET_COLS + col + 1u] & 7u);
            d[n++] = (uint8_t)((a << 4) | b);
        }
    }
    return n;
}

static int apply_packet(const uint8_t *d, uint32_t n)
{
    if (!n)
        return 0;
    switch (d[0]) {
    case SK_GPKT_OPEN:
        if (n >= 2u && d[1] >= SK_GAME_FLAPPY && d[1] <= SK_GAME_TETRIS)
            game_open((uint8_t)d[1]);
        return 1;
    case SK_GPKT_STATE_F:
        if (gm.kind != SK_GAME_FLAPPY)
            game_open(SK_GAME_FLAPPY);
        unpack_flappy(d, n);
        return 1;
    case SK_GPKT_INPUT:
        if (n >= 2u)
            sk_game_input((uint8_t)d[1]);
        return 1;
    case SK_GPKT_CLOSE:
        game_close_local();                        /* no echo: the sender already closed */
        return 1;
    case SK_GPKT_STATE_T0:
    case SK_GPKT_STATE_T1:
        return 1;   /* handled by the link layer through sk_game_apply_state() */
    }
    return 0;
}

/* the tetris halves are committed together, so a lost half never shows a torn board.
 * T0 opens a snapshot (its header included), T1 closes it; the commit only happens when both of
 * the same id arrived, and an id commits once. */
static struct {
    uint32_t seq;                              /* the snapshot being assembled */
    uint32_t done;                             /* the last one committed */
    uint8_t have;                              /* done holds a snapshot */
    uint8_t head;                              /* its header (T0) arrived */
    uint8_t board[TET_CELLS];
    uint8_t piece, rot, next0, next1, over;
    int8_t px, py;                             /* signed: see tetris_t */
    uint16_t score, lines;
} tet_rx;

static int apply_tetris_half(const uint8_t *d, uint32_t n)
{
    uint32_t seq, row, i, p = 1;
    uint8_t part;
    if (n < 13u)
        return 0;
    part = (uint8_t)(d[0] == SK_GPKT_STATE_T1);
    seq = (uint32_t)d[p] << 14 | (uint32_t)d[p + 1u] << 7 | (uint32_t)d[p + 2u];
    p += 3;
    if (!part) {
        if (tet_rx.have && seq == tet_rx.done)  /* the same snapshot again: its T1 is in flight (or landed) */
            return 0;
        memset(tet_rx.board, 0, sizeof tet_rx.board);   /* a half that arrives truncated never leaves old rows */
        tet_rx.seq = seq;
        tet_rx.head = 1;
        tet_rx.piece = d[p++];
        tet_rx.rot = d[p++];
        tet_rx.px = (int8_t)((int32_t) d[p++] - 1);
        tet_rx.py = (int8_t)((int32_t) d[p++] - 1);
        tet_rx.next0 = d[p++];
        tet_rx.next1 = d[p++];
        tet_rx.over = (uint8_t)(d[p] & 1u);
        tet_rx.lines = (uint16_t)((d[p++] >> 1) & 0x3Fu);
        tet_rx.score = (uint16_t)(d[p] | (uint32_t)d[p + 1u] << 7);
        p += 2;
    } else {
        if (!tet_rx.head || seq != tet_rx.seq)
            return 0;                          /* no T0 for this id: a lost half shows nothing, not a tear */
    }
    row = part ? 9u : 0u;
    for (i = 0; i < 9u && p + 4u < n && row < TET_ROWS; i++, row++) {
        uint32_t col;
        for (col = 0; col < TET_COLS; col += 2u) {
            uint8_t a = (uint8_t)((d[p] >> 4) & 0x07u), b = (uint8_t)(d[p] & 0x07u);
            tet_rx.board[row * TET_COLS + col] = a;
            tet_rx.board[row * TET_COLS + col + 1u] = b;
            p++;
        }
    }
    if (!part)
        return 0;                              /* rows 0..8 are in: wait for the other half */
    if (gm.kind != SK_GAME_TETRIS)
        game_open(SK_GAME_TETRIS);
    memcpy(gm.tet.board, tet_rx.board, sizeof gm.tet.board);
    gm.tet.piece = tet_rx.piece % 7u;
    gm.tet.rot = (uint8_t)(tet_rx.rot & 3u);
    gm.tet.px = tet_rx.px;
    gm.tet.py = tet_rx.py;
    gm.tet.next0 = tet_rx.next0 % 7u;
    gm.tet.next1 = tet_rx.next1 % 7u;
    gm.tet.over = tet_rx.over;
    gm.tet.score = tet_rx.score;
    gm.tet.lines = tet_rx.lines;
    tet_rx.done = seq;                         /* this snapshot is whole */
    tet_rx.have = 1;
    tet_rx.head = 0;
    gm.rev++;                                  /* the view's counter: a mirror repaints on it too */
    gm.dirty = 1;
    return 1;
}

/* the link layer hands a whole packet here */
static int sk_game_apply_packet(const uint8_t *d, uint32_t n)
{
    if (n && (d[0] == SK_GPKT_STATE_T0 || d[0] == SK_GPKT_STATE_T1))
        return apply_tetris_half(d, n);
    return apply_packet(d, n);
}

/* ============================================================== drawing ==
 * The panel is 240 x 240 and the canvas is at most 240 x 124, so every frame is two
 * passes drawn in screen coordinates (cv_oy) and blitted one under the other. */
static const uint16_t TET_C[7] = {
    RGB(0, 210, 220), RGB(235, 220, 20), RGB(170, 70, 220), RGB(60, 100, 235),
    RGB(235, 150, 40), RGB(60, 205, 80), RGB(230, 60, 60)
};

static void fl_draw(void)
{
    flappy_t *f = &gm.fl;
    uint32_t i;
    char txt[16];
    /* pipes: a body and a lip at the gap */
    for (i = 0; i < 3u; i++) {
        int32_t x = f->gx[i];
        int32_t top = (int32_t)f->gy[i] - f->gh[i] / 2;
        int32_t bot = (int32_t)f->gy[i] + f->gh[i] / 2;
        if (x > 240 || x + FL_GATE_W < 0)
            continue;
        cv_rect(x, 0, FL_GATE_W, top - 8, T_THEME);
        cv_rect(x - 3, top - 8, FL_GATE_W + 6, 8, T_SEL);
        cv_rect(x, bot + 8, FL_GATE_W, 240 - (bot + 8), T_THEME);
        cv_rect(x - 3, bot, FL_GATE_W + 6, 8, T_SEL);
    }
    /* the ground and the bird */
    cv_rect(0, FL_GROUND, 240, 240 - FL_GROUND, T_SURF);
    cv_rect(0, FL_GROUND, 240, 1, T_LINE);
    {
        int32_t bx = FL_BIRD_X - FL_BIRD_HW, by = (f->y >> 8) - FL_BIRD_HH;
        cv_rrect(bx, by, FL_BIRD_HW * 2, FL_BIRD_HH * 2, 5, f->alive ? T_ACCENT : T_DIM, T_BG);
        cv_rect(bx + 9, by + 3, 3, 3, T_INK);          /* the eye */
        cv_rect(bx + 12, by + 7, 4, 2, T_SEL);         /* the beak */
    }
    /* the score, then the state line */
    fmt_int(txt, (int32_t)f->score);
    cv_text_in(0, 6, 240, &AF_M, txt, f->alive ? T_TEXT : T_MID, T_BG);
    if (!f->alive) {
        cv_rrect(48, 84, 144, 46, 8, T_SURF, T_BG);
        cv_frame(48, 84, 144, 46, T_ACCENT);
        cv_text_in(48, 92, 144, &AF_M, "GAME OVER", T_ACCENT, T_SURF);
        cv_text_in(48, 112, 144, &AF_S, "TAP TO RETRY", T_TEXT, T_SURF);
    }
    cv_text(6, 222, &AF_S, "ANY PAD / KEY: FLAP", T_DIM);
    cv_text_r(234, 222, &AF_S, "OCT- BACK", T_DIM, T_BG);
}

static void tet_draw_cell(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t v)
{
    if (!v)
        cv_rect(x, y, w, h, T_BG);
    else {
        cv_rect(x, y, w, h, TET_C[(v - 1u) % 7u]);
        cv_rect(x, y, w, 1, T_RAISE);
    }
}

static void tet_draw(void)
{
    tetris_t *t = &gm.tet;
    const int32_t cw = 10, ch = 12, bx = 24, by = 12;
    uint32_t x, y, i, row2;
    char txt[20];
    /* the well */
    cv_rect(bx - 2, by - 2, TET_COLS * cw + 4, TET_ROWS * ch + 4, T_LINE);
    for (y = 0; y < TET_ROWS; y++)
        for (x = 0; x < TET_COLS; x++)
            tet_draw_cell(bx + (int32_t)x * cw, by + (int32_t)y * ch, cw - 1, ch - 1,
                          t->board[y * TET_COLS + x]);
    /* the falling piece */
    {
        uint16_t m = tet_mask();
        for (y = 0; y < 4u; y++)
            for (x = 0; x < 4u; x++) {
                int32_t py2, px2;
                if (!(m & (1u << (y * 4u + x))))
                    continue;
                px2 = (int32_t)(t->px + x);
                py2 = (int32_t)(t->py + y);
                if (py2 < 0 || px2 < 0 || px2 >= (int32_t)TET_COLS)
                    continue;
                tet_draw_cell(bx + px2 * cw, by + py2 * ch, cw - 1, ch - 1, (uint8_t)(t->piece + 1u));
            }
    }
    /* the side panel: title, lines, score, the two next pieces */
    cv_text(140, 12, &AF_M, "TETRIS", T_THEME);
    cv_text(140, 34, &AF_S, "LINES", T_MID);
    fmt_int(txt, (int32_t)t->lines);
    cv_text(204, 34, &AF_S, txt, T_TEXT);
    cv_text(140, 50, &AF_S, "SCORE", T_MID);
    fmt_int(txt, (int32_t)t->score);
    cv_text(204, 50, &AF_S, txt, T_TEXT);
    cv_text(140, 74, &AF_S, "NEXT", T_MID);
    for (i = 0; i < 2u; i++) {
        uint8_t pc = i ? t->next1 : t->next0;
        uint16_t m = TET_SHAPE[pc % 7u];
        const int32_t ox = 146, oy = 88 + (int32_t)i * 40;
        cv_rect(ox - 2, oy - 2, 20, 20, T_SURF);
        for (row2 = 0; row2 < 4u; row2++)
            for (x = 0; x < 4u; x++)
                if (m & (1u << (row2 * 4u + x)))
                    cv_rect(ox + (int32_t)x * 4, oy + (int32_t)row2 * 4, 4, 4, TET_C[pc % 7u]);
    }
    if (t->over) {
        cv_rrect(32, 96, 128, 46, 8, T_SURF, T_BG);
        cv_frame(32, 96, 128, 46, T_ACCENT);
        cv_text_in(32, 104, 128, &AF_M, "GAME OVER", T_ACCENT, T_SURF);
        cv_text_in(32, 124, 128, &AF_S, "TAP TO RETRY", T_TEXT, T_SURF);
    }
    cv_text(6, 222, &AF_S, "PADS / KEYS: MOVE", T_DIM);
    cv_text_r(234, 222, &AF_S, "OCT- BACK", T_DIM, T_BG);
}

static void game_draw_pass(uint32_t top, uint32_t h)
{
    cv_begin(240, h, T_BG);
    cv_oy = -(int32_t)top;
    if (gm.kind == SK_GAME_FLAPPY)
        fl_draw();
    else if (gm.kind == SK_GAME_TETRIS)
        tet_draw();
    cv_oy = 0;
    cv_blit(0, top);
}

static void sk_game_draw(void)
{
    if (gm.kind == SK_GAME_NONE)
        return;
    if (gm.kind == SK_GAME_TETRIS && !gm.dirty)
        return;
    game_draw_pass(0, 124);
    game_draw_pass(124, 116);
    gm.dirty = 0;
}

/* the 16 drumpad LEDs: TETRIS shows the next piece (the one after, faintly, on every pad left);
 * FLAPPY lights the whole bank on a flap */
void sk_game_pads(void)
{
#ifdef SKM_ON_DEVICE
    uint32_t i;
    if (gm.kind == SK_GAME_FLAPPY) {
        if (gm.pad_flash)
            for (i = 0; i < 16u; i++)
                smk_rgb_set(i, 0x00, 0x50, 0xB0);
        else
            for (i = 0; i < 16u; i++)
                smk_rgb_set(i, 0, 0, 0);
    } else if (gm.kind == SK_GAME_TETRIS) {
        uint16_t m = TET_SHAPE[gm.tet.next0 % 7u];
        const uint8_t *c0 = TET_RGB[gm.tet.next0 % 7u];
        const uint8_t *c1 = TET_RGB[gm.tet.next1 % 7u];
        for (i = 0; i < 16u; i++) {
            uint32_t x = i % 4u, y = i / 4u;
            if (m & (1u << (y * 4u + x)))
                smk_rgb_set(i, c0[0], c0[1], c0[2]);
            else
                smk_rgb_set(i, c1[0] / 8u, c1[1] / 8u, c1[2] / 8u);
        }
    }
#endif
}

/* --------------------------------------------- the companion's view --- */
#if defined(SKM_SMK_PANEL)
void sk_game_fill_view(sk_game_view_t *v)
{
    if (!v)
        return;
    memset(v, 0, sizeof *v);
    v->kind = gm.kind;
    v->rev = gm.rev;
    if (gm.kind == SK_GAME_FLAPPY) {
        flappy_t *f = &gm.fl;
        v->alive = f->alive;
        v->started = f->started;
        v->score = (int16_t)f->score;
        v->bird_y = (int16_t)(f->y >> 8);
        v->bird_vy = (int16_t)f->vy;
        for (uint32_t i = 0; i < 3u; i++) {
            v->gx[i] = f->gx[i];
            v->gy[i] = f->gy[i];
            v->gh[i] = f->gh[i];
        }
    } else if (gm.kind == SK_GAME_TETRIS) {
        tetris_t *t = &gm.tet;
        memcpy(v->board, t->board, sizeof v->board);
        v->piece = t->piece;
        v->rot = t->rot;
        v->px = t->px;
        v->py = t->py;
        v->next0 = t->next0;
        v->next1 = t->next1;
        v->over = t->over;
        v->lines = (int16_t)t->lines;
        v->score = (int16_t)t->score;
        v->alive = (uint8_t)(t->over ? 0u : 1u);
    }
}
#endif
