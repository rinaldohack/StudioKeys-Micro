/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* The wheels' calibration in its own flash object (wheel_cal.h has the record and what it is for).
 * Included after storage.c: one A/B pair at FL_WCAL_BASE, written like every other object (erase, payload,
 * the commit record last), so a power cut mid-write leaves the previous calibration in charge. A write is
 * only made when the wizard finishes, and only when the transport is stopped (a flash erase silences the
 * audio); otherwise it is queued and wcal_poll writes it at the first stop. */
#include "wheel_cal.h"                     /* (the wizard's TU includes it earlier: this is the record itself) */

static uint8_t wcal_dirty;                 /* 1: the record waits for its flash write */
static uint32_t wcal_try_ms;               /* 0x80000000: no time set */

static int wcal_store(void)                /* 0 written, 2 no flash / failed */
{
#if SKM_FLASH
    if (!flash_ok)
        return 2;
    return st_save(OBJ_WHEELCAL, &wcal, sizeof wcal) ? 2 : 0;
#else
    return 2;
#endif
}

static void wcal_boot(void)
{
#if SKM_FLASH
    wcal_t r;
    if (flash_ok && st_load(OBJ_WHEELCAL, &r, sizeof r) == (int)sizeof r && r.magic == WCAL_MAGIC) {
        wcal = r;                                       /* (a half-record still has its own wheels' checks) */
        wcal_apply();
        return;
    }
#endif
    memset(&wcal, 0, sizeof wcal);                      /* no record: the factory numbers */
    wcal_apply();
}

/* the wizard's last step: written now, or queued until the transport stops */
static int wcal_save(void)
{
    int rc;
    if (transport_busy()) {
        wcal_dirty = 1;
        wcal_try_ms = fm1_ms;
        return 1;
    }
    wcal_dirty = 0;
    rc = wcal_store();
    return rc;
}

static void wcal_poll(void)
{
    if (!wcal_dirty || transport_busy() || (int32_t)(fm1_ms - wcal_try_ms) < 0)
        return;
    if (wcal_store()) {
        wcal_try_ms = fm1_ms + 1000u;                   /* (a failing part is retried, a second apart) */
        return;
    }
    wcal_dirty = 0;
}
