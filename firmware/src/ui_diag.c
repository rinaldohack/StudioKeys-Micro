/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* DIAG: the SYSTEM page's CPU column, OCT+ (act_cols: G_INFO). What the instrument thinks of its own clock and
 * what the last boot died doing - the numbers that used to need `fm1t memr` and a serial console, which the
 * hardware bring-up does not have.
 *
 * Read-only: nothing here sets anything (the pitch lock, the CPU meter and the breadcrumbs are audio.c's).
 * Redrawn when a value it shows changes, at most every 15 frames (~0.2 s): a page that redrew every frame
 * would spend a few per cent of the CPU the page is trying to measure.
 *
 *   header  0..24   the cog, DIAGNOSTICS, OCT- BACK
 *   panel  28..214  a row a label (left) and its value (right), a rule between them:
 *                   RATE the measured I2S rate, PITCH its ppm against 44,100, LOCK the accepted windows,
 *                   CPU / ISR MAX / LATE the render's health, BOOTS the boot count with the failed ones,
 *                   LAST where the previous boot's main loop was, RESET why it restarted
 *   footer  218     the trim the lock asked for (left), the firmware's version (right) */
#define DT_XL 10                                /* a label's left edge */
#define DT_XR 230                               /* a value's right edge */
#define DT_Y0 32                                /* the first row */
#define DT_RH 20                                /* a row's height */
#define DT_N 9
#define DT_BAND 124u                            /* the canvas's rows: two bands, as the menu */

#if !defined(SKM_DIAG_SRC)
/* a build without audio.c (tests/ui_test.c, tests/ui_render.c): the page still draws, from this snapshot - zeros
 * until a test fills it (ui_render.c: real numbers, to lint and shoot the page as it looks on the hardware) */
skm_diag_t skm_diag_test;
static void skm_diag_read(skm_diag_t *d) { *d = skm_diag_test; }
#endif

static const char *diag_reset(uint32_t p3)
{
    /* P3_RST_SRC, bit per cause (hal/fm1_sys.h): the first one set is why the last boot started */
    static const char *const R[7] = {"POWER", "VDDIO", "WDT", "VCM", "LONG", "1V2", "SOFT"};
    uint32_t k = 0;
    while (k < 7u && !(p3 & (1u << k)))
        k++;
    return k < 7u ? R[k] : "--";
}

/* "44117.6" from the milli-Hz, or WAIT until a window has landed */
static void diag_rate(char *b, uint32_t mhz)
{
    if (!mhz) {
        str_cpy(b, "WAIT", 12);
        return;
    }
    fmt_fix(b, (int32_t)(mhz / 100u), 1);                 /* deci-Hz: one decimal */
}

/* "+400" / "-13": the sign is explicit, matching the direction the trim moves the pitch */
static void diag_ppm(char *b, int32_t ppm)
{
    b[0] = ppm < 0 ? '-' : '+';
    fmt_int(b + 1, ppm < 0 ? -ppm : ppm);
}

static uint32_t diag_append(char *b, unsigned max, const char *s)
{
    unsigned n = str_len(b);
    str_cpy(b + n, s, max - n);
    return n + str_len(s);
}

static void diag_last(char *b, const skm_diag_t *d)       /* "S6 P12 H0": the boot-before's breadcrumbs */
{
    unsigned n;
    b[0] = 'S';
    fmt_int(b + 1, (int32_t)d->prev_stage);
    n = (unsigned)diag_append(b, 16, " P");
    fmt_int(b + n, (int32_t)d->prev_page);
    n = (unsigned)diag_append(b, 16, " H");
    fmt_int(b + n, (int32_t)d->prev_home);
}

static void diag_boots(char *b, const skm_diag_t *d)      /* "3 F0": boots, the failed ones (bootguard) */
{
    unsigned n;
    fmt_int(b, (int32_t)d->boots);
    n = (unsigned)diag_append(b, 16, " F");
    fmt_int(b + n, (int32_t)d->boot_failed);
}

/* the trim as a ppm (Q16 1.0 = 65536 = no correction) */
static int32_t diag_trim_ppm(uint32_t q16)
{
    return (int32_t)(((int64_t)q16 - 65536) * 1000000 / 65536);
}

/* the rows: labels fixed, values and colours from the snapshot; returns a signature of what is shown */
static uint32_t diag_collect(const skm_diag_t *d, char v[DT_N][16], uint16_t col[DT_N])
{
    uint32_t i, k, h = 2166136261u;
    for (i = 0; i < DT_N; i++) {
        v[i][0] = 0;
        col[i] = T_THEME;
    }
    diag_rate(v[0], d->rate_mhz);
    diag_ppm(v[1], d->ppm);
    if (d->wins) {
        v[2][0] = 'x';
        fmt_int(v[2] + 1, (int32_t)(d->wins > 9999u ? 9999u : d->wins));
    } else {
        str_cpy(v[2], "WAIT", 16);
    }
    fmt_int(v[3], (int32_t)(d->cpu_q8 * 100u / 256u));
    k = str_len(v[3]);
    v[3][k] = '%';
    v[3][k + 1] = 0;
    fmt_int(v[4], (int32_t)d->max_us);
    diag_append(v[4], 16, "US");
    fmt_int(v[5], (int32_t)d->late);
    diag_boots(v[6], d);
    diag_last(v[7], d);
    str_cpy(v[8], diag_reset(d->prev_rst), 16);

    /* what is wrong is shown in the accent colour, not left to be noticed */
    if (!d->rate_mhz)
        col[0] = T_ACCENT;                                /* no lock yet: the pitch is the tables', not the clock's */
    if (d->late)
        col[5] = T_ACCENT;
    if (d->boot_failed)
        col[6] = T_ACCENT;

    for (i = 0; i < DT_N; i++)
        for (k = 0; v[i][k]; k++)
            h = (h ^ (uint8_t)v[i][k]) * 16777619u;
    return h;
}

static void diag_head(void)
{
    int32_t w = kh_w(KC_OCTDN, "BACK"), x = 232 - w;
    cv_begin(240, H_HEAD, T_BG);
    GFX_HOOK_ALIGN(0, HEAD_MY + AF_M_CAP_Y, 0, HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V,
                   "diag header icon on its title's line");
    cv_icon_mid(8, H_HEAD / 2, 16, ICON_X_COG, T_THEME, T_BG);
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "diag header text on its middle");
    cv_text(30, HEAD_MY, &AF_M, "DIAGNOSTICS", T_TEXT);
    cv_key_hint(x, HALF_UP(H_HEAD - KC_H), KC_OCTDN, "BACK", 1, T_BG);
    cv_blit(0, Y_HEAD);
}

