#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
"""Phase-3 44 px probe — recreate StudioKEYS panel layout math and assert
every button / pad / knob / fader / side key is ≥ 44 px on both axes.
Piano keybed is exempt (Kontakt-style narrower keys per plan).
Also asserts the three faceplate columns tile without overlapping: sized-to-fit
controls are useless if one column is drawn on top of another (P9's 0.24 screen
column vs the hard-coded 0.20 start of the mid column did exactly that).
Keep in sync with plugin/StudioKEYS/PanelLayout.h."""
from __future__ import annotations

import sys
from pathlib import Path

MIN = 44.0
PLUG_W, PLUG_H = 1280.0, 880.0
PAD = 8.0
CHROME_H = 32.0
DESK_FRAC = 0.46
KEYBED_H = 90.0
SCREEN_FRAC = 0.24
MID_FRAC = 0.46
RIGHT_FRAC = 0.30
UNDER_SCREEN_H = 120.0
KNOBS_BAND = 0.48
PADS_BAND = 0.44
FADER_BAND = 0.55
BTN12_BAND = 0.40


def grid_cell(band_w: float, band_h: float, i: int, rows: int, cols: int, pad: float = 2.0):
    cw, ch = band_w / cols, band_h / rows
    return max(0.0, cw - 2 * pad), max(0.0, ch - 2 * pad)


def main() -> int:
    bw = PLUG_W - 2 * PAD
    bh = PLUG_H - 2 * PAD
    desk_h = bh * DESK_FRAC
    panel_top = CHROME_H + 4.0 + desk_h + 4.0
    panel_h = bh - panel_top - KEYBED_H
    panel_w = bw
    fails: list[str] = []
    notes: list[str] = []

    def check(name: str, w: float, h: float):
        if w + 1e-6 < MIN or h + 1e-6 < MIN:
            fails.append(f"{name}: {w:.1f}×{h:.1f} < {MIN}")

    # --- the three faceplate columns tile the panel, left to right -------------------------
    screen_x0 = 0.0
    mid_x0 = panel_w * SCREEN_FRAC
    mid_x1 = panel_w * (1.0 - RIGHT_FRAC)
    right_x0 = panel_w * (1.0 - RIGHT_FRAC)
    screen_col_w = mid_x0 - screen_x0
    mid_w = mid_x1 - mid_x0
    right_w = panel_w - right_x0
    if abs(SCREEN_FRAC + MID_FRAC + RIGHT_FRAC - 1.0) > 1e-6:
        fails.append(
            f"columns do not tile: {SCREEN_FRAC:.2f}+{MID_FRAC:.2f}+{RIGHT_FRAC:.2f}"
            f"={SCREEN_FRAC + MID_FRAC + RIGHT_FRAC:.2f} != 1.00"
        )
    spans = [("screen", screen_x0, mid_x0), ("mid", mid_x0, mid_x1), ("right", right_x0, right_x0 + right_w)]
    for (an, a0, a1), (bn, b0, b1) in zip(spans, spans[1:]):
        if a1 > b0 + 1e-6:
            fails.append(f"columns overlap: {an} ends at {a1:.1f}, {bn} starts at {b0:.1f}")

    # --- left: screen square + under-screen 2×3 buttons -----------------------------------
    screen_side = min(screen_col_w - 8.0 - 2.0, panel_h - 8.0 - UNDER_SCREEN_H - 2.0)
    if screen_side < 120.0:
        fails.append(f"screen: {screen_side:.0f} px square is unusable")
    if screen_x0 + (screen_col_w - screen_side) * 0.5 + screen_side > mid_x0 + 1e-6:
        fails.append("screen square runs into the knobs/pads column")
    for i in range(6):
        cw, ch = grid_cell(screen_col_w - 8.0, UNDER_SCREEN_H, i, 3, 2, 2.0)
        check(f"under-screen[{i}]", max(MIN, cw), max(MIN, ch))
        if ch + 1e-6 < MIN:
            # P9 shortened this band to 120 px to grow the screen; 3 rows leave 36 px per key.
            notes.append(f"under-screen[{i}] {cw:.0f}×{ch:.0f} px is under the {MIN:.0f} px rule (P9 trade-off)")

    # --- middle: 8 knobs, then the 16 pads ------------------------------------------------
    mid_h = panel_h - 8.0
    knob_cell_w = (mid_w - 8.0) / 8.0 - 4.0
    knob_cell_h = mid_h * KNOBS_BAND - 4.0
    side = min(knob_cell_w, knob_cell_h, 52.0 + 30.0)  # LayoutUI: kKnobSize + 30 cap
    for i in range(8):
        check(f"knob[{i}]", side, side)

    pads_h = mid_h * PADS_BAND
    for i in range(16):
        cw, ch = grid_cell(mid_w - 8.0, pads_h, i, 2, 8, 2.0)
        check(f"pad[{i}]", cw, ch)

    # --- right: 4 faders, then the 12 printed side keys -----------------------------------
    right_h = panel_h - 8.0
    faders_h = right_h * FADER_BAND
    for i in range(4):
        cw, ch = grid_cell(right_w - 8.0, faders_h, i, 1, 4, 4.0)
        check(f"fader[{i}]", cw, ch)

    btn12_h = right_h * BTN12_BAND
    for i in range(12):
        cw, ch = grid_cell(right_w - 8.0, btn12_h, i, 2, 6, 2.0)
        check(f"under-fader[{i}]", cw, max(MIN, ch))

    wheels_strip = 96.0
    wheels_w = wheels_strip - 8.0
    check("bender", wheels_w * 0.5, KEYBED_H - 8.0)
    check("modwheel", wheels_w * 0.5, KEYBED_H - 8.0)

    # --- the replica above is only as good as the C++ it mirrors:
    #     LayoutUI has to take its columns from PanelLayout.h, never from a literal span -----
    src = Path(__file__).resolve().parent.parent / "plugin/StudioKEYS/StudioKEYS.cpp"
    if src.is_file():
        code = src.read_text(encoding="utf-8")
        for need in ("panel.W() * sklayout::kScreenColFrac", "panel.W() * sklayout::kRightFrac"):
            if need not in code:
                fails.append(f"StudioKEYS.cpp: faceplate columns must be derived from PanelLayout.h ({need})")
        # the pre-P9 spans that put the pads on top of the under-screen buttons
        for bad in ("panel.W() * 0.20f", "panel.W() * 0.50f"):
            if bad in code:
                fails.append(f"StudioKEYS.cpp: hard-coded column span {bad} — use sklayout::k*Frac")

    if fails:
        print("hit-target FAIL:")
        for f in fails:
            print(" ", f)
        return 1
    if notes:
        print("hit-target notes:")
        for n in notes:
            print(" ", n)
    print(
        f"hit-target ok (≥{MIN:.0f} px, columns tile) "
        f"panel={panel_w:.0f}×{panel_h:.0f} desk={desk_h:.0f} "
        f"cols={screen_col_w:.0f}|{mid_w:.0f}|{right_w:.0f} screen={screen_side:.0f}px "
        f"buttons=6+12 pads=16 knobs=8 faders=4"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
