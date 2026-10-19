#!/usr/bin/env python3
"""Draw the P8 PAGE inspector at its real geometry, the way SkPageKnob does now.

The rects come from StudioKEYS.cpp RebuildDesk and SkPanelControls.h SkPageKnob::Draw (see
tools/check_page_inspector_layout.cpp, which asserts they do not overlap). This renders a PNG of
the panel with the labels and values of a given page, so a layout change can be looked at without
opening a DAW. Not part of the build.

    python3 tools/preview_page_inspector.py out.png --page lofi
    python3 tools/preview_page_inspector.py out.png --page phys
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "..", "resources", "fonts", "Roboto-Regular.ttf")

# the mint palette (SkMint.h)
BG = (18, 21, 24)
WELL = (30, 34, 38)
THEME = (72, 82, 92)
INK = (170, 182, 194)
ACCENT = (126, 220, 188)

PLUG_W, PLUG_H = 1280, 880
CHROME_H, DESK_FRAC = 32.0, 0.46
STRIP_H = 112.0   # not used: the height is derived

# the pages, as sk_host_format_param prints them (host/sk_host.c -> params.c param_format)
PAGES = {
    "lofi": [("CHIP", "4BIT", 0.0), ("WAVE", "PLS", 0.0), ("DUTY", "32", 0.25), ("CRSH", "0 %", 0.0),
             ("SWP", "0 %", 0.0), ("VIB", "16 %", 0.13), ("ARP", "OFF", 0.0), ("TONE", "100 %", 1.0)],
    "phys": [("MODEL", "SYMP", 1.0), ("CHRD", "ROOT", 1.0), ("BRIT", "63 %", 0.63), ("DAMP", "69 %", 0.69),
             ("SYMP", "50 %", 0.5), ("ACC", "79 %", 0.79), ("BUZZ", "72 %", 0.72), ("EXC", "0 %", 0.0)],
    "fm6": [("ALG", "PAT", 0.0), ("FB", "0", 0.5), ("MLVL", "0 %", 0.5), ("MRAT", "0", 0.5),
            ("MEG", "0 %", 0.5), ("VMOD", "0", 0.5), ("DTUN", "0 %", 0.5), ("SLOT", "OWN", 1.0)],
}


def rect(x, y, w, h):
    return (x, y, x + w, y + h)


def get_padded(r, p):
    return (r[0] - p, r[1] - p, r[2] + p, r[3] + p)


def from_top(r, a):
    return (r[0], r[1], r[2], r[1] + a)


def from_bottom(r, a):
    return (r[0], r[3] - a, r[2], r[3])


def reduced_from_top(r, a):
    return (r[0], r[1] + a, r[2], r[3])


def reduced_from_bottom(r, a):
    return (r[0], r[1], r[2], r[3] - a)


def from_right(r, a):
    return (r[2] - a, r[1], r[2], r[3])


def grid_cell(r, idx, nr, nc):
    h = (r[3] - r[1]) / nr
    w = (r[2] - r[0]) / nc
    row, col = idx // nc, idx % nc
    return (r[0] + w * col, r[1] + h * row, r[0] + w * (col + 1), r[1] + h * (row + 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--page", default="lofi", choices=sorted(PAGES))
    ap.add_argument("--scale", type=float, default=2.0, help="render scale")
    args = ap.parse_args()

    b = (10, 10, PLUG_W - 10, PLUG_H - 10)
    desk_band = from_top(reduced_from_top(b, CHROME_H + 4), (b[3] - b[1]) * DESK_FRAC)
    body = get_padded(reduced_from_top(desk_band, 32), -8)
    mixer = get_padded(from_right(body, (body[2] - body[0]) * 0.24), -4)
    page_h = max(96.0, min(124.0, (mixer[3] - mixer[1]) * 0.36))
    page = get_padded(from_bottom(mixer, page_h), -2)
    pg = get_padded(reduced_from_top(page, 22), -4)

    s = args.scale
    W, H = int((page[2] - page[0]) * s) + 8, int((page[3] - page[1]) * s) + 8
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    f_title = ImageFont.truetype(FONT, int(11 * s))
    f_label = ImageFont.truetype(FONT, int(11 * s))
    f_val = ImageFont.truetype(FONT, int(14 * s))

    def T(r):
        return ((r[0] - page[0]) * s + 4, (r[1] - page[1]) * s + 4, (r[2] - page[0]) * s + 4,
                (r[3] - page[1]) * s + 4)

    d.rounded_rectangle(T(page), radius=5 * s, fill=WELL, outline=THEME, width=max(1, int(1.25 * s)))
    d.text((T(page)[0] + 8 * s, T(page)[1] + 4 * s), "PAGE", font=f_title, fill=THEME)

    cells = PAGES[args.page]
    for i, (label, value, frac) in enumerate(cells):
        cell = get_padded(grid_cell(pg, i, 2, 4), -3)
        inner = get_padded(cell, -4)
        d.rounded_rectangle(T(cell), radius=4 * s, fill=(24, 27, 31), outline=THEME, width=max(1, int(s)))
        x0, y0, x1, y1 = T(inner)
        d.text((x0, y0), label, font=f_label, fill=INK)
        # the value box: from the label's bottom to 2 px above the bar, vertically centred
        vy0, vy1 = y0 + 11 * s, y1 - 2 * s
        vx = x1 - d.textlength(value, font=f_val)
        d.text((x0 + (x1 - x0 - (x1 - vx)) * 0.0, (vy0 + vy1) / 2 - 7 * s), value, font=f_val, fill=ACCENT)
        bar = from_bottom(inner, 2)
        bx0, by0, bx1, by1 = T(bar)
        d.rounded_rectangle((bx0, by0, bx1, by1), radius=s, fill=(40, 45, 50))
        if frac > 0:
            d.rounded_rectangle((bx0, by0, bx0 + (bx1 - bx0) * frac, by1), radius=s, fill=ACCENT)

    img.save(args.out)
    print(f"{args.out}: page box {page[2]-page[0]:.0f} x {page[3]-page[1]:.0f}, "
          f"cell {get_padded(grid_cell(pg, 0, 2, 4), -3)[2]-get_padded(grid_cell(pg, 0, 2, 4), -3)[0]:.0f} x "
          f"{get_padded(grid_cell(pg, 0, 2, 4), -3)[3]-get_padded(grid_cell(pg, 0, 2, 4), -3)[1]:.0f}")


if __name__ == "__main__":
    main()
