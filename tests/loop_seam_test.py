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

tools/autosample.py bake makes the wrap seamless: the loop is a whole number of the note's periods
(loop_points), so x[le] -> x[ls] is the waveform's own next sample, and only a 4 ms micro-fade is
left to bury the ADPCM step. The zones that could not be period-locked (the top of the piano) keep
the 45 ms tail fade instead, which blends the material just before the loop start into the tail. The
limit below is loose enough for the CC0 library's loops (FLUTE is near 1.8, its waveform is bright
enough that a plain edge already places high in the percentile) and tight enough to catch a 3-10x
jump.

The second half of this test is the loop's *level*: a repeated window repeats its envelope too, so
any level shape left in it is a tremolo at the pass rate (the "wobble" the flattened sets had, worst
on the low notes where the pass rate is lowest). level_flatten divides that envelope out; the limit
is what is left of it.

  tests/loop_seam_test.py [HEADER]
"""
import math
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
    bad += wobble(zones, data, set_of)
    return 1 if bad else 0


WOB_LIMIT = 1.5               # dB of level modulation at the loop's own pass rate


def wobble(zones, data, set_of):
    """AM at the pass rate (1 / loop length) of each looped zone, on the decoded audio"""
    worst, bad = [], 0
    for zi, (off, n, ls, le, rate, root, pred, idx, lo, hi, looped) in enumerate(zones):
        if not looped or le <= ls + 64:
            continue
        p = i = 0
        out = []
        for pos in range(le + 1):                 # (the seam section already decoded this; cheap enough)
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
            out.append(p)
        seg = out[ls:le + 1]
        t = len(seg)
        w, h = max(2, int(0.02 * 22050)), max(1, int(0.005 * 22050))
        env = []
        for c in range(0, t - w, h):         # one pass, wrapped over its own end
            v = sum(seg[(c + k) % t] ** 2 for k in range(w))
            env.append(10.0 * math.log10(max(v, 1.0)))
        m = sum(env) / len(env)
        env = [v - m for v in env]
        k = len(env)
        mt = (k - 1) / 2.0
        stt = sum((j - mt) ** 2 for j in range(k)) or 1
        slope = sum((j - mt) * env[j] for j in range(k)) / stt
        env = [env[j] - slope * (j - mt) for j in range(k)]
        wf = 2 * math.pi * (len(seg) / len(env)) / len(seg)   # one cycle per pass
        re = sum(env[j] * math.cos(wf * j) for j in range(k))
        im = sum(env[j] * math.sin(wf * j) for j in range(k))
        amp = 2 * math.sqrt(re * re + im * im) / k
        worst.append((amp, zi, set_of(zi)))
        bad += amp > WOB_LIMIT
    worst.sort(reverse=True)
    for amp, zi, nm in worst[:5]:
        print(f"loop_level: {nm} zone {zi}: {amp:.2f} dB at the pass rate")
    print(f"loop_level: {len(worst)} loops checked, worst {worst[0][0]:.2f} dB (limit {WOB_LIMIT})")
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/gen/skm_samples.h"))
