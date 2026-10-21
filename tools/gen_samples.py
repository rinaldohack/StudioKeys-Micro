#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
"""Build the SAMPLER engine's sample sets from WAV files into a C header.

Samples are stored as IMA ADPCM (4 bit) at their native rate; loop points
and roots come from the WAV 'smpl' chunk and the file name (C3 = MIDI 60
naming), single-cycle waves get their root from rate / loop length.
The ADPCM state at the loop start is stored so loops restart exactly.

Libraries:
  own        build/baked/<SET>/ (tools/autosample.py bake, from assets/samples-own/<SET>/):
             STRINGS, HORN. The bake loops the stationary tail of each take and leaves
             SMP_DECAY[knee, m1, m2, b1, b2] per zone so the firmware puts a decay back at play
             time (eng_sample.c sample_render); the ensembles are sustained, so theirs is 0.
  cc0        assets/samples-cc0/ (tools/fetch_cc0.py, Versilian Studios, CC0):
             FLUTE, SAX (set 0 once PIANO, 1 once TRANH, 4 once PERC: all aliases of STRINGS)
SAMPLE's factory presets are the non-alias sets (SMP_NPRESETS); every sample set and SET / USR
index stays in its original place.
A retired set keeps its index as an alias: the original's name and zones (no data), and a
preset equal to the original's (SMP_SET_ORIG), so old projects and presets play it and
browsing skips it (params.c enum_orig, ui.c preset_orig).
Set 4 was PERC, the GM-mapped drum kit (StudioKEYS's generated drums and CC0 hand percussion,
~66 KB of flash): retired, the DRUM engine plays the same GM map. A sound that selected it
loads as DRUM with its default kit (core.h drum_from_perc); what that cannot reach (GRAIN's
SRC, a value sent live) plays the alias, STRINGS.

The PIANO set (16 zones, 70 KB, 4-bit) is gone: the same flash bought the ensembles twice the
length instead, and a 4-bit piano was the weakest thing in the box. Set 0 and SLICE's SRC 0 now
play STRINGS, so a stored sound that named the piano plays the strings.

The SLICE engine's built-in BREAK (eng_slice.c) is gone (its one bar of 16ths cost 22 KB of flash the
sampled sets needed), along with the drum-kit builder that arranged it. SRC 0 is a SAMPLE set's
material now (it was 4 in 1.0.4), so a stored SRC 0 plays it; no data follows the sets.

SLICE's SRC 0 is SLC_SRC_SET's zone nearest middle C: no data of its own, only its slice table
(one AUTO slice, the note's attack): SLC_SRC_INIT, its rate folded so the material's own root
plays at C4. Without the CC0 library it has no material (SLICE plays a sine there).

The header is cached (build/gen_samples.cache) under a hash of every
input file, this script, sampleio.py and the Python version, so unchanged
inputs skip the slow pitch detection.
"""
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
from sampleio import detect_hz, hz_to_midi, ima_encode, key_split, onset, peak, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
GENDIR = SRC / "build" / "genwav"
CACHE = SRC / "build" / "gen_samples.cache"
CC0 = SRC / "assets" / "samples-cc0"
OWN = SRC / "assets" / "samples-own"            # the takes, as recorded (autosample.py)
BAKED = SRC / "build" / "baked"                 # the flattened zones the bake writes (per set)
TR = 22050                                   # stored sample rate
BLK = 32                                     # eng_sample.c: one control block, CTL samples
FS = 48000                                   # the device's output rate (the decay table's time base)
Q15 = 1 << 15                                    # the decay table's unity, Q15 per control block

