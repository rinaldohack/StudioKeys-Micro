/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * SMK-37 flash store. BTIF is 0xC0000 and USRTRIM is 0xC1000; both stay
 * through 0xC1FFF. A save must not land there. */
#pragma once
#include <stdint.h>

/* The flash chip the store accepts. The SMK-37's part is not the FM-1's: the Elite reads 0xEB6014
 * where the FM-1 reads 0x856014, and the driver uses only generic SPI NOR commands (9F, 05/35, 06,
 * 20, 02, 0B), so accepting the id is all a different part needs. Gating on 0x856014 alone left the
 * SMK read-only: every save was silently dropped and every boot started from the defaults. */
#define FL_JEDEC_SMK 0xEB6014u
#define FL_JEDEC_FM1 0x856014u
static inline int fl_jedec_ok(uint32_t id) { return id == FL_JEDEC_SMK || id == FL_JEDEC_FM1; }

#define FL_DATA_LO      0x000C2000u
#define FL_DATA_HI      0x00100000u
#define FL_GLOB_LO      0x000C2000u
#define FL_GLOB_HI      0x000C4000u
#define FL_OTA_LO       0x000E0000u
#define FL_OTA_HI       0x000E5000u
#define FL_SET_BASE     0x000C2000u
#define FL_PROJ_BASE    0x000C4000u
#define FL_PRE_BASE     0x000CC000u
#define FL_FM6_A        0x000D0000u
#define FL_FM6_B        0x000D1000u
#define FL_MIX_BASE     0x000D2000u   /* one sector: four mixer parts (mixer_keep.c). Gap before OTA. */
#define SMP_USER_BASE   0x000E5000u
#define SMP_USER_SIZE   0x00009000u
#define FL_PRE_HI       0x000EE000u   /* user preset banks 2..6 (five A/B pairs, after the samples) */
#define FL_UPF_BASE     0x000F8000u   /* user preset FM6 patches, banks 1..3 (three A/B pairs) */
#define FL_WCAL_BASE    0x000FE000u   /* the wheels' calibration, A/B: the store's last two sectors (wheel_cal.h) */

#define FL_HITS_BT(off, n) ((uint32_t)(off) < 0xC2000u && (uint32_t)(n) != 0u && \
                            (uint32_t)(off) + (uint32_t)(n) > 0xC0000u && \
                            (uint32_t)(off) + (uint32_t)(n) >= (uint32_t)(off))
