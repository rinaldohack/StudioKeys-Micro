#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phase-8 import-bay Virus structural check (ImportBay.h LoadVirusTi / ExportVirus).

Builds a minimal Access/Virus SysEx (classic single: F0 00 20 33 01 dd 10 bb ss
… 256-byte pages A+B … cs F7) matching StudioKEYS.cpp “Virus demo”, parses it
with the same rules as ImportBay.h LoadVirusTi comments (page A+B), and asserts:

  - ≥1 ANALOG bay row
  - name non-empty
  - converted size == 24 (12× int16 LE)
  - original SysEx message preserved bit-for-bit
  - AnalogToVirusPages-style export → re-import keeps name / ANALOG blob size

Also exercises the short Access demo path that LoadVirusTi accepts when no full
single is present. Structural Python mirror of ImportBay.h (not a C++ compile).
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@dataclass
class BayRow:
    name: str
    engine: str = "ANALOG"
    mono: int = 0
    glide: int = 0
    glmode: int = 0
    converted: bytes = field(default_factory=bytes)
    original: bytes = field(default_factory=bytes)


def clamp7(v: int) -> int:
    if v < 0:
        return 0
    if v > 127:
        return 127
    return v


def clamp16(v: int, lo: int, hi: int) -> int:
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def trim_name(nm: bytes) -> str:
    s = []
    for c in nm[:10]:
        ch = chr(c) if 32 <= c <= 126 else " "
        s.append(ch)
    return "".join(s).rstrip()


def fill_virus_init_pages() -> bytearray:
    """Mirror skimport::FillVirusInitPages."""
    page = bytearray(256)
    page[10] = 64
    page[17] = 64
    page[18] = 127
    page[21] = 32
    page[22] = 64
    page[23] = 127
    page[31] = 32
    page[33] = 64
    page[36] = 127
    page[40] = 90
    page[41] = 64
    page[42] = 30
    page[46] = 64
    page[47] = 64
    page[48] = 64
    page[55] = 64
    page[56] = 127
    page[57] = 64
    page[58] = 40
    page[59] = 10
    page[60] = 70
    page[61] = 90
    page[62] = 64
    page[63] = 60
    page[91] = 100
    page[93] = 64
    page[128 + 112 : 128 + 122] = b"-INIT-    "
    return page


def virus_wave_from_page(a: bytes) -> int:
    if a[28] & 1:
        return 6
    shape = a[17]
    if shape < 32:
        return 3
    if shape > 96:
        return 1
    return 0


def virus_pages_to_analog(page: bytes) -> tuple[list[int], int, int, str]:
    """Mirror skimport::VirusPagesToAnalog — returns (an[12], mono, glide, name)."""
    assert len(page) >= 256
    a = page
    b = page[128:]
    an = [0] * 12
    an[0] = virus_wave_from_page(a)
    an[1] = clamp16(int(a[26]) - 64, -64, 63)
    an[2] = a[33]
    an[3] = a[37]
    an[4] = a[40]
    an[5] = a[42]
    an[6] = clamp7(int(a[49]) * 21)
    an[7] = clamp16(int(a[46]) - 64, -64, 63)
    an[8] = a[59]
    an[9] = a[60]
    an[10] = a[61]
    an[11] = a[63]
    mono = 1 if a[94] > 0 else 0
    glide = a[5]
    name = trim_name(b[112:122])
    if not name:
        name = "Virus TI"
    return an, mono, glide, name


def analog_to_virus_pages(an: list[int], name: str, mono: int, glide: int) -> bytearray:
    """Mirror skimport::AnalogToVirusPages."""
    page = fill_virus_init_pages()
    page[5] = clamp7(glide)
    page[17] = 110 if an[0] == 1 else (16 if an[0] == 3 else 64)
    page[26] = clamp7(int(an[1]) + 64)
    page[28] = 1 if an[0] == 6 else 0
    page[33] = clamp7(an[2])
    page[37] = clamp7(an[3])
    page[40] = clamp7(an[4])
    page[42] = clamp7(an[5])
    page[49] = clamp7(an[6]) // 21
    if page[49] > 6:
        page[49] = 6
    page[46] = clamp7(int(an[7]) + 64)
    page[59] = clamp7(an[8])
    page[60] = clamp7(an[9])
    page[61] = clamp7(an[10])
    page[63] = clamp7(an[11])
    page[94] = 1 if mono else 0
    nm = bytearray(b" " * 10)
    raw = name.encode("ascii", "replace")[:10]
    for i, c in enumerate(raw):
        nm[i] = c if 32 <= c <= 126 else ord(" ")
    page[128 + 112 : 128 + 122] = nm
    return page


