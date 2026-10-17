#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
"""Phase-8 import-bay Bulk32 structural check (ImportBay.h LoadDx7Bulk / ExportDx7Bulk).

Builds a synthetic Yamaha DX7 32-voice packed bank SysEx, parses it with the same
layout rules documented in plugin/StudioKEYS/ImportBay.h (skimport::Dx7ToFm6Packed,
ImportBay::LoadDx7Bulk / ExportDx7Bulk), and asserts:

  - 32 bay rows
  - every name non-empty
  - converted FM6 blob size == 128
  - original 128-byte packed voice preserved bit-for-bit
  - export → re-import round-trip keeps 32 rows / names / originals

This is a structural Python mirror of the C++ header (ImportBay pulls Librarian /
sk_host, so a tiny CLI #include is heavier than needed here). Not a bit-exact
compile against ImportBay.h.
"""
from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Same bytes as skimport::kFm6Init in ImportBay.h
K_FM6_INIT = bytes(
    [
        99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0,
        0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99,
        99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0, 2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 0,
        2, 0, 99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 56, 0, 99, 2, 0, 99, 99, 99, 99, 50, 50, 50, 50,
        0, 8, 35, 0, 0, 0, 49, 24, 73, 78, 73, 84, 32, 86, 79, 73, 67, 69,
    ]
)
assert len(K_FM6_INIT) == 128


@dataclass
class BayRow:
    name: str
    engine: str = "FM6"
    mono: int = 0
    glide: int = 0
    glmode: int = 0
    converted: bytes = field(default_factory=bytes)
    original: bytes = field(default_factory=bytes)


def trim_name(nm: bytes) -> str:
    s = []
    for c in nm[:10]:
        ch = chr(c) if 32 <= c <= 126 else " "
        s.append(ch)
    return "".join(s).rstrip()


def dx7_to_fm6_packed(dx7: bytes) -> bytes:
    """Mirror skimport::Dx7ToFm6Packed."""
    assert len(dx7) >= 128
    out = bytearray(K_FM6_INIT)
    for k in range(6):
        s = 17 * k
        for i in range(17):
            out[s + i] = dx7[s + i] & 0x7F
    for i in range(102, 118):
        out[i] = dx7[i] & 0x7F
    out[110] &= 31
    out[111] = (dx7[111] & 7) | (dx7[111] & 8)
    for i in range(10):
        c = dx7[118 + i] & 0x7F
        out[118 + i] = c if 32 <= c <= 126 else ord(" ")
    return bytes(out)


def find_dx7_porta(data: bytes) -> tuple[int, int]:
    """Mirror skimport::FindDx7Porta — optional format 0x02 function dump."""
    mono = glide = 0
    n = len(data)
    i = 0
    while i + 8 < n:
        if data[i] != 0xF0 or data[i + 1] != 0x43:
            i += 1
            continue
        fmt = data[i + 3]
        if fmt in (0x09, 0x00):
            i += 1
            continue
        if fmt == 0x02 and i + 10 < n:
            j = i + 4
            while j < n and data[j] != 0xF7:
                j += 1
            if j > i + 6:
                body = i + 6
                if body + 1 < j:
                    mono = 1 if data[body] > 0 else 0
                    glide = data[body + 1] & 0x7F
                    return mono, glide
        i += 1
    return 0, 0


def load_dx7_bulk(data: bytes) -> list[BayRow]:
    """Mirror ImportBay::LoadDx7Bulk."""
    if not data or len(data) < 6 + 128 or data[0] != 0xF0:
        return []
    off = 0
    n = len(data)
    while off + 6 < n and not (
        data[off] == 0xF0 and data[off + 1] == 0x43 and data[off + 3] == 0x09 and data[off + 4] == 0x20
    ):
        off += 1
    if off + 6 >= n:
        return []
    body = data[off:]
    remain = len(body)
    bank_mono, bank_glide = find_dx7_porta(data)
    rows: list[BayRow] = []
    voice_off = 6
    for v in range(32):
        if voice_off + 128 > remain:
            break
        voice = body[voice_off : voice_off + 128]
        name = trim_name(voice[118:128])
        if not name:
            name = f"DX7 {v + 1}"
        rows.append(
            BayRow(
                name=name,
                engine="FM6",
                mono=bank_mono,
                glide=bank_glide,
                converted=dx7_to_fm6_packed(voice),
                original=bytes(voice),
            )
        )
        voice_off += 128
    return rows


def export_dx7_bulk(rows: list[BayRow]) -> bytes:
    """Mirror ImportBay::ExportDx7Bulk for FM6 bay rows (pads with INIT)."""
    out = bytearray(6 + 128 * 32 + 2)
    out[0:6] = bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00])
    init = dx7_to_fm6_packed(bytes(128))
    for v in range(32):
        dst = 6 + v * 128
        out[dst : dst + 128] = init
        if v < len(rows) and rows[v].engine == "FM6":
            r = rows[v]
            if len(r.original) >= 128:
                out[dst : dst + 128] = r.original[:128]
            elif len(r.converted) >= 128:
                out[dst : dst + 128] = r.converted[:128]
            nm = (r.name.encode("ascii", "replace")[:10]).ljust(10)
            out[dst + 118 : dst + 128] = nm
    body_sum = sum(out[6 : 6 + 128 * 32]) & 0x7F
    out[6 + 128 * 32] = (128 - body_sum) & 0x7F
    out[6 + 128 * 32 + 1] = 0xF7
    return bytes(out)