# CC0 library: set -> kind ("oneshot" decaying, "sus" looped sustain, "kit" one sample per key)
# Built-in sets (strings, horn, flute, sax); the other slots of the 8 are for the user (USR1-3, loaded
# from the editor). The set indices are append-only -- a stored sound keeps the SET it was saved
# with -- so the retired ones stay as aliases ("alias": the set named, no data): 1 was TRANH, and
# 4 was PERC (PERC_SLOT; its sounds load as the DRUM engine). 0 was PIANO, the 16-zone 4-bit
# piano: removed (70 KB of flash, and the flash buys the two sets a player holds notes on instead),
# so a stored 0 and SLICE's SRC 0 now play STRINGS.
CC0_SETS = [("STRINGS", "alias"), ("STRINGS", "alias"), ("FLUTE", "sus"), ("SAX", "sus"),
            ("HORN", "alias"), ("STRINGS", "own"), ("HORN", "own")]
PERC_SLOT = 4                                # core.h SMP_SET_PERC: SAMPLE's factory presets once ended before it
MEASURED_TUNING = ()                         # sets whose recordings are not at A440 (was TRANH, ~+35 ct)

KIT_BASE = 53                     # F3, the lowest FM-1 key

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

# SLICE's BREAK and the drum-kit builder that fed it are gone (22 KB of flash the sampled sets
# needed). SRC 0 stays: a stored SLICE SRC 0 plays a SAMPLE set's material (STRINGS since PIANO left).
SLC_SRC_SET = "STRINGS"                      # SLICE's SRC 0: this set's zone nearest SLC_SRC_NOTE
SLC_SRC_NOTE = 60                            # ... and the note the material is played at (middle C)
SLC_GRID, SLC_AUTO = 128, 32                # eng_slice.c slc_src_t

ENV = {"wave":(5, 80, 100, 50), "kit": (0, 127, 127, 60), "multi": (0, 85, 0, 75),
       "oneshot": (0, 127, 127, 70), "sus": (12, 80, 120, 60),
       "str": (22, 90, 120, 75), "hn": (18, 90, 120, 72)}
# the reverb send of a factory preset (P_E5 is the zone's... no: FX(dist, chorus, delay, reverb)):
# load-bearing for the flattened sets, whose own tail no longer carries the room
FX_SEND = {"STRINGS": 45, "HORN": 40}
OWN_ENV = {"STRINGS": "str", "HORN": "hn"}

SET_KEEP = {}
SET_FADE = {}

_wavs = {}


def cos_fade(x, fade):
    """fade the last `fade` samples of x out on a raised cosine (no corner at either end)"""
    n = len(x)
    for i in range(fade):
        x[n - fade + i] *= 0.5 + 0.5 * math.cos(math.pi * (i + 1) / fade)


def wav(path):
    """read_any_wav, once per file and run (the CC0 pass reads every file twice)"""
    if path not in _wavs:
        _wavs[path] = read_any_wav(path)
    return _wavs[path]


def name_note(name):
    """'_C#3' in a CC0 file name -> (octave, pitch class)"""
    m = re.search(r"_([A-G]#?)(-?\d)", name)
    return (int(m.group(2)), NOTE[m.group(1)]) if m else None


def cc0_entries(setname, kind):
    """-> [(file name, int16 samples at TR, loop or None, root)]"""
    files = sorted((CC0 / setname).glob("*.wav"))
    out = []
    if kind != "kit":
        # octave convention of this set (C4 = 60 or C3 = 60): YIN votes, the names give the notes
        votes = {1: 0, 2: 0}
        for p in files:
            nn = name_note(p.name)
            if not nn:
                continue
            sr, x = wav(p)
            det = hz_to_midi(detect_hz(x[onset(x):], sr))
            for o in (1, 2):
                if abs(det - ((nn[0] + o) * 12 + nn[1])) < 1.0:
                    votes[o] += 1
        conv = 2 if votes[2] >= votes[1] else 1
    for k, p in enumerate(files):
        sr, x = wav(p)
        x = x[max(0, onset(x) - 16):]
        nn = name_note(p.name)
        if kind == "kit":
            root = KIT_BASE + k
        elif nn:
            root = (nn[0] + conv) * 12 + nn[1]
            if setname in MEASURED_TUNING:              # tuned off A440: the measured pitch near the named note
                det = hz_to_midi(detect_hz(x, sr))
                if abs(det - root) < 1.0:
                    root = det
        else:
            root = hz_to_midi(detect_hz(x, sr))
        x = resample(x, sr, TR)
        keep = SET_KEEP.get(setname) or {"oneshot": 1.0, "sus": 0.95, "kit": 0.6}[kind]
        x = x[:int(keep * TR)]
        n = len(x)
        if kind == "sus":                            # crossfaded sustain loop in the steady part
            ls, le, xf = int(0.40 * TR), n - 1, int(0.06 * TR)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        else:
            if setname in SET_FADE:                  # a shortened set: a long raised-cosine fade-out
                cos_fade(x, int(SET_FADE[setname] * TR))
            else:
                fade = int(0.08 * TR)
                for i in range(fade):
                    x[n - fade + i] *= 1 - i / fade
            loop = None
        pk = peak(x)
        out.append((p.name, [int(v * 30000 / pk) for v in x], loop, root))
    return out