def analog_blob_pack(an: list[int]) -> bytes:
    """12× int16 LE → 24 bytes (ImportBay AnalogBlobPack)."""
    return struct.pack("<12h", *[int(x) for x in an])


def analog_blob_unpack(blob: bytes) -> list[int] | None:
    if len(blob) < 24:
        return None
    return list(struct.unpack_from("<12h", blob, 0))


def load_virus_ti(data: bytes) -> list[BayRow]:
    """Mirror ImportBay::LoadVirusTi."""
    if not data or len(data) < 4:
        return []
    rows: list[BayRow] = []
    n = len(data)
    off = 0
    while off + 8 < n:
        if data[off] != 0xF0:
            off += 1
            continue
        if off + 7 >= n or data[off + 1] != 0x00 or data[off + 2] != 0x20 or data[off + 3] != 0x33:
            off += 1
            continue
        end = off + 1
        while end < n and data[end] != 0xF7:
            end += 1
        if end >= n:
            break
        msg_len = end - off + 1
        if msg_len >= 9 and data[off + 4] == 0x01 and data[off + 6] == 0x10:
            page: bytes | None = None
            if msg_len >= 520:
                if off + 8 + 256 <= end:
                    page = data[off + 8 : off + 8 + 256]
            elif msg_len >= 9 + 256 + 2:
                page = data[off + 9 : off + 9 + 256]
            if page is not None:
                an, mono, glide, name = virus_pages_to_analog(page)
                rows.append(
                    BayRow(
                        name=name,
                        engine="ANALOG",
                        mono=mono,
                        glide=glide,
                        converted=analog_blob_pack(an),
                        original=bytes(data[off : end + 1]),
                    )
                )
        off = end + 1

    if not rows and data[0] == 0xF0 and data[1] == 0x00 and data[2] == 0x20 and data[3] == 0x33:
        # Short / partial Access message: one default ANALOG row (demo button).
        page = fill_virus_init_pages()
        if len(data) >= 9 + 128 and data[6] == 0x10:
            page[0:128] = data[9 : 9 + 128]
            if len(data) >= 9 + 256:
                page[128:256] = data[9 + 128 : 9 + 256]
        an, mono, glide, name = virus_pages_to_analog(bytes(page))
        if name in ("-INIT-", ""):
            name = "Virus TI Import"
        rows.append(
            BayRow(
                name=name,
                engine="ANALOG",
                mono=mono,
                glide=glide,
                converted=analog_blob_pack(an),
                original=bytes(data),
            )
        )
    return rows


def export_virus(row: BayRow) -> bytes:
    """Mirror ImportBay::ExportVirus (classic framed single)."""
    if row.engine != "ANALOG":
        return b""
    an = analog_blob_unpack(row.converted) or [0, 0, 64, 0, 90, 30, 0, 0, 10, 70, 90, 60]
    page = analog_to_virus_pages(an, row.name, row.mono, row.glide)
    device = 0x00
    if (
        len(row.original) >= 7
        and row.original[0] == 0xF0
        and row.original[1] == 0x00
        and row.original[2] == 0x20
        and row.original[3] == 0x33
    ):
        device = row.original[5]
    out = bytearray([0xF0, 0x00, 0x20, 0x33, 0x01, device, 0x10, 0x00, 0x40])
    out.extend(page)
    checksum = (device + 0x10 + 0x00 + 0x40 + sum(page)) & 0x7F
    out.append(checksum)
    out.append(0xF7)
    return bytes(out)


def make_classic_virus_demo() -> bytes:
    """Same layout as StudioKEYS.cpp Virus demo button."""
    virus = bytearray(9 + 256 + 2)
    virus[0:9] = bytes([0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x00, 0x40])
    page = memoryview(virus)[9 : 9 + 256]
    page[5] = 32  # A5 portamento → glide
    page[17] = 64  # A17 osc1 shape (saw)
    page[40] = 100  # A40 cutoff
    page[42] = 40  # A42 resonance
    page[94] = 1  # A94 key mode mono
    page[128 + 112 : 128 + 122] = b"VIRUS DEMO"
    checksum = (virus[5] + 0x10 + virus[7] + virus[8] + sum(virus[9 : 9 + 256])) & 0x7F
    virus[9 + 256] = checksum
    virus[9 + 256 + 1] = 0xF7
    return bytes(virus)


def make_short_access_demo() -> bytes:
    """Access header only — LoadVirusTi short/partial path → one ANALOG row."""
    return bytes([0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x00, 0x40, 0xF7])


