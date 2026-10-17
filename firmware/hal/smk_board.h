/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * SMK-37 Elite panel, measured on the probe and checked against the stock tables.
 * Cell ids are what the working SPI2 scan reports: high nibble column, low nibble row.
 * The stock firmware's column number is one higher; these ids match the hardware. */
#pragma once
#include <stdint.h>

#define SMK_NKEYS 37u
#define SMK_NPADS 16u
#define SMK_NENC  8u
#define SMK_NCOL  16u

/* Lowest key is C1 (MIDI 24). Highest is C4 (MIDI 60). */
#define SMK_KEY_NOTE0 24u
/* Pads play part 4, the drum kit. Channel 10 is outside the CH1-4 route. */
#define SMK_PAD_CH    3u
#define SMK_PAD_NOTE0 36u
#define SMK_KEY_CH    0u

/* Probe cells. First contact, second contact. */
static const uint8_t SMK_KEYS[SMK_NKEYS][2] = {
    {0x00, 0x42}, {0x10, 0x62}, {0x20, 0x82}, {0x30, 0xA2}, {0x40, 0xC2}, {0x50, 0xE2}, {0x60, 0xD2},
    {0x70, 0xB2}, {0x80, 0x92}, {0x90, 0x72}, {0xB0, 0x52}, {0xC0, 0x32}, {0xD0, 0x12}, {0xE0, 0x02},
    {0xA0, 0x22}, {0x01, 0x23}, {0x11, 0x33}, {0x21, 0x43}, {0x31, 0x53}, {0x41, 0x63}, {0x51, 0x73},
    {0x61, 0x83}, {0x71, 0x93}, {0x81, 0xA3}, {0x91, 0xB3}, {0xA1, 0xC3}, {0xB1, 0xD3}, {0xC1, 0x13},
    {0xD1, 0x03}, {0xE1, 0x04}, {0x54, 0x14}, {0x64, 0x24}, {0x74, 0x34}, {0x84, 0x44}, {0x94, 0xC4},
    {0xA4, 0xE4}, {0xB4, 0xD4},
};

/* Encoder phases, knobs 1..8. */
static const uint8_t SMK_ENC[SMK_NENC][2] = {
    {0x87, 0x97}, {0x27, 0x37}, {0x07, 0x17}, {0xA7, 0xB7},
    {0x47, 0x57}, {0x67, 0x77}, {0xC7, 0xD7}, {0xE7, 0xF7},
};

/* Printed buttons, probe cells. */
enum {
    SMK_GLOBE = 0x05, SMK_SCALE = 0x15, SMK_SEQ = 0x25, SMK_CHORD = 0x35, SMK_PARA = 0x45,
    SMK_NOTE_REPEAT = 0x55, SMK_ARP = 0x65, SMK_FX = 0x75, SMK_PATCH = 0x85,
    SMK_SEQ_PLAY = 0x06, SMK_KNOB_BANK = 0x16, SMK_OCT_UP = 0x26, SMK_SEQ_REC = 0x36,
    SMK_RIGHT = 0x46, SMK_OCT_DOWN = 0x56, SMK_FADER_BANK = 0x66, SMK_BT = 0x76, SMK_LEFT = 0x86
};

/* Mux address (bits 3..5) for positions 1..8. Same wiring on every analog chip.
 * Digit on the probe, left to right, is that address. Position 1 lands on digit 4,
 * then 1, 2, 3, 5, 7, 8, 6. */
static const uint8_t SMK_MUX_OF[8] = {3, 0, 1, 2, 4, 6, 7, 5};

enum {
    SMK_ADC_PAD_A = 1,   /* PA8, front pads, positions 1..8 */
    SMK_ADC_FADER = 2,   /* PA10: faders 1..4, pitch, mod, sustain. Digit 8 unused */
    SMK_ADC_PAD_B = 8    /* PC9, back pads, same position order */
};

/* PA10 roles, as mux addresses. The probe's numbers were 8-bit (raw >> 2);
 * these are the 10-bit readings. */
enum {
    SMK_FADER1 = 3, SMK_FADER2 = 0, SMK_FADER3 = 1, SMK_FADER4 = 2,
    SMK_PITCH = 4,
    SMK_MOD = 5,
    SMK_SUSTAIN = 6
};
enum {
    SMK_PITCH_LO = 0x60u << 2, SMK_PITCH_MID = 0x7Du << 2, SMK_PITCH_HI = 0x9Cu << 2,
    SMK_MOD_ZERO = 0x9Au << 2, /* wheel down */
    SMK_MOD_FULL = 0x60u << 2  /* wheel up */
};
