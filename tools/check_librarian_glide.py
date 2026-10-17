#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
"""Phase-7 librarian glide check (Librarian.h SeedDemo / CanSendToKeyboard).

Parses plugin/StudioKEYS/Librarian.h SeedDemo literals and asserts:

  - at least two FM6 seed patches with distinct glide times (6 and 24)
  - CanSendToKeyboard is HardwareReady-only (VstOnly cannot send)

This is a structural Python mirror of the C++ header — SeedDemo / CanSendToKeyboard
are inline in Librarian.h; a tiny CLI #include pulls sk_host and is heavier than needed.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIBRARIAN_H = ROOT / "plugin" / "StudioKEYS" / "Librarian.h"

# Phase 7: two FM6 patches with different glide times.
EXPECTED_FM6_GLIDES = {6, 24}


def extract_seed_demo(text: str) -> str:
    m = re.search(
        r"void\s+SeedDemo\s*\(\s*\)\s*\{(.*?)(?=\n  /\*\*|\n  void |\n  bool |\n};\n)",
        text,
        re.S,
    )
    if not m:
        raise AssertionError("SeedDemo() not found in Librarian.h")
    return m.group(1)


def parse_fm6_glides(seed: str) -> list[tuple[str, int]]:
    """Find LibPatch blocks with engine FM6 and their glide literals."""
    # Split on LibPatch declarations inside SeedDemo.
    blocks = re.split(r"LibPatch\s+(\w+)\s*;", seed)
    # blocks[0] = preamble, then name, body, name, body, ...
    found: list[tuple[str, int]] = []
    i = 1
    while i + 1 < len(blocks):
        var = blocks[i]
        body = blocks[i + 1]
        eng = re.search(rf"{re.escape(var)}\.engine\s*=\s*\"([^\"]+)\"", body)
        glide_m = re.search(rf"{re.escape(var)}\.glide\s*=\s*(\d+)", body)
        name_m = re.search(rf"{re.escape(var)}\.name\s*=\s*\"([^\"]+)\"", body)
        if eng and eng.group(1) == "FM6" and glide_m:
            label = name_m.group(1) if name_m else var
            found.append((label, int(glide_m.group(1))))
        i += 2
    return found


def check_can_send(text: str) -> None:
    """Document from source: VstOnly returns false (HardwareReady only)."""
    m = re.search(
        r"bool\s+CanSendToKeyboard\s*\([^)]*\)\s*const\s*\{(.*?)\}",
        text,
        re.S,
    )
    if not m:
        raise AssertionError("CanSendToKeyboard not found in Librarian.h")
    body = " ".join(m.group(1).split())
    # Conceptual: only HardwareReady can send → VstOnly is false.
    if "LibKind::HardwareReady" not in body:
        raise AssertionError(
            "CanSendToKeyboard must gate on LibKind::HardwareReady "
            f"(VstOnly → false); got: {body!r}"
        )
    if not re.search(r"return\s+p\.kind\s*==\s*LibKind::HardwareReady\s*;", body):
        raise AssertionError(
            "CanSendToKeyboard expected "
            "`return p.kind == LibKind::HardwareReady;` "
            f"got: {body!r}"
        )


def main() -> int:
    text = LIBRARIAN_H.read_text(encoding="utf-8")
    seed = extract_seed_demo(text)
    fm6 = parse_fm6_glides(seed)
    if len(fm6) < 2:
        print(f"FAIL: need ≥2 FM6 SeedDemo patches, found {fm6}", file=sys.stderr)
        return 1
    glides = {g for _, g in fm6}
    if not EXPECTED_FM6_GLIDES.issubset(glides):
        print(
            f"FAIL: SeedDemo FM6 glides {sorted(glides)} "
            f"missing expected {sorted(EXPECTED_FM6_GLIDES)} "
            f"(patches={fm6})",
            file=sys.stderr,
        )
        return 1
    if len(glides) < 2:
        print(
            f"FAIL: FM6 SeedDemo glides must differ; got {fm6}",
            file=sys.stderr,
        )
        return 1

    check_can_send(text)

    names = ", ".join(f"{n} glide={g}" for n, g in fm6)
    print(f"librarian glide ok ({names}; CanSendToKeyboard=HardwareReady only)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
