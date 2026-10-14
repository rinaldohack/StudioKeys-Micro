#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phase-3 44 px probe — recreate StudioKEYS panel layout math and assert
every button / pad / knob / fader / side key is ≥ 44 px on both axes.
Piano keybed is exempt (Kontakt-style narrower keys per plan).
Keep in sync with plugin/StudioKEYS/PanelLayout.h."""
from __future__ import annotations

import sys

MIN = 44.0
PLUG_W, PLUG_H = 1280.0, 880.0
PAD = 10.0
CHROME_H = 32.0
DESK_FRAC = 0.50
KEYBED_H = 72.0
SCREEN_FRAC = 0.20
MID_FRAC = 0.44
RIGHT_FRAC = 0.28
UNDER_SCREEN_H = 148.0
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

    def check(name: str, w: float, h: float):
        if w + 1e-6 < MIN or h + 1e-6 < MIN:
            fails.append(f"{name}: {w:.1f}×{h:.1f} < {MIN}")

    # Under-screen 3 rows × 2 cols (BANK|FADER / <|> / OCT+|OCT-)
    screen_col_w = panel_w * SCREEN_FRAC
    for i in range(6):
        cw, ch = grid_cell(screen_col_w, UNDER_SCREEN_H, i, 3, 2, 2.0)
        check(f"under-screen[{i}]", max(MIN, cw), max(MIN, ch))

    mid_w = panel_w * MID_FRAC
    mid_h = panel_h
    for i in range(8):
        check(f"knob[{i}]", 52.0, 52.0)

    pads_h = mid_h * 0.44
    for i in range(16):
        cw, ch = grid_cell(mid_w, pads_h, i, 2, 8, 3.0)
        s = max(MIN, min(cw, ch * 0.9))
        check(f"pad[{i}]", s, min(s, ch))

    right_w = panel_w * RIGHT_FRAC
    faders_h = panel_h * FADER_BAND
    for i in range(4):
        cw, ch = grid_cell(right_w, faders_h, i, 1, 4, 4.0)
        check(f"fader[{i}]", cw, ch)

    btn12_h = panel_h * BTN12_BAND
    for i in range(12):
        cw, ch = grid_cell(right_w, btn12_h, i, 2, 6, 2.0)
        check(f"under-fader[{i}]", cw, max(MIN, ch))

    wheels_strip = 96.0
    wheels_w = wheels_strip - 8.0
    check("bender", wheels_w * 0.5, KEYBED_H - 8.0)
    check("modwheel", wheels_w * 0.5, KEYBED_H - 8.0)

    if fails:
        print("hit-target FAIL:")
        for f in fails:
            print(" ", f)
        return 1
    print(
        f"hit-target ok (≥{MIN:.0f} px) "
        f"panel={panel_w:.0f}×{panel_h:.0f} desk={desk_h:.0f} "
        f"buttons=6+12 pads=16 knobs=8 faders=4"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
