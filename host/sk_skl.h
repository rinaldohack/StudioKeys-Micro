/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * StudioKEYS link-port lock SysEx (cable 3). See host/README.md. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* F0 7D 53 4B 4C … F7  — "SKL" */
enum {
  SK_SKL_HDR0 = 0x7D,
  SK_SKL_HDR1 = 0x53, /* S */
  SK_SKL_HDR2 = 0x4B, /* K */
  SK_SKL_HDR3 = 0x4C, /* L */
  SK_SKL_HELLO = 0x01,
  SK_SKL_HELLO_ACK = 0x02,
  SK_SKL_CRC = 0x03,
  SK_SKL_CRC_ACK = 0x04,
  SK_SKL_CHOOSE = 0x05,
  SK_SKL_RESULT = 0x06,
  SK_SKL_EVENT = 0x07,
  SK_SKL_DROP = 0x08,
  SK_SKL_DISMISS = 0x09,
  /* Project snapshot (project_store_t, ~3584 B) — 7-bit packed chunks after CHOOSE when CRCs differ */
  SK_SKL_SNAP_BEGIN = 0x0A, /* dir(1) + size(5×7) + crc(5×7) */
  SK_SKL_SNAP_CHUNK = 0x0B, /* seq(2×7) + ≤48 payload nybbles (lo/hi nibble packing) */
  SK_SKL_SNAP_END = 0x0C    /* crc(5×7) — receiver applies + replies RESULT */
};

enum {
  SK_SKL_DIR_HW_TO_PLUGIN = 0,
  SK_SKL_DIR_PLUGIN_TO_HW = 1
};

enum {
  SK_SKL_OK = 0,
  SK_SKL_REFUSE_VERSION = 1,
  SK_SKL_REFUSE_CRC = 2,
  SK_SKL_REFUSE_STATE = 3
};

/* EVENT ids above P_COUNT: settings DMA (14-bit id in 2×7-bit). */
enum {
  SK_SKL_META_LANG = 0x3FFD,    /* 0 = EN, 1 = ID */
  SK_SKL_META_PALETTE = 0x3FFE, /* settings.palette index */
  SK_SKL_META_GAME = 0x3FFC,    /* P19/P20: FLAPPY / TETRIS packets (see firmware/src/game.c) */
  /* P6h, keyboard -> plug-in: the output clock it measured and the pitch lock it applies, so the
   * LINK window can show them (the unit has no serial console in normal use). */
  SK_SKL_META_RATE_PPM = 0x3FFB,  /* ppm against 44,100 */
  SK_SKL_META_RATE_TRIM = 0x3FFA  /* pitch lock Q16 - 65536 */
};

#ifdef __cplusplus
}
#endif
