/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* measure every formatted parameter value's width in each face (host probe, not part of run_tests.sh) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
static struct { volatile uint32_t notes, buttons; } fm1_in;
#define memset skm_memset
#define memcpy skm_memcpy
#define memcmp skm_memcmp
#include "skm_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include "../firmware/src/gfx.c"
#include "../firmware/src/core.h"
#include "../firmware/src/engines.c"
#include "../firmware/src/params.c"

#define ROOM 24

int main(void)
{
    int32_t v;
    char val[16];
    const char *unit;
    int with_unit = 0, unitless = 0, uworst = 0, vworst = 0;
    char vworst_s[24] = "", uworst_s[24] = "";
    printf("%-7s %-12s %-4s %4s\n", "param", "value", "fmt", "XS");
    for (uint32_t id = 0; id < P_COUNT; id++) {
        const param_desc_t *d = &TP[id];
        if (!d->label || !d->label[0] || d->label[0] == '-') continue;
        for (v = d->min; v <= d->max; v++) {
            char full[24];
            param_format(d, v, val, &unit);
            snprintf(full, sizeof full, "%s%s", val, unit[0] ? unit : "");
            int32_t wx = text_w(&AF_XS, full);
            int32_t wu = text_w(&AF_XS, val);
            if (wx > ROOM) with_unit++;
            if (wu > ROOM) unitless++;
            if (wu > uworst && strchr("0123456789", val[0]) && d->fmt != F_ENUM) { uworst = wu; snprintf(uworst_s, sizeof uworst_s, "%s %s", d->label, val); }
            if (wx > vworst) { vworst = wx; snprintf(vworst_s, sizeof vworst_s, "%s %s", d->label, full); }
        }
    }
    printf("XS: values with unit > 24px: %d ; value+unit worst: %d (%s)\n", with_unit, vworst, vworst_s);
    printf("XS: unitless (name) > 24px: %d\n", unitless);
    printf("XS: numeric worst (no unit): %d (%s)\n", uworst, uworst_s);
    /* engine edit labels + values */
    int elab = 0, eval = 0;
    for (uint32_t e = 0; e < 1u; e++) {}
    (void)v;
    return 0;
}
