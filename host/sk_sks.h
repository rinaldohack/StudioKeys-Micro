/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * StudioKEYS surface SysEx on the link port (cable 3). See firmware sk_surf.c. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* F0 7D 53 4B 53 … F7  — "SKS" */
enum {
  SK_SKS_HDR0 = 0x7D,
  SK_SKS_HDR1 = 0x53, /* S */
  SK_SKS_HDR2 = 0x4B, /* K */
  SK_SKS_HDR3 = 0x53, /* S */
  SK_SKS_LIVE_NOTE = 0x01, /* note, vel → Live cable 1 */
  SK_SKS_LIVE_CC = 0x02,   /* cc, val  → Live cable 1 */
  SK_SKS_HUI_FADER = 0x11, /* strip, val → HUI cable 2 pitch-bend */
  SK_SKS_HUI_NOTE = 0x12   /* note, vel → HUI cable 2 note on/off */
};

#ifdef __cplusplus
}
#endif
