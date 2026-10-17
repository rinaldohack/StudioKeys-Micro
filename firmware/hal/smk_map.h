/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * SMK-37 flash store. BTIF is 0xC0000 and USRTRIM is 0xC1000; both stay
 * through 0xC1FFF. A save must not land there. */
#pragma once

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

#define FL_HITS_BT(off, n) ((uint32_t)(off) < 0xC2000u && (uint32_t)(n) != 0u && \
                            (uint32_t)(off) + (uint32_t)(n) > 0xC0000u && \
                            (uint32_t)(off) + (uint32_t)(n) >= (uint32_t)(off))
