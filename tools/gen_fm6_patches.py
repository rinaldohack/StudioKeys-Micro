#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
"""FM6 factory patches (src/eng_fm6.c) -> a C header of packed 128-byte voices.

  tools/gen_fm6_patches.py OUT.h

StudioKEYS's own patches, made for it (no factory ROM data of any instrument): written here as readable
operator settings, packed in the generic 128-byte 6-operator voice layout (the 32-voice bank's record).
Operators are listed as OP1..OP6; the packed record starts with OP6, as the format does.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm6_cartridge import BANK_DATA


def op(r=(99, 99, 99, 99), l=(99, 99, 99, 0), ol=0, fc=1, ff=0, det=7, mode=0, kvs=0, ams=0, rs=0,
       bp=39, ld=0, rd=0, lc=0, rc=0):
    return dict(r=r, l=l, ol=ol, fc=fc, ff=ff, det=det, mode=mode, kvs=kvs, ams=ams, rs=rs, bp=bp, ld=ld, rd=rd,
                lc=lc, rc=rc)


def voice(name, alg, ops, fb=0, oks=1, pr=(99, 99, 99, 99), pl=(50, 50, 50, 50), lfs=35, lfd=0, lpmd=0, lamd=0,
          lks=1, lfw=0, lpms=3, trnsp=24):
    """the 155-byte single-voice layout (OP6 first); alg 1..32"""
    assert len(ops) == 6 and 1 <= alg <= 32 and len(name) <= 10
    v = []
    for o in reversed(ops):                        # OP6 .. OP1
        v += list(o["r"]) + list(o["l"]) + [o["bp"], o["ld"], o["rd"], o["lc"], o["rc"], o["rs"], o["ams"],
                                            o["kvs"], o["ol"], o["mode"], o["fc"], o["ff"], o["det"]]
    v += list(pr) + list(pl) + [alg - 1, fb, oks, lfs, lfd, lpmd, lamd, lks, lfw, lpms, trnsp]
    v += [ord(c) for c in name.ljust(10)]
    assert len(v) == 155
    return v


def pack(v):
    """155 -> the 128-byte record (the C side: eng_fm6.c fm6_pack)"""
    b = []
    for k in range(6):
        o = v[k * 21:k * 21 + 21]
        b += o[0:11]
        b += [(o[11] & 3) | (o[12] & 3) << 2, (o[13] & 7) | (o[20] & 15) << 3, (o[14] & 3) | (o[15] & 7) << 2,
              o[16], (o[17] & 1) | (o[18] & 31) << 1, o[19]]
    b += v[126:135]
    b += [(v[135] & 7) | (v[136] & 1) << 3]
    b += v[137:141]
    b += [(v[141] & 1) | (v[142] & 7) << 1 | (v[143] & 7) << 4, v[144]]
    b += v[145:155]
    assert len(b) == 128 and all(0 <= x < 128 for x in b)
    return b


INIT = voice("INIT VOICE", 1, [op(ol=99)] + [op() for _ in range(5)], lpms=3)

# OP1..OP6. Algorithm 5 = three pairs (1<-2, 3<-4, 5<-6 with the feedback), 32 = six carriers.
PATCHES = [
    ("TINE EP", voice("TINE EP", 5, [
        op(r=(96, 25, 25, 67), l=(99, 75, 0, 0), ol=98, kvs=2, rs=3),
        op(r=(95, 50, 35, 78), l=(99, 75, 0, 0), ol=58, kvs=7, rs=3),
        op(r=(95, 20, 20, 50), l=(99, 95, 0, 0), ol=90, kvs=2, det=8, rs=2),
        op(r=(97, 62, 40, 60), l=(99, 60, 0, 0), ol=68, fc=14, kvs=6, rs=3),
        op(r=(95, 30, 20, 60), l=(99, 90, 0, 0), ol=78, det=6, kvs=1),
        op(r=(95, 40, 30, 60), l=(99, 80, 0, 0), ol=52, kvs=3)],
        fb=3, lfs=34, lfd=33, lfw=4, lpms=2)),
    ("BELL", voice("GLASS BELL", 5, [
        op(r=(99, 40, 25, 35), l=(99, 80, 0, 0), ol=96, kvs=1),
        op(r=(99, 35, 25, 35), l=(99, 70, 0, 0), ol=78, fc=3, ff=50, kvs=3),
        op(r=(99, 45, 30, 40), l=(99, 70, 0, 0), ol=84, fc=2, ff=38, det=9, kvs=1),
        op(r=(99, 50, 30, 40), l=(99, 60, 0, 0), ol=70, fc=5, kvs=3),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=78, fc=5, ff=40, det=5),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=60)],
        fb=0, lpms=0)),
    ("FM BASS", voice("ROUND BASS", 5, [
        op(r=(99, 40, 30, 75), l=(99, 85, 70, 0), ol=99, kvs=2),
        op(r=(99, 60, 40, 75), l=(99, 72, 55, 0), ol=80, kvs=5),
        op(r=(99, 55, 40, 75), l=(99, 70, 50, 0), ol=80, det=8, kvs=1),
        op(r=(99, 72, 50, 75), l=(99, 65, 40, 0), ol=78, fc=2, kvs=6),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    ("BRASS", voice("BRASS SECT", 5, [
        op(r=(65, 50, 40, 60), l=(99, 92, 90, 0), ol=98, kvs=1),
        op(r=(55, 50, 40, 60), l=(99, 88, 85, 0), ol=80, kvs=3),
        op(r=(62, 50, 40, 60), l=(99, 92, 90, 0), ol=92, det=9, kvs=1),
        op(r=(50, 50, 40, 60), l=(99, 88, 85, 0), ol=78, kvs=3),
        op(r=(62, 50, 40, 60), l=(99, 92, 90, 0), ol=88, det=5, kvs=1),
        op(r=(52, 50, 40, 60), l=(99, 85, 82, 0), ol=72, kvs=2)],
        fb=5, pr=(80, 60, 99, 60), pl=(46, 50, 50, 50), lfs=33, lfd=50, lpmd=6, lfw=4, lpms=3)),
    ("PAD", voice("SOFT PAD", 5, [
        op(r=(40, 30, 40, 40), l=(99, 95, 90, 0), ol=92, det=5),
        op(r=(35, 30, 40, 40), l=(85, 90, 80, 0), ol=60, kvs=1),
        op(r=(40, 30, 40, 40), l=(99, 95, 90, 0), ol=92, det=10),
        op(r=(30, 25, 40, 40), l=(80, 90, 85, 0), ol=55, fc=2, kvs=1),
        op(r=(38, 30, 40, 40), l=(99, 95, 90, 0), ol=80, fc=2),
        op(r=(35, 30, 40, 40), l=(85, 90, 85, 0), ol=50)],
        fb=2, lfs=30, lfd=60, lpmd=4, lfw=4, lpms=2)),
    ("MARIMBA", voice("WOOD BARS", 5, [
        op(r=(99, 52, 40, 55), l=(99, 0, 0, 0), ol=99, kvs=2, rs=2),
        op(r=(99, 75, 50, 60), l=(99, 0, 0, 0), ol=70, fc=4, kvs=5, rs=3),
        op(r=(99, 70, 50, 60), l=(99, 0, 0, 0), ol=72, fc=4, kvs=3, rs=3),
        op(r=(99, 80, 50, 60), l=(99, 0, 0, 0), ol=45, kvs=3, rs=3),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    ("ORGAN", voice("DRAWBARS", 32, [
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=90),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=84, fc=0),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=86, fc=2),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=76, fc=3),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=78, fc=4),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=72, fc=1, ff=50)],
        fb=0, lfs=60, lpmd=3, lfw=4, lpms=3)),
    ("PLUCK", voice("NYLON PICK", 5, [
        op(r=(99, 45, 30, 60), l=(99, 60, 0, 0), ol=98, kvs=2, rs=2),
        op(r=(99, 70, 40, 60), l=(99, 40, 0, 0), ol=76, fc=3, kvs=4, rs=2),
        op(r=(99, 60, 40, 60), l=(99, 30, 0, 0), ol=74, fc=2, det=8, kvs=2, rs=2),
        op(r=(99, 75, 40, 60), l=(99, 30, 0, 0), ol=68, kvs=3, rs=2),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
]

# RJP Patch (a Dexed 32-voice cartridge, embedded in fm6_cartridge.py: the tree holds no SysEx file).
# Names are the cartridge's, shortened to the 10 the voice holds.
# RJP_SYX_NAMES is the cartridge's order. RJP_ORDER is how they sit in the factory list: keys, organs, basses,
# leads, winds, plucks, pads, chords.
RJP_SYX_NAMES = [
    "TT Lead", "Syn Bells", "Soft Organ", "Hard Organ", "Hard Bell", "Ac Guitar", "Harsh EP", "Soft EP",
    "Organ 2", "Organ 3", "Glass Pad", "Soft Pluck", "Chord Alt", "Warm Pad", "Digi Pad", "Bass 3Osc",
    "SoftChord", "Phased Pad", "Soft Pad", "Pluck Bass", "Chord 2", "TB Lead", "HardChord", "Syn Bass",
    "Muted", "Music Box", "Accordion", "Syn Flute", "Syn Brass", "Woodblock", "Slow 3D", "RnB Lead",
]
RJP_ORDER = [
    "Soft EP", "Harsh EP", "Ac Guitar", "Accordion",
    "Soft Organ", "Organ 2", "Organ 3", "Hard Organ",
    "Bass 3Osc", "Syn Bass", "Pluck Bass",
    "TT Lead", "TB Lead", "RnB Lead",
    "Syn Brass", "Syn Flute",
    "Soft Pluck", "Muted", "Syn Bells", "Hard Bell", "Music Box", "Woodblock",
    "Glass Pad", "Warm Pad", "Soft Pad", "Digi Pad", "Phased Pad", "Slow 3D",
    "SoftChord", "Chord 2", "Chord Alt", "HardChord",
]
# Q8 output gain (256 = the voice as stored). The first eight stay at 256.
# These bring the cartridge's loudest 100 ms (C2/C3/C4, velocity 100, over 6 s) to about 7200,
# the middle of those eight, and keep the peak under the loudest of them (~22000).
RJP_GAIN = {
    "Soft EP": 159, "Harsh EP": 173, "Ac Guitar": 160, "Accordion": 114,
    "Soft Organ": 101, "Organ 2": 77, "Organ 3": 181, "Hard Organ": 171,
    "Bass 3Osc": 129, "Syn Bass": 51, "Pluck Bass": 197,
    "TT Lead": 215, "TB Lead": 188, "RnB Lead": 184,
    "Syn Brass": 146, "Syn Flute": 53,
    "Soft Pluck": 124, "Muted": 134, "Syn Bells": 115, "Hard Bell": 211, "Music Box": 37, "Woodblock": 223,
    "Glass Pad": 93, "Warm Pad": 167, "Soft Pad": 168, "Digi Pad": 151, "Phased Pad": 292, "Slow 3D": 204,
    "SoftChord": 302, "Chord 2": 213, "Chord Alt": 152, "HardChord": 148,
}


def rjp_voices():
    """32 packed records from the embedded cartridge, with the display name written into bytes 118..127."""
    data = BANK_DATA
    if len(data) != 4096:
        raise SystemExit("fm6_cartridge.BANK_DATA is not a 32-voice bank (4096 bytes)")
    if len(RJP_SYX_NAMES) != 32 or len(RJP_ORDER) != 32 or set(RJP_SYX_NAMES) != set(RJP_ORDER):
        raise SystemExit("RJP_SYX_NAMES and RJP_ORDER must be the same 32 voices")
    by_name = {}
    for i, name in enumerate(RJP_SYX_NAMES):
        if len(name) > 10:
            raise SystemExit(f"RJP name {name!r} is longer than 10")
        rec = bytearray(data[i * 128:(i + 1) * 128])
        rec[118:128] = name.encode("ascii").ljust(10)
        if any(x > 127 for x in rec):
            raise SystemExit(f"RJP voice {i} has a byte above 127")
        by_name[name] = bytes(rec)
    return [(name, by_name[name]) for name in RJP_ORDER]


def rjp_style(name):
    """mono, suggested pattern, fx sends. FM6 ignores the track envelope; the sends are a hint."""
    n = name.lower()
    if "bass" in n:
        return 1, 2, (0, 0, 8, 12)
    if "lead" in n or "flute" in n or "brass" in n:
        return 0, 4, (0, 18, 16, 28)
    if "organ" in n:
        return 0, 6, (8, 30, 0, 20)
    if any(w in n for w in ("bell", "pluck", "wood", "box", "guitar", "muted")):
        return 0, 7, (0, 8, 22, 36)
    return 0, 5, (0, 28, 18, 40)


def c_rows(b):
    return ",\n".join("     " + ", ".join(f"{x:3d}" for x in b[i:i + 16]) for i in range(0, len(b), 16))


def factory():
    """(name, packed 128) for StudioKEYS's eight, then the cartridge.

    One voice per sound: the same operator data twice (the name bytes ignored) would duplicate a
    factory slot, so the cartridge is only ever added, never re-imported (a bank whose voices are
    already here stops the build instead of growing the list)."""
    rows = [(name, pack(v)) for name, v in PATCHES]
    rows += rjp_voices()
    seen = {}
    for name, b in rows:
        key = bytes(b[:118])                         # (the name lives in 118..127)
        if key in seen:
            raise SystemExit(f"duplicate factory voice: {name!r} is {seen[key]!r}")
        seen[key] = name
    return rows


def main(path):
    rows = factory()
    n = len(rows)
    base = len(PATCHES)
    L = ["/* generated by tools/gen_fm6_patches.py: FM6 factory patches, packed (128 bytes).",
         " * The first eight are StudioKEYS's. The rest are the RJP cartridge (tools/fm6_cartridge.py,\n"
         " * compiled in: no SysEx file at build or run time). */",
         "#pragma once", "#include <stdint.h>", f"#define FM6_NFACTORY {n}",
         "static const uint8_t FM6_INIT[128] = {", c_rows(pack(INIT)), "};",
         f"static const uint8_t FM6_FACTORY[{n}][128] = {{"]
    for name, b in rows:
        L += [f"    {{/* {name} */", c_rows(b) + "},"]
    L += ["};", "static const char *const FM6_SLOT_NAMES[] = {"]
    L += [f'    "F{i + 1}",' for i in range(n)]
    L += ['    "OWN",', "    0", "};", "#define FM6_EXTRA_PRESETS \\"]
    extra = []
    for i, (name, _) in enumerate(rows[base:]):
        mono, pat, fx = rjp_style(name)
        d, c, dl, r = fx
        cont = " \\" if i + 1 < n - base else ""
        extra.append(
            f'    {{"{name}", {{0, 0, 0, 0, 0, 0, 0, {base + i}}}, {{0, 0, 127, 0}}, 0, {mono}, '
            f"FX({d}, {c}, {dl}, {r}), PAT({pat})}},{cont}"
        )
    L += extra
    gains = ["256"] * base
    for name, _ in rows[base:]:
        g = RJP_GAIN.get(name, 256)
        if not 32 <= g <= 1024:
            raise SystemExit(f"gain for {name} is {g}, want 32..1024")
        gains.append(str(g))
    L += ["static const uint16_t FM6_LOUD[FM6_NFACTORY] = {",
          "    " + ", ".join(gains),
          "};",
          "/* 256 = unity. Cartridge voices are scaled in eng_fm6.c so they sit with the first eight. */"]
    Path(path).write_text("\n".join(L) + "\n")
    print(f"fm6 patches: {n} ({base} + {n - base} cartridge) -> {path}")


def js():
    """the same patches as the reference FM6 factory table (INIT_PK, FACTORY_PK)"""
    rows = factory()
    base = len(PATCHES)
    a = lambda b: "[" + ",".join(str(x) for x in b) + "]"
    print("  const INIT_PK = " + a(pack(INIT)) + ";")
    print("  const FACTORY_PK = [")
    for name, b in rows:
        print("    " + a(b) + ",   /* " + name + " */")
    print("  ];")
    print("  /* FM6 presets after PLUCK */")
    bits = []
    for i, (name, _) in enumerate(rows[base:]):
        mono, pat, _fx = rjp_style(name)
        bits.append(f'P("{name}", [0, 0, 0, 0, 0, 0, 0, {base + i}], [0, 0, 127, 0], {mono}, {pat})')
    print("      presets extra: " + ", ".join(bits))


if __name__ == "__main__":
    js() if sys.argv[1] == "--js" else main(sys.argv[1])