def own_meta(setname):
    """build/baked/<SET>/bake.json: the zones the bake wrote (file, root, loop, knee, dB/s)"""
    return json.loads((BAKED / setname / "bake.json").read_text())


def block_q15(db_s, k=1.0):
    """a dB/s slope as the firmware's per-control-block multiplier (Q15) for eng_sample.c: the
    stored zone has the slope divided out, so the voice multiplies by 10^(db_s k BLK / (20 FS))
    every CTL samples. k < 1 makes the slope the low-pass cutoff follows (tone darkens as it decays).
    0 = no decay (the voice multiplies by 1 and skips the work)"""
    if not db_s:
        return 0
    m = int(round(10 ** (db_s * k * BLK / (20.0 * FS)) * Q15))
    return max(1, min(Q15 - 1, m))


def bake_all(log=print):
    """(re)run tools/autosample.py bake for every own set whose takes are newer than its bake
    (bake.json's mtime is the stamp: the bake writes it last)"""
    tool = Path(__file__).with_name("autosample.py")
    for name in dict.fromkeys(n for n, k in CC0_SETS if k == "own"):
        src = sorted((OWN / name).glob("*.wav"))
        out = BAKED / name / "bake.json"
        if not src:
            log(f"samples: {name}: no takes in {OWN / name} - skipped")
            continue
        newest = max(p.stat().st_mtime for p in src)
        if out.exists() and out.stat().st_mtime >= max(newest, tool.stat().st_mtime):
            continue
        log(f"samples: {name}: baking {len(src)} takes")
        subprocess.run([sys.executable, str(tool), "bake", "--set", name], check=True)


def ima_states(data, positions):
    """IMA ADPCM decoder state before sample p for each p (ascending), packed as eng_slice.c reads it:
    (predictor & 0xFFFF) | index << 16 (segment 0)"""
    pred, idx, out, want = 0, 0, [], list(positions)
    for n in range(max(want) + 1 if want else 0):
        while want and want[0] == n:
            out.append((pred & 0xFFFF) | idx << 16)
            want.pop(0)
        code = (data[n >> 1] >> (4 * (n & 1))) & 15
        step = sio.IMA_STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
        idx = max(0, min(88, idx + sio.IMA_IDX[code & 7]))
    return out


