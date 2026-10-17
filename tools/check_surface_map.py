#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
"""Assert Ableton midi.py note/CC constants match SurfaceCard.h enums.

Parses plugin/ableton/StudioKEYS/midi.py and plugin/StudioKEYS/SurfaceCard.h
with regex. Compares the Live surface map; also compares HUI notes when the
same symbol exists in both files. Exit 1 on any mismatch.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MIDI_PY = ROOT / "plugin" / "ableton" / "StudioKEYS" / "midi.py"
SURFACE_H = ROOT / "plugin" / "StudioKEYS" / "SurfaceCard.h"
SK_HUI_C = ROOT / "firmware" / "src" / "sk_hui.c"

# midi.py NAME → SurfaceCard.h kName (Live)
PAIRS = (
    ("PAD0", "kPad0"),
    ("STOP_ALL", "kStopAll"),
    ("BANK_M", "kBankM"),
    ("BANK_P", "kBankP"),
    ("FACE", "kFace"),
    ("PADFACE", "kPadFace"),
    ("KEYS", "kKeys"),
    ("PLAY", "kPlay"),
    ("STOP", "kStop"),
    ("REC", "kRec"),
    ("METRO", "kMetro"),
    ("CAPTURE", "kCapture"),
    ("CC_KNOB0", "kCcKnob0"),
    ("CC_FADER0", "kCcFader0"),
)

# Expected Live anchors (PROTOCOL.md / plan phase 9).
ANCHORS = {
    "PAD0": 36,
    "STOP_ALL": 44,
    "FACE": 70,
    "PLAY": 80,
    "STOP": 81,
    "REC": 82,
    "CC_KNOB0": 21,
    "CC_FADER0": 41,
}

# Optional HUI pairs — only asserted when both sides define the name.
HUI_PAIRS = (
    ("HUI_REC0", "kHuiRec0"),
    ("HUI_SOLO0", "kHuiSolo0"),
    ("HUI_MUTE0", "kHuiMute0"),
    ("HUI_SEL0", "kHuiSel0"),
    ("HUI_BANK_L", "kHuiBankL"),
    ("HUI_BANK_R", "kHuiBankR"),
    ("HUI_PLAY", "kHuiPlay"),
    ("HUI_STOP", "kHuiStop"),
    ("HUI_RECORD", "kHuiRecord"),
)


def parse_midi_py(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for m in re.finditer(r"^([A-Z][A-Z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*$", text, re.M):
        out[m.group(1)] = int(m.group(2), 0)
    return out


def parse_surface_h(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for m in re.finditer(r"\b(k[A-Za-z0-9]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*,?", text):
        out[m.group(1)] = int(m.group(2), 0)
    return out


def parse_sk_hui_c(text: str) -> dict[str, int]:
    """HUI_N_* enums from firmware/src/sk_hui.c."""
    out: dict[str, int] = {}
    for m in re.finditer(r"\b(HUI_N_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*,?", text):
        out[m.group(1)] = int(m.group(2), 0)
    return out


# firmware HUI_N_* → SurfaceCard kHui*
HUI_FW_PAIRS = (
    ("HUI_N_REC0", "kHuiRec0"),
    ("HUI_N_SOLO0", "kHuiSolo0"),
    ("HUI_N_MUTE0", "kHuiMute0"),
    ("HUI_N_SEL0", "kHuiSel0"),
    ("HUI_N_BANK_L", "kHuiBankL"),
    ("HUI_N_BANK_R", "kHuiBankR"),
    ("HUI_N_PLAY", "kHuiPlay"),
    ("HUI_N_STOP", "kHuiStop"),
    ("HUI_N_RECORD", "kHuiRecord"),
)


def main() -> int:
    if not MIDI_PY.is_file() or not SURFACE_H.is_file():
        print(f"missing map files:\n  {MIDI_PY}\n  {SURFACE_H}", file=sys.stderr)
        return 1

    py = parse_midi_py(MIDI_PY.read_text())
    hh = parse_surface_h(SURFACE_H.read_text())
    fails: list[str] = []
    checked = 0

    for py_name, h_name in PAIRS:
        if py_name not in py:
            fails.append(f"midi.py missing {py_name}")
            continue
        if h_name not in hh:
            fails.append(f"SurfaceCard.h missing {h_name}")
            continue
        checked += 1
        if py[py_name] != hh[h_name]:
            fails.append(f"{py_name}={py[py_name]} != {h_name}={hh[h_name]}")

    for name, want in ANCHORS.items():
        got = py.get(name)
        if got != want:
            fails.append(f"anchor {name}: want {want} got {got}")

    hui_checked = 0
    for py_name, h_name in HUI_PAIRS:
        if py_name not in py:
            fails.append(f"midi.py missing {py_name}")
            continue
        if h_name not in hh:
            fails.append(f"SurfaceCard.h missing {h_name}")
            continue
        hui_checked += 1
        checked += 1
        if py[py_name] != hh[h_name]:
            fails.append(f"{py_name}={py[py_name]} != {h_name}={hh[h_name]}")

    fw_hui = 0
    if SK_HUI_C.is_file():
        fw = parse_sk_hui_c(SK_HUI_C.read_text())
        for fw_name, h_name in HUI_FW_PAIRS:
            if fw_name not in fw:
                fails.append(f"sk_hui.c missing {fw_name}")
                continue
            if h_name not in hh:
                fails.append(f"SurfaceCard.h missing {h_name}")
                continue
            fw_hui += 1
            if fw[fw_name] != hh[h_name]:
                fails.append(f"{fw_name}={fw[fw_name]} != {h_name}={hh[h_name]}")

    if fails:
        print("surface map FAIL:")
        for f in fails:
            print(" ", f)
        return 1

    print(
        "surface map ok "
        f"(kPad0={hh['kPad0']} kStopAll={hh['kStopAll']} kFace={hh['kFace']} "
        f"kPlay/Stop/Rec={hh['kPlay']}/{hh['kStop']}/{hh['kRec']} "
        f"kCcKnob0={hh['kCcKnob0']} kCcFader0={hh['kCcFader0']}; "
        f"{checked} pairs; {hui_checked} HUI midi.py; {fw_hui} HUI fw)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
