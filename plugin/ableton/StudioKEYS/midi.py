# SPDX-License-Identifier: GPL-3.0-only
"""StudioKEYS Live MIDI constants — keep in sync with firmware sk_live.c / PROTOCOL.md."""

SYSEX_START = 0xF0
SYSEX_END = 0xF7
SYSEX_HEADER = (0x7D, 0x53, 0x4B, 0x4D)  # educational + 'SKM'

PAD0 = 36
STOP_ALL = 44
BANK_M = 46
BANK_P = 47
MUTE0 = 48
SOLO0 = 52
ARM0 = 56
SEL0 = 60
FACE = 70
PADFACE = 71
KEYS = 72
PLAY = 80
STOP = 81
REC = 82
METRO = 83
CAPTURE = 84
SCENE_M = 85
SCENE_P = 86

CC_KNOB0 = 21
CC_FADER0 = 41

# Mackie HUI notes (USB cable 2) — keep in sync with firmware sk_hui.c / SurfaceCard.h.
# Ableton script uses Live (cable 1); these exist so check_surface_map.py can gate HUI parity.
HUI_REC0 = 0x00
HUI_SOLO0 = 0x08
HUI_MUTE0 = 0x10
HUI_SEL0 = 0x18
HUI_BANK_L = 0x2E
HUI_BANK_R = 0x2F
HUI_PLAY = 0x4E
HUI_STOP = 0x4D
HUI_RECORD = 0x4F

NUM_TRACKS = 4
NUM_SCENES = 2