def try_fixture() -> bytes | None:
    for rel in (
        "tests/fixtures/virus_single.syx",
        "tests/virus_single.syx",
        "tests/fixtures/virus_ti.syx",
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
        syx = make_classic_virus_demo()
        src = "synthetic"

    fails: list[str] = []

    def check(cond: bool, msg: str) -> None:
        if not cond:
            fails.append(msg)

    check(syx[0] == 0xF0, "starts with F0")
    check(syx[1:4] == bytes([0x00, 0x20, 0x33]), "Access manufacturer id 00 20 33")
    check(syx[-1] == 0xF7, "ends with F7")

    rows = load_virus_ti(syx)
    check(len(rows) >= 1, f"want ≥1 ANALOG row, got {len(rows)}")

    for i, r in enumerate(rows):
        check(bool(r.name) and r.name.strip() != "", f"row {i}: empty name")
        check(r.engine == "ANALOG", f"row {i}: engine {r.engine!r} != ANALOG")
        check(len(r.converted) == 24, f"row {i}: converted size {len(r.converted)} != 24")
        check(len(r.original) > 0, f"row {i}: original empty")
        # Original is the framed Access message (or whole short buffer)
        check(r.original[0] == 0xF0 and r.original[-1] == 0xF7, f"row {i}: original not F0…F7")
        check(r.original[1:4] == bytes([0x00, 0x20, 0x33]), f"row {i}: original Access id")

    if src == "synthetic" and rows:
        r0 = rows[0]
        check(r0.name == "VIRUS DEMO", f"demo name VIRUS DEMO, got {r0.name!r}")
        check(r0.mono == 1, f"demo mono want 1 got {r0.mono}")
        check(r0.glide == 32, f"demo glide want 32 got {r0.glide}")
        an = analog_blob_unpack(r0.converted)
        assert an is not None
        check(an[0] == 0, f"demo WAVE want 0 (saw) got {an[0]}")
        check(an[4] == 100, f"demo CUT want 100 got {an[4]}")
        check(an[5] == 40, f"demo RES want 40 got {an[5]}")
        # Original preserved bit-for-bit from synthetic SysEx
        check(r0.original == syx, "row 0: original != input SysEx")

    # Round-trip structural export (AnalogToVirusPages / ExportVirus)
    if rows:
        exported = export_virus(rows[0])
        check(len(exported) == 9 + 256 + 2, f"export length {len(exported)} != 267")
        check(exported[0:7] == bytes([0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10]), "export Access header")
        check(exported[-1] == 0xF7, "export F7")
        again = load_virus_ti(exported)
        check(len(again) >= 1, f"re-import want ≥1, got {len(again)}")
        if again:
            check(again[0].name == rows[0].name, f"round-trip name {again[0].name!r} != {rows[0].name!r}")
            check(again[0].engine == "ANALOG", "round-trip engine")
            check(len(again[0].converted) == 24, "round-trip converted size")
            check(again[0].original == exported, "round-trip original == exported SysEx")
            # Values that survive AnalogToVirusPages → VirusPagesToAnalog
            a0 = analog_blob_unpack(rows[0].converted)
            a1 = analog_blob_unpack(again[0].converted)
            assert a0 is not None and a1 is not None
            page_rt = analog_to_virus_pages(a0, rows[0].name, rows[0].mono, rows[0].glide)
            expect, _, _, _ = virus_pages_to_analog(bytes(page_rt))
            check(a1 == expect, f"round-trip ANALOG blob {a1} != {expect}")
            check(again[0].mono == rows[0].mono, "round-trip mono")
            check(again[0].glide == rows[0].glide, "round-trip glide")

    # Short Access path (LoadVirusTi demo fallback)
    short = make_short_access_demo()
    short_rows = load_virus_ti(short)
    check(len(short_rows) == 1, f"short path want 1 row, got {len(short_rows)}")
    if short_rows:
        check(short_rows[0].engine == "ANALOG", "short path engine")
        check(bool(short_rows[0].name), "short path name non-empty")
        check(len(short_rows[0].converted) == 24, "short path converted size 24")
        check(short_rows[0].original == short, "short path original preserved")
        check(short_rows[0].name == "Virus TI Import", f"short name, got {short_rows[0].name!r}")

    if fails:
        print(f"check_virus_bay FAIL ({src}):", file=sys.stderr)
        for f in fails:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print(
        f"check_virus_bay ok ({src}): {len(rows)} ANALOG row(s), blob=24, "
        "original preserved, AnalogToVirusPages round-trip, short path ok"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
