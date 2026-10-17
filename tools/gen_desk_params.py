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


def parse_edit(path: pathlib.Path) -> list[str]:
    text = path.read_text()
    m = re.search(r"\.edit\s*=\s*\{(.*?)\n\s*\},", text, re.S)
    if not m:
        raise SystemExit(f"no .edit in {path}")
    labels = re.findall(r'\{\s*"([^"]+)"', m.group(1))
    if len(labels) != 8:
        raise SystemExit(f"{path}: want 8 edit labels, got {labels}")
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
        out[name] = re.findall(r'"([^"]*)"', m.group(1))
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
        if desk.get(name) != labs:
            print(f"MISMATCH {name}: desk={desk.get(name)} fw={labs}", file=sys.stderr)
            bad += 1
    if bad:
        print(f"{bad} engine desk label table(s) out of sync with firmware", file=sys.stderr)
        return 1
    print("desk params match firmware edit[] labels")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