def make_packed_voice(name: str, seed: int = 0) -> bytes:
    """Minimal synthetic DX7 packed voice (128 B): INIT overlay + unique alg/name."""
    v = bytearray(K_FM6_INIT)
    # Distinct algorithm / feedback so voices aren't identical blobs
    v[110] = seed % 32
    v[111] = (seed % 8) | ((seed // 8) & 8)
    nm = (name.encode("ascii", "replace")[:10]).ljust(10, b" ")
    v[118:128] = nm
    # Keep 7-bit clean like a real SysEx body
    for i in range(128):
        v[i] &= 0x7F
    return bytes(v)


def make_bulk_sysex(voices: list[bytes]) -> bytes:
    assert len(voices) == 32
    out = bytearray([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00])
    for voice in voices:
        assert len(voice) == 128
        out.extend(voice)
    body_sum = sum(out[6:]) & 0x7F
    out.append((128 - body_sum) & 0x7F)
    out.append(0xF7)
    assert len(out) == 6 + 128 * 32 + 2  # 4104
    return bytes(out)


def try_fixture() -> bytes | None:
    for rel in (
        "tests/fixtures/dx7_bulk32.syx",
        "tests/dx7_bulk32.syx",
        "tests/fixtures/dx7_bank.syx",
    ):
        p = ROOT / rel
        if p.is_file():
            return p.read_bytes()
    return None


def main() -> int:
    fixture = try_fixture()
    if fixture is not None:
        syx = fixture
        src = "fixture"
    else:
        voices = []
        for i in range(32):
            # Voice 0 blank name → LoadDx7Bulk must invent "DX7 1"
            name = "" if i == 0 else f"Vox{i:02d}"
            voices.append(make_packed_voice(name, seed=i + 1))
        syx = make_bulk_sysex(voices)
        src = "synthetic"

    fails: list[str] = []

    def check(cond: bool, msg: str) -> None:
        if not cond:
            fails.append(msg)

    # Header signature (ImportBay scan key)
    check(syx[0] == 0xF0 and syx[1] == 0x43 and syx[3] == 0x09 and syx[4] == 0x20, "bulk header F0 43 xx 09 20")
    check(syx[-1] == 0xF7, "bulk ends with F7")
    check(len(syx) >= 6 + 128 * 32 + 2, f"bulk length >= 4104 (got {len(syx)})")

    rows = load_dx7_bulk(syx)
    check(len(rows) == 32, f"want 32 rows, got {len(rows)}")

    for i, r in enumerate(rows):
        check(bool(r.name) and r.name.strip() != "", f"row {i}: empty name")
        check(r.engine == "FM6", f"row {i}: engine {r.engine!r} != FM6")
        check(len(r.converted) == 128, f"row {i}: converted size {len(r.converted)} != 128")
        check(len(r.original) == 128, f"row {i}: original size {len(r.original)} != 128")
        # Original preserved from bank body
        body_voice = syx[6 + i * 128 : 6 + (i + 1) * 128]
        check(r.original == body_voice, f"row {i}: original != bank voice bytes")
        # Converted follows Dx7ToFm6Packed rules
        expect = dx7_to_fm6_packed(r.original)
        check(r.converted == expect, f"row {i}: converted != Dx7ToFm6Packed(original)")

    if src == "synthetic" and rows:
        check(rows[0].name == "DX7 1", f"blank name → DX7 1, got {rows[0].name!r}")
        check(rows[1].name == "Vox01", f"row 1 name Vox01, got {rows[1].name!r}")

    # Export → re-import round-trip (ExportDx7Bulk overwrites name bytes 118..127
    # with the bay display name — blank-import "DX7 N" therefore changes original.)
    exported = export_dx7_bulk(rows)
    check(len(exported) == 4104, f"export length {len(exported)} != 4104")
    check(exported[0:6] == bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00]), "export header")
    check(exported[-1] == 0xF7, "export F7")
    again = load_dx7_bulk(exported)
    check(len(again) == 32, f"re-import want 32, got {len(again)}")
    if len(again) == 32 and len(rows) == 32:
        for i in range(32):
            check(again[i].name == rows[i].name, f"round-trip name[{i}] {again[i].name!r} != {rows[i].name!r}")
            check(len(again[i].converted) == 128, f"round-trip converted[{i}] size")
            exp_voice = bytearray(rows[i].original)
            nm = (rows[i].name.encode("ascii", "replace")[:10]).ljust(10)
            exp_voice[118:128] = nm
            check(
                again[i].original == bytes(exp_voice),
                f"round-trip original[{i}] != export voice (name overlay)",
            )
            # Non-name payload unchanged through export
            check(
                again[i].original[:118] == rows[i].original[:118],
                f"round-trip original[{i}] operator/global bytes changed",
            )

    if fails:
        print(f"check_bay_bulk FAIL ({src}):", file=sys.stderr)
        for f in fails:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print(f"check_bay_bulk ok ({src}): 32 rows, FM6 blob=128, original preserved, bulk round-trip")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
