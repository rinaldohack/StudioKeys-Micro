#!/usr/bin/env python3
"""size_report.py -- where the firmware's flash and RAM actually go.

Reads the linked image (build/studiokeys.elf), not the sources: every number is a
symbol size from `nm -S`, so it is what the linker really emitted. Meant to be run
after a build whenever a budget question comes up:

    tools/size_report.py                 # human report
    tools/size_report.py --cuts          # ... plus the cut catalogue (bytes per candidate)
    tools/size_report.py --elf other.elf # compare two builds

Attribution is by symbol name prefix. That is exact for the data tables (they are
one symbol each) and for functions whose names carry the module (draw_*, ui_*,
ed_*, game_*, *_render). Anything it cannot place honestly lands in "core", which
also holds every string literal and vtable the linker folded into .text -- so
"core" is never a claim that 100 KB of it is dead weight.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP_SLOT = 0x8DFBC                    # tools/fm1pkg_make.py
REGIONS = [                           # firmware/app.ld
    ("RAMTEXT", 0x01C00000, 0x6000),  # flash driver + user preset mirror
    ("RAM", 0x01C08000, 96 * 1024),   # .data/.bss
    ("POOL", 0x01C20000, 0x54000),    # the big zero-init buffers (.pool)
    ("NOINIT", 0x01C7C000, 0x3D50),   # project slots, survives a reset
]

# One rule per subsystem; first match wins. Order matters (ui_input before ui_*).
RULES = [
    ("samples", r"^SMP_DATA$"),
    ("fonts", r"^AF_"),
    ("icons", r"^(AI12|AI16|AI24|ICON_)"),
    ("tables", r"^(SINE|TP|KC_|DV_|SLC_|FM6_)"),
    ("games", r"^(game_|sk_game|sk_gi|tet_|fl_(gap_h|gap_y|reset|flap|step))"),
    ("draw/graph", r"^(draw_|graph|cv_|px_|text_|lg_|aafont|glyph|hc_|icon_)"),
    ("ui_input", r"^ui_input"),
    ("ui", r"^ui_"),
    ("editor", r"^(ed_|edit_)"),
    ("engines", r"^(analog_|drum_|grain_|formant_|trio_|noise_|slice_|wheel_|lofi_|"
                r"phys_|smp_|slice_|eng_|voice_|osc_|flt_|env_)"),
]


def nm(path):
    tool = shutil.which("llvm-nm") or shutil.which("nm")
    if not tool:
        sys.exit("size_report: no nm on PATH")
    out = subprocess.run([tool, "-S", path], capture_output=True, text=True).stdout
    syms = []
    for ln in out.splitlines():
        p = ln.split()
        if len(p) < 4:
            continue
        try:
            addr, size = int(p[0], 16), int(p[1], 16)
        except ValueError:
            continue
        syms.append((addr, size, p[2], p[3]))
    if not syms:
        sys.exit(f"size_report: {path} has no symbols (stripped?)")
    return syms


def xip(syms):
    """Symbols that live in flash (XIP): code, constants, string literals."""
    return [s for s in syms if s[2] in "tT" and s[0] >= 0x02000000]


def size_of(syms, name):
    return sum(s[1] for s in syms if s[3] == name)


def pref(syms, pat):
    return sum(s[1] for s in xip(syms) if re.search(pat, s[3]))


def smp_sets():
    """Bytes per factory sample set, from the generated zone/set tables."""
    path = os.path.join(ROOT, "build", "gen", "skm_samples.h")
    if not os.path.exists(path):
        return {}
    src = open(path, errors="ignore").read()
    zt = re.search(r"SMP_ZONES\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    st = re.search(r"SMP_SETS\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not zt or not st:
        return {}
    zones = []
    for row in re.findall(r"\{([^{}]+)\}", zt.group(1)):
        v = [int(x) for x in row.split(",")]
        zones.append((v[0], v[1]))                     # (offset, samples)
    sets = re.findall(r'\{"(\w+)",\s*(\d+),\s*(\d+)\}', st.group(1))
    out = {}
    for name, z0, nz in sets:
        z0, nz = int(z0), int(nz)
        if nz == 0 or name in out:      # retired sets are aliases of one that is already counted
            continue
        base = zones[z0][0]
        last_off, last_n = zones[z0 + nz - 1]
        out[name] = (last_off + (last_n + 1) // 2) - base
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default=os.path.join(ROOT, "build", "studiokeys.elf"))
    ap.add_argument("--cuts", action="store_true", help="also price the cut catalogue")
    args = ap.parse_args()

    syms = nm(args.elf)
    flash = xip(syms)
    total = sum(s[1] for s in flash)
    img = os.path.join(ROOT, "build", "studiokeys.bin")
    image = os.path.getsize(img) if os.path.exists(img) else total

    print(f"image   {image:>7} B   slot {APP_SLOT:>7} B   free {APP_SLOT - image:>7} B"
          f"  ({(APP_SLOT - image) * 100 // APP_SLOT}%)")

    print("\nFLASH (XIP: functions, constants, literals)")
    buckets = {}
    for s in flash:
        for name, pat in RULES:
            if re.search(pat, s[3]):
                buckets[name] = buckets.get(name, 0) + s[1]
                break
        else:
            buckets["core"] = buckets.get("core", 0) + s[1]
    for k, v in sorted(buckets.items(), key=lambda x: -x[1]):
        print(f"  {v:>7} B  {v * 100.0 / total:5.1f}%  {k}")
    print(f"  {total:>7} B          total")

    print("\nFLASH by source file (functions only, top 14)")
    sizes = {}
    for s in flash:
        sizes[s[3]] = sizes.get(s[3], 0) + s[1]
    per = []
    for f in glob.glob(os.path.join(ROOT, "firmware", "src", "*.c")):
        src = open(f, errors="ignore").read()
        names = set(re.findall(r"^[A-Za-z_][\w \t\*]*?\b([a-z_]\w*)\s*\(", src, re.M))
        n = sum(sizes[k] for k in names if k in sizes)
        per.append((n, os.path.basename(f), len([k for k in names if k in sizes])))
    for n, f, c in sorted(per, reverse=True)[:14]:
        print(f"  {n:>7} B  {c:>3} fn  {f}")

    print("\nRAM (by linker region)")
    for name, origin, length in REGIONS:
        inside = [s for s in syms if origin <= s[0] < origin + length and s[2] in "bBdDtT"]
        hi = max((s[0] + s[1] for s in inside), default=origin)
        used = hi - origin
        print(f"  {name:<8} {used:>7} / {length:>7} B   free {length - used:>6}")
    print("  top objects")
    ram = [s for s in syms if s[2] in "bB" and s[0] >= 0x01C00000]
    for s in sorted(ram, key=lambda x: -x[1])[:14]:
        print(f"    {s[1]:>7} B  {s[0]:08x}  {s[3]}")

    if not args.cuts:
        return

    print("\nCUT CATALOGUE")
    sets = smp_sets()
    rows = []
    rows.append(("C1", "FLUTE + SAX factory sets",
                 sets.get("FLUTE", 0) + sets.get("SAX", 0), "one line: CC0_SETS in tools/gen_samples.py"))
    rows.append(("C2", "font face XS (9 px)", pref(syms, r"^AF_XS") or 0, "drop one rasterised size"))
    rows.append(("C3", "font face L (28 px)", pref(syms, r"^AF_L\b|^AF_L_"), "drop the big patch-name face"))
    rows.append(("C4", "icon atlas 24 px", pref(syms, r"^AI24"), "drop one atlas"))
    rows.append(("C5", "icon atlas 16 px", pref(syms, r"^AI16"), "drop one atlas"))
    rows.append(("C6", "graph strip: draw_graph", size_of(syms, "draw_graph"), "no graph page"))
    rows.append(("C7", "games", pref(syms, RULES[4][1]), "menu rows + companion modal"))
    for cid, label, n, mech in rows:
        print(f"  {cid:<4} {n:>7} B  {label:<42} {mech}")


if __name__ == "__main__":
    main()