class Builder:
    def __init__(self):
        self.zones, self.blob, self.kinds = [], bytearray(), {}
        self.sets = [None] * len(CC0_SETS)       # by set index: (name, z0, nz), aliases filled after
        self.alias = {}                          # set index -> the set index it aliases
        self.slc = None

    def slice_material(self):
        """SLICE's SRC 0: SLC_SRC_SET's zone nearest SLC_SRC_NOTE (its data, no copy); its slice table:
        decoder states at k * len / SLC_GRID, one AUTO slice at 0 (a single note has one attack).
        The material's own root is folded into the table's rate, so playing SLC_SRC_NOTE plays it at
        that pitch (the slicer has no root of its own: note 60 is unity varispeed)."""
        zi = next((i for i, s in enumerate(self.sets) if s and s[0] == SLC_SRC_SET), None)
        if zi is None:
            return
        _, z0, nz = self.sets[zi]
        zs = self.zones[z0:z0 + nz]
        z = min(zs, key=lambda q: abs(q["root16"] - SLC_SRC_NOTE * 16))
        off, n, root = z["off"], z["n"], z["root16"] / 16.0
        data = self.blob[off:off + (n + 1) // 2]
        grid = ima_states(data, [k * n // SLC_GRID for k in range(SLC_GRID)])
        ratio = 2.0 ** ((SLC_SRC_NOTE - root) / 12.0)     # slower = lower, so the material's pitch lands on C4
        rate = int(round(TR / 44100 * ratio * 65536))
        self.slc = dict(off=off, n=n, grid=grid, apos=[0], ast=ima_states(data, [0]), rate=rate,
                        root=root)

    def add(self, s, loop_start):
        """ADPCM-encode s into the blob (each sample starts on an even offset) -> (offset, state at loop_start)"""
        enc, st = ima_encode(s, loop_start)
        off = len(self.blob)
        self.blob += enc
        if len(self.blob) & 1:
            self.blob.append(0)
        return off, st

    def add_set(self, idx, name, kind, entries):
        """entries: zone dicts with root16 (and key for a kit); assigns the key ranges"""
        if not entries:
            return
        if kind == "kit":
            for e in entries:
                e["lo"] = e["hi"] = e["key"]
        else:
            entries.sort(key=lambda e: e["root16"])
            for e, (lo, hi) in zip(entries, key_split([e["root16"] // 16 for e in entries])):
                e["lo"], e["hi"] = lo, hi
        assert self.sets[idx] is None, f"set {idx} ({name}) built twice"
        self.sets[idx] = (name, len(self.zones), len(entries))
        self.zones += entries
        self.kinds[name] = kind

    def alias_set(self, idx, name):
        """a retired set's index: the zones of the set named `name` (no data of its own). Resolved
        after every real set is built, so an alias may sit before its original (set 0 was PIANO,
        now STRINGS)."""
        orig = next(i for i, s in enumerate(self.sets)
                    if s and s[0] == name and i not in self.alias)
        self.alias[idx] = orig
        self.sets[idx] = self.sets[orig]

    def cc0_set(self, idx, name, kind):
        entries = []
        for k, (_, s, loop, root) in enumerate(cc0_entries(name, kind)):
            ls, le = loop if loop else (len(s), len(s))
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=bool(loop), sr=TR,
                                root16=int(round(root * 16)), pred=st[0], idx=st[1],
                                key=KIT_BASE + k if kind == "kit" else None))
        self.add_set(idx, name, kind, entries)

    def own_set(self, idx, name):
        """a baked set (tools/autosample.py bake): the zones as the bake left them, each looped, with
        the decay it divided out of the tail -> SMP_DECAY"""
        meta, entries = own_meta(name), []
        for z in meta["zones"]:
            sr, s, loop = sio.read_wav(BAKED / name / z["file"])
            ls, le = loop
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=True, sr=sr,
                                root16=int(round(z["root"] * 16)), pred=st[0], idx=st[1], key=None,
                                dec=dict(knee=z["knee"], m1=block_q15(z["db1"]), m2=block_q15(z["db2"]),
                                         b1=block_q15(z["db1"], 0.30), b2=block_q15(z["db2"], 0.30))))
        self.add_set(idx, name, OWN_ENV.get(name, "sus"), entries)

    def header(self):
        zones, sets, blob = self.zones, self.sets, self.blob
        L = ["/* generated by tools/gen_samples.py: IMA ADPCM sample sets */", "#pragma once",
             "#include <stdint.h>", ""]
        L.append(f"static const uint8_t SMP_DATA[{max(1, len(blob))}] = {{")
        for i in range(0, len(blob), 32):
            L.append("    " + ",".join(map(str, blob[i:i + 32])) + ",")
        if not blob:
            L.append("    0,")
        L.append("};")
        L.append("static const smp_zone_t SMP_ZONES[] = {")
        for e in zones:
            rate = int(round(e["sr"] / 44100 * 65536))
            L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, "
                     f"{e['idx']}, {e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
        if not zones:
            L.append("    {0, 0, 0, 0, 65536, 960, 0, 0, 0, 127, 0},")
        L.append("};")
        L.append(f"#define SMP_NZONES {max(1, len(zones))}")
        L.append("/* SMP_DECAY[i]: the decay SMP_ZONES[i]'s data had divided out (tools/autosample.py bake),")
        L.append(" * put back per voice by eng_sample.c: from sample `knee` the gain follows m1 (Q15 per control")
        L.append(" * block) until the loop (ls), m2 after it; b1/b2 are the same slopes for the low-pass, so a")
        L.append(" * flattened note darkens as it decays. 0 = no decay (every non-baked zone: multiply by 1) */")
        L.append("static const smp_decay_t SMP_DECAY[] = {")
        for e in zones:
            d = e.get("dec")
            if d and not (d["m1"] or d["m2"]):
                d = None                        # a sustained zone: the bake kept its slope 0, nothing to put back
            L.append("    {%d, %d, %d, %d, %d}," % ((d["knee"], d["m1"], d["m2"], d["b1"], d["b2"]) if d
                                                   else (0, 0, 0, 0, 0)))
        if not zones:
            L.append("    {0, 0, 0, 0, 0},")
        L.append("};")
        L.append("static const smp_set_t SMP_SETS[] = {")
        for name, z0, nz in sets:
            L.append(f'    {{"{name}", {z0}, {nz}}},')
        if not sets:
            L.append('    {"NONE", 0, 1},')
        L.append("};")
        L.append(f"#define SMP_NSETS {max(1, len(sets))}")
        named = sets or [("NONE", 0, 0)]
        # every set is a factory preset; the retired aliases (1: TRANH, 4: PERC) are skipped by name
        # (ui.c preset_rank). 4 keeps loading as DRUM (core.h SMP_SET_PERC), not as SAMPLE
        perc = len(named)
        L.append(f"#define SMP_NPRESETS {perc}")
        if len(named) > PERC_SLOT:
            L.append(f"#define SMP_PERC_SLOT {PERC_SLOT}   /* once PERC (the GM kit): an alias, its sounds load as DRUM */")
        L.append("/* SMP_PRESET_TABLE[i] is set i; an alias entry repeats the original's preset (hidden by name) */")
        L.append("static const preset_t SMP_PRESET_TABLE[] = {")
        for i, (name, _, _) in enumerate(named):
            k = self.kinds.get(name, "wave")
            a, d, s_, r = ENV[k]
            loop = 0 if k == "kit" else 1
            pat = ", PAT(12)" if k == "kit" else ""     # a kit: engines.c PATTERNS[11] BEAT
            si = self.alias.get(i, i)                   # an alias: the original's preset (hidden by its name)
            fx = f", FX(0, 0, 0, {FX_SEND[name]})" if name in FX_SEND else ""   # a flattened tail has no room of its own
            L.append(f'    {{"{name}", {{{si}, 0, 0, {loop}, 127, 0, 0, 0}}, {{{a}, {d}, {s_}, {r}}}, 0, 0'
                     f'{fx}{pat}}},')
        L.append("};")
        orig = [self.alias.get(i, i) for i in range(len(named))]
        L.append("/* SET i plays SMP_SET_ORIG[i]: != i for a retired set kept as an alias (its preset i too) */")
        L.append("static const uint8_t SMP_SET_ORIG[] = {" + ", ".join(map(str, orig)) + "};")
        L.append(f"#define SMP_NALIAS {sum(1 for i, o in enumerate(orig) if o != i and i < perc)}   /* among the presets */")
        names = ", ".join(f'"{n}"' for n, _, _ in named)
        L.append("#define SMP_SET_NAMES_INIT " + names)
        L.append("static const char *const SMP_SET_NAMES[] = {" + names + "};")
        L += self.slc_header()
        return "\n".join(L) + "\n"

    @staticmethod
    def slc_init(name, b, rate=None):
        """an eng_slice.c slc_src_t initializer (len, rate, nseg, nauto, seg[], grid[], apos[], ast[])"""
        rate = int(round(TR / 44100 * 65536)) if rate is None else rate

        def lst(v, k):
            v = list(v) + [0] * (k - len(v))
            return ", \\\n    ".join(", ".join(map(str, v[i:i + 12])) for i in range(0, len(v), 12))
        return [f"#define {name} {{{b['n']}, {rate}, 1, {len(b['apos'])}, {{{{{b['off']}, 0, {b['n']}}}}}, {{ \\",
                "    " + lst(b["grid"], SLC_GRID) + "}, { \\", "    " + lst(b["apos"], SLC_AUTO) + "}, { \\",
                "    " + lst(b["ast"], SLC_AUTO) + "}}"]

    def slc_header(self):
        """SLC_SRC_INIT: SLICE's SRC 0, a SAMPLE set's zone ({0}: none, no CC0 library)"""
        b = self.slc
        if not b:
            return ["#define SLC_SRC_INIT {0}"]
        return ["", f"/* SLICE's SRC 0: the {SLC_SRC_SET} zone nearest MIDI {SLC_SRC_NOTE} (root {b['root']:g}, "
                    f"{b['n']} samples at SMP_DATA[{b['off']}], rate folded so it plays at C4) */",
                f"#define SLC_SRC_NOTE {SLC_SRC_NOTE}"] + self.slc_init("SLC_SRC_INIT", b, b["rate"])

    def summary(self):
        brk = f", SLICE SRC 0 (shared) {self.slc['n']} samples" if self.slc else ""
        dec = sum(1 for z in self.zones if z.get("dec") and (z["dec"]["m1"] or z["dec"]["m2"]))
        if dec:
            brk += f", {dec} zones decay-flattened"
        return f"samples: {len(self.sets)} sets, {len(self.zones)} zones, {len(self.blob)} B ADPCM{brk}"


def slice_on():
    """the SLICE engine is built (src/core.h: on unless SKM_SLICE=0)"""
    return os.environ.get("SKM_SLICE", "1") != "0"


def input_key(have_cc0):
    """hash of everything the header depends on"""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for p in (here / "gen_samples.py", here / "sampleio.py"):
        h.update(p.read_bytes())
    h.update(repr((sys.version_info[:2], have_cc0, slice_on())).encode())   # sum() differs across versions
    files = sorted(GENDIR.glob("*.wav"))
    if have_cc0:
        files += sorted(CC0.glob("*/*.wav"))
    files += sorted(BAKED.glob("*/*.wav")) + sorted(BAKED.glob("*/bake.json"))
    for p in files:
        h.update(str(p.relative_to(SRC)).encode() + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def main(out):
    subprocess.run([sys.executable, str(Path(__file__).with_name("gen_waves.py")), str(GENDIR)], check=True)
    have_cc0 = CC0.exists() and any(CC0.glob("*/*.wav"))
    if not have_cc0:
        print("samples: CC0 library not fetched (tools/fetch_cc0.py) - no SAMPLE sets")
    if have_cc0:
        bake_all()
    key = input_key(have_cc0)
    try:
        ck, summary, text = CACHE.read_text().split("\n", 2)
        if ck == key:
            Path(out).write_text(text)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    b = Builder()
    if have_cc0:
        for i, (name, kind) in enumerate(CC0_SETS):  # the real sets first, at their own indices
            if kind == "alias":
                continue
            if kind == "own":
                b.own_set(i, name)
            else:
                b.cc0_set(i, name, kind)
        for i, (name, kind) in enumerate(CC0_SETS):  # then every retired index, as an alias by name
            if kind == "alias":
                b.alias_set(i, name)
    if slice_on():                                  # SLICE's SRC 0: the zone itself, its slice table
        b.slice_material()                          # (no data of its own: the STRINGS set's)
    text = b.header()
    Path(out).write_text(text)
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(f"{key}\n{b.summary()}\n{text}")
    print(b.summary())


if __name__ == "__main__":
    main(sys.argv[1])
