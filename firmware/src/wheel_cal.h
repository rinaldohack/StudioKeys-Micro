/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* Wheel calibration: MENU > SYSTEM > CALIBRATE PITCH BEND / CALIBRATE MOD WHEEL.
 *
 * What the two wheels' pots report at their own ends, in place of the factory numbers (hal/smk_board.h
 * SMK_PITCH_* / SMK_MOD_*), so a pot that sits 20 counts off centre still bends from a true centre and reaches
 * the full bend at its own top and bottom instead of somewhere in the middle of the travel.
 *
 * It is a measurement of this unit, not a preference, so it gets its own flash object (OBJ_WHEELCAL,
 * FL_WCAL_BASE: storage.c): the settings record's import / export, a backup restore and a firmware update
 * never touch it. A record that fails the checks below (erased flash, a half-written one) means the factory
 * numbers, wheel by wheel: calibrating the mod wheel leaves a learned pitch calibration alone, and the other
 * way round.
 *
 * ui_input.c runs the wizard (the prompts, `>` to record a step), hal/fm1_input.h applies the numbers to the
 * scan, wheel_cal.c (after storage.c) loads and writes the flash object.
 *
 * The record is also read where there is no scan at all (the companion, the host tests): wcal_apply() only
 * reaches the scan when hal/fm1_input.h was included (SMK_WCAL_SCAN), so this header costs those builds the
 * struct and the checks, nothing else. */
#pragma once

/* the least travel a calibrated wheel can have, shared with the scan's own guard (hal/fm1_input.h) */
#ifndef SMK_WCAL_MIN
#define SMK_WCAL_MIN 16
#endif

#define WCAL_MAGIC 0x5743414Cu                 /* "WCAL" */
typedef struct {
    uint32_t magic;
    uint16_t pitch_c;                      /* released: the bend's centre */
    uint16_t pitch_up;                     /* the top of its travel (bend +8191) */
    uint16_t pitch_dn;                     /* .. and the bottom (-8192) */
    uint16_t mod_zero;                     /* wheel down: CC1 0 (no spring, so no centre) */
    uint16_t mod_full;                     /* .. and up: CC1 127 */
    uint16_t rsv;                          /* (0; the record stays 16 bytes) */
} wcal_t;
static wcal_t wcal;                        /* the applied record (0: nothing learned) */

static int wcal_pitch_ok(const wcal_t *r)
{
    return r->magic == WCAL_MAGIC && r->pitch_up <= 1023u && r->pitch_c < r->pitch_up &&
           r->pitch_dn < r->pitch_c && r->pitch_up - r->pitch_c >= SMK_WCAL_MIN &&
           r->pitch_c - r->pitch_dn >= SMK_WCAL_MIN;
}
static int wcal_mod_ok(const wcal_t *r)
{
    return r->magic == WCAL_MAGIC && r->mod_zero <= 1023u && r->mod_full < r->mod_zero &&
           r->mod_zero - r->mod_full >= SMK_WCAL_MIN;
}
/* the record -> the scan (a wheel with no usable record: its factory numbers). The companion and the host
 * tests have no scan: the record is still kept and checked, it just has no pot to steer there. */
static void wcal_apply(void)
{
#if defined(SMK_WCAL_SCAN)
    int p = wcal_pitch_ok(&wcal), m = wcal_mod_ok(&wcal);
    smk_pitch_cal(p ? wcal.pitch_c : 0, p ? wcal.pitch_up : 0, p ? wcal.pitch_dn : 0);
    smk_mod_cal(m ? wcal.mod_zero : 0, m ? wcal.mod_full : 0);
#endif
}
/* the wizard's last step: a learned wheel enters the record, the other keeps what it had */
static void wcal_set_pitch(int32_t c, int32_t up, int32_t dn)
{
    wcal.magic = WCAL_MAGIC;
    wcal.rsv = 0;
    wcal.pitch_c = (uint16_t)c;
    wcal.pitch_up = (uint16_t)up;
    wcal.pitch_dn = (uint16_t)dn;
    wcal_apply();
}
static void wcal_set_mod(int32_t zero, int32_t full)
{
    wcal.magic = WCAL_MAGIC;
    wcal.rsv = 0;
    wcal.mod_zero = (uint16_t)zero;
    wcal.mod_full = (uint16_t)full;
    wcal_apply();
}

/* wheel_cal.c (included after storage.c, before persist_boot) */
static void wcal_boot(void);               /* the flash copy -> the scan, else the factory numbers */
static int wcal_save(void);                /* 0 written, 1 queued (playing), 2 no flash / failed */
static void wcal_poll(void);               /* main loop: the queued write, once the transport stops */
