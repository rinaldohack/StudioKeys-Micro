#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
"""Verify plugin desk labels against firmware eng_*.c .edit tables (plan phase 4)."""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ORDER = [
    ("ANALOG", "eng_analog.c"),
    ("FM6", "eng_fm6.c"),
    ("PHASE", "eng_phase.c"),
    ("LOFI", "eng_lofi.c"),
    ("SAMPLE", "eng_sample.c"),
    ("VOICE", "eng_formant.c"),
    ("TRIO", "eng_trio.c"),
    ("WHEEL", "eng_wheel.c"),
    ("GRAIN", "eng_grain.c"),
    ("PHYS", "eng_phys.c"),
    ("NOISE", "eng_noise.c"),
    ("SLICE", "eng_slice.c"),
    ("DRUM", "eng_drum.c"),
]
DESK = ROOT / "plugin" / "StudioKEYS" / "DeskParams.h"

# The desk's own grid is 4 x 2, so `kParams_*[]` and `EngineDesk::nParams` are 8. The engines have
# had 12 `.edit` rows since P6 (E0..E11: WHEEL's nine bars plus PERC / CLICK / ROTR, and so on), and
# the desk draws its labels live through `sk_host_edit_label`, so the table is the fallback for the
# first 8 only. This gate therefore compares the 8 the desk can show, and reports the rows past them
# rather than failing on them - a 12-label mirror nothing reads would hide a real mismatch in the 8.
# `--print` lists all of them.
DESK_N = 8


def parse_edit(path: pathlib.Path) -> list[str]:
    text = path.read_text()
    m = re.search(r"\.edit\s*=\s*\{(.*?)\n\s*\},", text, re.S)
    if not m:
        raise SystemExit(f"no .edit in {path}")
    labels = re.findall(r'\{\s*"([^"]+)"', m.group(1))
    if len(labels) < DESK_N:
        raise SystemExit(f"{path}: want at least {DESK_N} edit labels, got {labels}")
    return labels


def fw_tables() -> dict[str, list[str]]:
    return {name: parse_edit(ROOT / "firmware" / "src" / fn) for name, fn in ORDER}


def desk_tables() -> dict[str, list[str]]:
    text = DESK.read_text()
    out: dict[str, list[str]] = {}
    for name, _ in ORDER:
        m = re.search(rf"kParams_{name}\[\]\s*=\s*\{{([^}}]+)\}}", text)
        if not m:
            raise SystemExit(f"DeskParams.h missing kParams_{name}")
        labels = re.findall(r'"([^"]*)"', m.group(1))
        if len(labels) != DESK_N:
            raise SystemExit(f"DeskParams.h kParams_{name}: want {DESK_N} labels, got {labels}")
        out[name] = labels
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", default=True)
    ap.add_argument("--print", action="store_true")
    args = ap.parse_args()
    fw = fw_tables()
    if args.print:
        for n, labs in fw.items():
            print(f"{n}: {labs}")
        return 0
    desk = desk_tables()
    bad = 0
    for name, labs in fw.items():
        if desk.get(name) != labs[:DESK_N]:
            print(f"MISMATCH {name}: desk={desk.get(name)} fw={labs[:DESK_N]}", file=sys.stderr)
            bad += 1
            continue
        if len(labs) > DESK_N:                  # (E8..E11: on the CFW's own pages, not on the desk)
            print(f"{name}: desk {DESK_N}, firmware {len(labs)} rows "
                  f"(beyond the desk: {', '.join(repr(x) for x in labs[DESK_N:])})")
    if bad:
        print(f"{bad} engine desk label table(s) out of sync with firmware", file=sys.stderr)
        return 1
    print("desk params match firmware edit[] labels")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