static void diag_body(const char v[DT_N][16], const uint16_t col[DT_N], const skm_diag_t *d)
{
    static const char *const L[DT_N] = {"RATE", "PITCH", "LOCK", "CPU", "ISR MAX", "LATE", "BOOTS", "LAST", "RESET"};
    uint32_t i;
    char b[24];
    cv_rrect(4, DT_Y0 - 4, 232, DT_N * DT_RH + 6, 8, T_SURF, T_BG);
    for (i = 0; i < DT_N; i++) {
        int32_t y = DT_Y0 + (int32_t)i * DT_RH;
        if (i)
            cv_rect(12, y - 1, 216, 1, ux.style ? T_RULE : T_LINE);
        GFX_HOOK_ALIGN(0, y, 0, y + DT_RH, AL_V, "diag row label centred up/down");
        cv_text_on(DT_XL, y + CAP_IN(S, DT_RH), &AF_S, L[i], T_MID, T_SURF);
        GFX_HOOK_ALIGN(0, y, 0, y + DT_RH, AL_V, "diag row value centred up/down");
        cv_text_r(DT_XR, y + CAP_IN(M, DT_RH), &AF_M, v[i], col[i], T_SURF);
    }
    /* the correction the lock applied (the tables are 44,100; the I2S clock is not), and the version */
    diag_ppm(b, diag_trim_ppm(d->trim_q16));
    diag_append(b, sizeof b, " TRIM");
    GFX_HOOK_ALIGN(0, 0, 0, 0, AL_PASS, "diag footer on one line");
    cv_text(DT_XL, DT_Y0 + DT_N * DT_RH + 6, &AF_S, b, T_DIM);
    cv_text_r(DT_XR, DT_Y0 + DT_N * DT_RH + 6, &AF_S, SKM_VERSION, T_DIM, T_BG);
}

static void draw_diag(void)
{
    static uint32_t shown_sig, shown_frame;
    skm_diag_t d;
    char v[DT_N][16];
    uint16_t col[DT_N];
    uint32_t sig, pass;
    skm_diag_read(&d);
    sig = diag_collect(&d, v, col);
    if (!ui.force) {
        if (sig == shown_sig)
            return;                                       /* nothing it shows changed */
        if ((uint32_t)(ui.frame - shown_frame) < 15u)
            return;                                       /* .. it did: at most every ~0.2 s */
    }
    shown_sig = sig;
    shown_frame = ui.frame;
    diag_head();
    for (pass = 0; pass < 2u; pass++) {
        int32_t top = pass ? (int32_t)DT_BAND : H_HEAD;
        cv_begin(240, pass ? 240u - DT_BAND : DT_BAND - H_HEAD, T_BG);
        cv_oy = -top;
        diag_body(v, col, &d);
        cv_oy = 0;
        cv_blit(0, (uint32_t)top);
    }
    lcd_sync();
}
