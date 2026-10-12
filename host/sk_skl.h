/* SPDX-License-Identifier: GPL-3.0-only
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
  SK_SKL_DISMISS = 0x09
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

#ifdef __cplusplus
}
#endif
