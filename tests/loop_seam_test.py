#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
"""The SAMPLE engine's loop seams, measured on the ADPCM the firmware actually decodes.

A stored zone plays [0..le] once and then repeats [ls..le] for as long as the key is held, so the
wrap is a step from the decoded sample at le to the one at ls. If those two differ by more than the
waveform's own sample-to-sample slope the wrap is a click, once per loop pass (a 0.3 s loop clicks
3 times a second: that is the "ticking" the takes were shipped with). The same holds for the first
pass into the loop (the step from the material before ls into the loop head).

eng_sample.c restores the decoder's predictor and step index at the wrap (zone.pred / zone.idx),
so the check decodes the loop twice with that state put back, which is exactly what the firmware
does. Each seam is scored against the 99.9th percentile of every |x[n] - x[n - 1]| in the decoded
zone: a normal waveform edge scores below 1, a click several times over.

tools/autosample.py bake makes the wrap seamless: it fades the loop's last 45 ms into the material
just *before* the loop start, so x[le] is the take's own sample before the head and x[le] -> x[ls]
is the recording's adjacency. The limit below is loose enough for the CC0 library's loops (FLUTE is
near 1.8, its waveform is bright enough that a plain edge already places high in the percentile)
and tight enough to catch a 3-10x jump.

  tests/loop_seam_test.py [HEADER]
"""
import re
import sys

LIMIT = 2.5                   # seam / p99.9 of the local sample-to-sample slope
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
        449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def block(src, name):
    """the numeric body of one generated array"""
    m = re.search(r"%s\[[^\]]*\] = \{(.*?)\};" % name, src, re.S)
    if not m:
        raise SystemExit(f"loop_seam: {name} not in the header")
    return m.group(1)


def main(path):
    src = open(path).read()
    data = [int(v) for v in block(src, "SMP_DATA").replace("\n", "").split(",") if v.strip()]
    zones = [tuple(int(v) for v in re.findall(r"-?\d+", row))
             for row in re.findall(r"\{([^{}]*)\}", block(src, "SMP_ZONES")) if row.count(",") == 10]
    sets = [(nm, int(z0), int(nz)) for nm, z0, nz in
            re.findall(r'\{"([A-Z0-9_]+)",\s*(\d+),\s*(\d+)\}', block(src, "SMP_SETS"))]

    def set_of(zi):
        for nm, z0, nz in sets:
            if z0 <= zi < z0 + nz:
                return nm
        return "?"

    worst, bad = [], 0
    for zi, (off, n, ls, le, rate, root, pred, idx, lo, hi, looped) in enumerate(zones):
        if not looped or le <= ls or le >= n:
            continue

        def dec(pos, p, i):
            b = data[off + (pos >> 1)]
            code = (b >> 4) if pos & 1 else (b & 15)
            st = STEP[i]
            vd = st >> 3
            if code & 4:
                vd += st
            if code & 2:
                vd += st >> 1
            if code & 1:
                vd += st >> 2
            p = max(-32768, min(32767, p - vd if code & 8 else p + vd))
            i = max(0, min(88, i + IDX[code & 7]))
            return p, i

        p = i = 0
        out = []
        for pos in range(le + 1):                 # the first pass, as the zone is entered
            p, i = dec(pos, p, i)
            out.append(p)
        for _ in range(2):                        # ... then the loop, with the state put back
            p, i = pred, idx
            for pos in range(ls, le + 1):
                p, i = dec(pos, p, i)
                out.append(p)
        d = sorted(abs(out[k] - out[k - 1]) for k in range(1, len(out)))
        floor = max(d[min(len(d) - 1, int(len(d) * 0.999))], 1)
        for k, what in ((le + 1, "wrap"), (ls, "entry")):
            r = abs(out[k] - out[k - 1]) / floor
            worst.append((r, zi, set_of(zi), what))
            bad += r > LIMIT
    worst.sort(reverse=True)
    for r, zi, nm, what in worst[:5]:
        print(f"loop_seam: {nm} zone {zi} {what}: {r:.2f}x the local 99.9 % step")
    n = len(worst)
    print(f"loop_seam: {n} seams ({n // 2} looped zones) checked, worst {worst[0][0]:.2f}x (limit {LIMIT})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/gen/skm_samples.h"))
