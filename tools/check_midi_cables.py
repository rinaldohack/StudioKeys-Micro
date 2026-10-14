#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assert four-cable product map in MidiCables.h / plan (ports 1..4)."""
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
HDR = ROOT / "plugin" / "StudioKEYS" / "MidiCables.h"


def main() -> int:
    t = HDR.read_text()
    need = [
        ("Performance = 0", "cable 0 / port 1"),
        ("Live = 1", "cable 1"),
        ("Hui = 2", "cable 2"),
        ("Link = 3", "cable 3"),
        ("AcceptsNotes", "Link rejects notes"),
    ]
    for needle, why in need:
        if needle not in t:
            print(f"missing {needle!r} ({why})", file=sys.stderr)
            return 1
    if "return c != Cable::Link" not in t and "c != Cable::Link" not in t:
        print("Link must reject notes", file=sys.stderr)
        return 1
    print("midi cables ok Performance/Live/HUI/Link")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
