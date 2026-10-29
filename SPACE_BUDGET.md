# Space budget — what there is, what can go, what it buys

**Status: measurement only. Nothing in this document is implemented.** Every byte below comes
from the linked image, not from reading sources: `tools/size_report.py` walks `nm -S` on
`build/studiokeys.elf` and prices each symbol. Re-run it after any build:

```sh
tools/size_report.py          # where it goes
tools/size_report.py --cuts   # ... plus the cut catalogue
```

Numbers are the last cross-build in the tree, `build/studiokeys.elf` from 2026-10-22
(`studiokeys.bin` = **580,676 B**), i.e. HEAD after `efbbfeb` ("Samples: drop PIANO, spend the flash
on longer STRINGS and HORN") and `e72e7e8`. **They are not today's tree:** the uncommitted work after
that build (TEST MODE, the wheel calibration, `wheel_cal.c`) is not in any of these numbers, and this
machine cannot produce a new one — `tools/build.py` runs the x86-64 Linux `pi32v2` toolchain through
Docker, and the only container runtime here (`colima`, profile `default`) is in a **Broken** state.
Re-measure before trusting any free-byte figure below.

An earlier revision of this document measured `63e2b4d` (2026-10-20, 492,704 B) and therefore claimed
**88,860 B of free flash**. That was right on the day and is wrong now: the samples commit that
followed spent about 88 KB of it.

---

## 1. The three budgets that bind

| budget | size | used | free | what the gate is |
|---|---|---|---|---|
| App slot (flash) | 581,564 B (`0x8DFBC`) | 580,676 | **888 B (0.2 %)** | `tools/build.py:264`, `tools/fm1pkg_make.py:130` |
| Data store (flash) | 248 KiB (`0xC2000..0x100000`, 253,952 B) | ~200 KiB | **49,152 B** | `firmware/src/storage.c` `_Static_assert`s |
| RAM | 96 KiB | 97,964 | **340 B** | `firmware/app.ld` RAM |
| POOL | 336 KiB (`0x54000`) | 335,228 | 8,836 B — **644 B spendable** | `firmware/app.ld` POOL, `build.py:278` keeps 8,192 |
| RAMTEXT | 24 KiB | 21,404 | 3,172 B | flash driver + preset mirror |
| NOINIT | 15,696 B | 14,540 | 1,156 B | projects, survives reset |

The asymmetry that used to matter here no longer does: **there is no flash room left to argue about.**
888 B is less than one font table. RAM has 340 B and the pool has 644 B it is actually allowed to
spend, so any feature that needs a buffer — even a 4 KB one — has to take it from an existing object.


---

## 2. Where the 580,676 B of flash go

XIP-resident bytes 564,635 (the rest of the image is `.data` initialisers, alignment and the
`0x120` block header):

| B | % | bucket | what it is |
|---|---|---|---|
| 208,380 | 36.9 % | samples | `SMP_DATA`: STRINGS 72,767 · HORN 72,767 · FLUTE 31,422 · SAX 31,422 |
| 141,654 | 25.1 % | core | voices, sequencer, FX, flash/storage, link, host glue, **string literals** |
| 55,582 | 9.8 % | fonts | 4 rasterised faces (XS 9 px, S 12 px, M 15 px, L 28 px) + adv/kern tables |
| 37,448 | 6.6 % | draw/graph | `ui_graph.c`, `draw_graph` (14,976), canvas text blitting |
| 33,052 | 5.9 % | engines | analog, drum, fm6, grain, slice, trio, phys, sample |
| 24,019 | 4.3 % | tables | SINE, TP, KC, DV, SLC, FM6 factory/presets |
| 20,118 | 3.6 % | icons | three AA atlases, 12 / 16 / 24 px |
| 14,848 | 2.6 % | editor | `editor.c` |
| 13,634 | 2.4 % | ui_input | keys, encoders, pads |
| 13,008 | 2.3 % | ui | `ui.c`, `ui_draw` surfaces |
| **2,892** | **0.5 %** | **games** | Flappy + Tetris + shared draw pass |

Samples went from 26.5 % to 36.9 % of the image in one commit: PIANO (41,349) and SLICE's BREAK loop
left, and STRINGS and HORN came in at 72,767 each (7 zones apiece). `SLC_SMP` is now 976 B.

Per source file, functions only (static attribution by symbol name; see the tool's caveat):

```
24058 ui_draw.c     18148 ui_input.c   18078 ui_graph.c   16636 main.c
11832 editor.c       9910 ui.c          7778 eng_drum.c    7074 audio.c
 6892 project.c      4638 gfx.c         4622 eng_fm6.c     4418 eng_analog.c
 4236 eng_grain.c    3556 voice.c
```

Two facts worth internalising:

- **The GUI is 113 KB of the image**: fonts 55,582 + icons 20,118 + draw/graph 37,448. It is the
  single largest thing you own that is not synthesis or samples.
- **The VST's "beautiful GUI" costs 0 B of this.** It is iPlug2 on the Mac. The only thing the
  keyboard ever sees of it is `sk_host_*` calls.


> Buckets are attributed by symbol-name prefix, so `core` is a catch-all: it holds the synth voice
> allocator, sequencer, FX, storage, link and host glue **and every string literal and vtable the
> linker folded into `.text`**. It is not 135 KB of dead weight, and it is not a target.

---

## 3. Where the 97,964 B + 335,228 B of RAM go

| B | region | symbol | owner | purpose |
|---|---|---|---|---|
| 131,072 | POOL | `dly_buf` | `fx.c` | `DLY_LEN` 65,536 int16 = 1.49 s mono delay |
| 59,520 | POOL | `cv_px` | `gfx.c` | 240 × 124 RGB565 draw canvas |
| 51,552 | POOL | `phys_slot` | `eng_phys.c` | PHYS: 4 parts × 3 voices × 4,296 B |
| 32,768 | POOL | `sl_buf` | `slicer.c` | 4 tracks × 4,096 int16 recordings; PERFORM borrows it as a 371 ms loop |
| 28,080 | POOL | `gr_p` | `eng_grain.c` | GRAIN: 4 parts × 3 voices × 2,340 B |
| 18,480 | RAMTEXT | `up_bank` | `upreset.c` | user preset mirror, 6 banks × 3,080 B |
| 14,336 | NOINIT | `proj_slot` | `project.c` | 4 projects × A/B |
| 12,512 | RAM | `chain` | `editor.c` | song chain |
| 9,874 | POOL | `rev_comb` | `fx.c` | ROOM/SPRING comb lines |
| 8,748 | POOL | `upf_hi` | `up_fm6.c` | FM6 patch mirror, 2 banks |

The delay line alone is 39 % of the pool. The canvas is 18 %. Those two decide whether a new
sampled engine can have a buffer at all.

---

## 4. Cut catalogue — flash

| id | item | B saved | mechanism | effort | what breaks |
|---|---|---|---|---|---|
| **C1** | FLUTE + SAX factory sets | **62,844** | one line: `CC0_SETS` in `tools/gen_samples.py` | S | two of the four factory SAMPLE sounds; 3 test files + goldens; `assets/samples-cc0/*` credits |
| C1b | SLICE's BREAK factory loop (2 s, 22.05 kHz, 44,100 samples) | 22,050 | `BREAK_HITS` cleared in the same generator | S | SLICE loses its demo break; SRC PIANO stays free (it shares the PIANO zone) |
| C2 | font face XS (9 px) | 12,409 | drop `AF_XS*` + its 3 kern/adv tables | M | hint lines ("OCT- BACK") reflow into S |
| C3 | font face L (28 px) | 7,135 | drop `AF_L*` | M | patch-name banner and big values shrink to M — 8 call sites in 5 files |
| C4 | icon atlas 24 px | 4,228 | `gen_aa_icons.py` size list | S | large menu icons fall back to 16 px |
| C4b | **C4 + the FX cells' 16 px cells** | **3,164** | `SKM_ICON24=0`: the atlas is not generated and the C does not name it; the 8 FX-only glyphs move to the 16 px set | **DONE** | header battery and USB plug 24 → 16 px, FX cells 24 → 16 px. `tests/ui_render.c` alignment sweep: 0 off. See §15.5 |
| C5 | icon atlas 16 px | 5,860 | same | S | icons fall back to 12 px |
| C6 | graph strip | 14,976 code + **59,520 RAM** | remove the graph page and `draw_graph` | L | WHEEL drawbar art, perf meters, sample waveform |
| C7 | games | 2,892 + 452 RAM | drop the menu rows and `game.c` | S | nothing that matters — see §9 |

## 5. Cut catalogue — RAM

| id | item | B saved | mechanism | effort | what breaks |
|---|---|---|---|---|---|
| **R1** | delay 1.49 s → 0.745 s | **65,536** | `DLY_LEN` 65536 → 32768 | S | delays longer than 0.75 s (1/4 note below 80 BPM) |
| R2 | canvas RGB565 → 8-bit indexed | 29,760 | `cv_px` becomes `uint8_t` + a 256-entry palette LUT at blit | M | needs >256 colours on one screen — none today, the themes are small palettes |
| R3 | canvas 124 → 96 rows | 13,440 | layout | M | graph height |
| R4 | PHYS polyphony 3 → 2 | 17,184 | `PHYS_POLY` | S | three-note string chords on one part |
| R5 | GRAIN polyphony 3 → 2 | 9,360 | `GR_POLY` | S | grain voice cap |
| R6 | slicer 4,096 → 2,048 samples/track | 16,384 | `SL_LEN` | S | 186 ms per track, PERFORM loop halves |

---

## 6. Bundles

| bundle | flash free | RAM free |
|---|---|---|
| B0 — today | 888 | 984 (340 RAM + 644 spendable pool) |
| B1 — content (C1) | **63,732** | 984 |
| B2 — B1 + R1 | 63,732 | **66,520** |
| B3 — B2 + UI diet (C2 C3 C4 C5) | 93,364 | 66,520 |
| B4 — B3 + C6 + R2 | 108,272 | 96,280 |

## 7. What the headroom buys

| target | price | fits where |
|---|---|---|
| one new engine (code that borrows existing buffers) | 3–5 KB | **only B1 and up** — this used to say "anywhere, even B0" |
| better factory PIANO (today: **none** — the set was removed with its 41,349 B) | ×3 layers ≈ 124 KB | B2/B3, and only after §12 is re-funded |
| 18-zone sampled instrument, **1** velocity layer, at erik37's density (7,526 B per layer-zone) | ~135 KB + ~20 KB RAM | B3 + R1 |
| same, **2** layers | ~271 KB | B4 + store, 260,730 B combined — just short |
| erik37's actual grand (18 × 3 + releases) | 406,428 B | **does not fit the slot at all** — see §8 |

## 8. Levers that are not "cuts"

- **Data store: 57,344 B free** (`0xD3000..0xE0000` = 53,248 + `0xFE000..0xFF000` = 4,096), separate
  from the app slot and untouched by an OTA. A bank there survives reflash, but it has to be staged
  by the app, not the loader.
- **The 184,320 B window `0x93000..0xC0000`** (the stock VM arena) is unmapped but useless as-is:
  `firmware/loader/ldr_core.c` hard-stops at `LDR_APP_HI 0x93000` and `firmware/loader/loader.c`
  refuses writes above it. Using it means changing the loader that is already in the field — brick
  risk, out of scope for a content cut.
- **Rate is already where it should be.** `TR = 22050` is the stored rate for every factory set and
  for the BREAK loop (44,100 samples = 2 s = one bar at 120 BPM, so `SLC_BREAK_INIT`'s first number
  is a sample count, not a rate). Halving further is a quality dial, not a lever worth pulling.

## 9. Rejected (measured, not worth it)

- **Flappy / Tetris.** 2,892 B flash, 452 B RAM (`gm` 248 + `tet_rx` 204) = 0.6 % of the image.
  Deleting them buys less than one font face and costs the menu/settings rows, the companion modal
  and the link mirror tests. Keep them.
- **The VST's looks.** 0 B of firmware. The `StudioKEYS.vst3` binary lives on the Mac.
- **`editor.c` (14,804 B).** It is the workflow, not decoration.
- **Growing `APP_SLOT`.** Not a cut, and not free: loader + bootguard + every unit in the field.

## 10. Gates and churn per cut

Whatever is cut, these go red and must be updated in the same commit:

| gate | file | why |
|---|---|---|
| image fits | `tools/build.py:264`, `tools/fm1pkg_make.py:130` | APP_SLOT |
| preset hash | `host/preset_hash.sha256` | desk params hash if presets change |
| SAMPLE set order | `tests/ui_test.c:464`, `:3531` | `presets[3] == "SAX"`, knob-2 list order |
| "stays SAMPLE" | `tests/upreset_test.c:235`, `:251` | uses FLUTE as the SAMPLE-engine case |
| project round-trip | `tests/project_test.c:644` | `p[P_E0] = 2` (FLUTE) |
| WHEEL registration | `tests/pages_test.c:220` | **unaffected** — that "FLUTE" is a WHEEL register name, not a sample set |
| goldens | `tests/golden.txt`, `tests/cpu_baseline.txt` | regenerate after the tables change |
| attribution | `assets/samples-cc0/CREDITS.txt`, `ATTRIBUTION.txt` | drop the two sources |

## 11. Recommendation (before the piano requirement)

> **C1 is withdrawn.** §12 wants orchestral content, and FLUTE + SAX *are* the winds: already looped,
> 10,474 B per zone, the cheapest content in the image. What follows still stands.

1. **R1 when a buffer is needed** — 65,536 B of pool for one `#define`. The delay is the only
   object in the firmware that is larger than the entire free space, and 0.745 s still covers every
   musical use except very slow 1/4-note slapback.
2. **Stop there unless there is a design decision to make.** Fonts, icons and graphs are 110 KB and
   they are the instrument's face; cutting them is a product decision, not a space decision.
3. **Keep the games.**

---

## 12. A real piano plus a small orchestra

Requirement: a piano good enough to dep for a Montage M7 on a gig, no sympathetic resonance, no
pedal/release samples, no bespoke graphics; plus a little orchestral content.

### 12.1 Why the piano is the expensive one

Price a **looped sustain** and a **piano attack** the same way — bytes = seconds × rate × bits/8:

| content | shape | B |
|---|---|---|
| FLUTE, 1 zone (shipped) | 0.95 s **looped**, 22.05 kHz, 4-bit | 10,474 |
| SAX, 1 zone (shipped) | same | 10,474 |
| current PIANO, 1 zone (shipped) | 0.75 s **one-shot**, 1 velocity layer | 8,269 |

A looped sustain is cheap because one cycle covers the rest of the note. A piano attack is not:
the transient cannot be looped, so its length is a hard cost, and its timbre changes with both key
and velocity, so it needs many zones × several layers.

The shipped PIANO is **5 zones × 1 layer × 0.75 s, one-shot** — 41,349 B, and it stops dead at
0.75 s. That is the whole reason it does not sound like a piano. Zone count is not the main deficit;
the missing piece is **velocity layering plus the loop/decay machinery**.

### 12.2 The three techniques that make it fit (all from erik37, all priced)

| technique | what it buys | cost |
|---|---|---|
| **2nd-order block ADPCM** (17 B / 32 samples, 16 predictor pairs, filter+shift per block) instead of IMA | IMA's 8-entry step table smears a piano attack; a predictor pair adapts to the waveform. This is the difference between "mp3-ish piano" and "piano". | ~1.2 KB code, ~500 B tables. Its own bank, its own reader — `eng_sample.c` untouched. |
| **Decay flattening + loop** — fit a two-slope dB/s decay per zone-layer offline, divide it out of the stored audio, re-apply it at runtime as a Q30 block multiplier with a knee | lets a zone store 0.45–0.70 s instead of 2–3 s. **Without it the bank is 3–4× larger.** Loop restart must restore the predictor history (`h1`, `h2`) or the seam clicks — the same thing `sample_next()` already does with `z->pred` / `z->idx`. | ~400 B code, 16 B/zone-layer in the table |
| **XIP playback, no RAM buffer** | the bank is `.rodata`, read through the XIP window like `SMP_DATA` | 0 B. Voice state is ~96 B: position, fraction, h1/h2, block multiplier, loop points, env, gain |

RAM total: **20 voices × 96 B ≈ 1.9 KB**, which fits today's free pool (8,836 B) with no cuts at all.
CPU: erik37 runs `ERK_VOICES 20` on the same silicon; we already gate CPU with
`tests/cpu_baseline.txt`, so the regression is measurable before it ships.

### 12.3 Why the GUI costs nothing

`eng_t` (`core.h:223`) already carries `param_desc_t edit[12]`, `preset_t *presets`, `poly`,
`note_on` and `render`. The editor, the preset browsers, project save/load and the companion are all
generic over that descriptor — `ENGINE_ORDER[]` is the only list that needs a row. So "simple GUI"
is not a design decision, it is the default: 12 param labels, 7 presets, no drawing code. **~700 B
of rodata, 0 B of UI.**

And the engine index is append-only (`core.h:139`): `ENG_PIANO` becomes 14, old projects still name
their engines correctly, and `ENG_SAMPLE` / `ENG_PHYS` / `ENG_DRUM` already prove the sampled path.

### 12.4 Bank cost (2 velocity layers, 22.05/28/32 kHz by register, 0.70/0.45/0.20 s attack+loop)

| zones | bank | + table | | zones | bank | + table |
|---|---|---|---|---|---|---|
| 8 | 88.6 K | 192 B | | 14 | 147.0 K | 336 B |
| 10 | 107.1 K | 240 B | | 16 | 168.3 K | 384 B |
| 11 | 116.2 K | 264 B | | 18 | 189.7 K | 432 B |
| 12 | 128.5 K | 288 B | | 18 × 3 layers | 284.5 K | — |

One zone covers 88/(N−1) semitones. Piano is the most formant-sensitive instrument there is, so:

- **10 zones → ±4.9 st**: audible stretch, acceptable in a band, exposed solo it is not a Montage.
- **12–14 zones → ±4.0 / ±3.4 st**: the honest "credible dep" range.
- **18 zones → ±2.6 st**: erik37's choice, and the reason his sounds like a grand.

Orchestral ensembles are the opposite: dense, non-formantic, so **2 zones is genuinely fine** for
strings and brass (10,474 B per zone at 0.95 s looped).

### 12.5 Funding

> **Status: unfunded as of the 2026-10-22 build.** This table was computed against 88,860 B free.
> The samples commit that shipped instead (`efbbfeb`) removed PIANO and SLICE's BREAK — which are
> sources S1 and S3 below — and spent the rest on STRINGS and HORN. Free flash is now 888 B. The
> arithmetic below is still the right arithmetic; almost none of the money is left.

| source | B | note |
|---|---|---|
| free today | 888 | was 88,860 |
| ~~S1 — PIANO set 0, 5 zones → 2 zones~~ | ~~+24,811~~ | **gone**: the set was removed entirely |
| S2 — UI diet C2 C3 C4 C5 | +29,632 | XS face, L face, 24 px and 16 px icon atlases |
| ~~S3 — C1b, SLICE's BREAK loop~~ | ~~+22,050~~ | **gone**: `SLC_SMP` is 976 B today |
| S4 — C6, graph strip | +14,976 | also frees 59,520 B of pool, which the piano does not need |
| C1 — FLUTE + SAX factory sets | +62,844 | the only remaining content lever, and §11 withdraws it |
| engine + 12 params + 7 presets | −5,120 | once |


| package | contents | total | available | verdict |
|---|---|---|---|---|
| **P1** | piano 12 × 2, orchestra untouched | 136,952 | 143,303 (S1+S2) | fits, 6.4 K spare — **too tight to ship** |
| **P2** | piano 10 × 2 + STRINGS 2 + BRASS 2 | 156,964 | 165,353 (S1+S2+S3) | fits, 8.4 K spare |
| **P3** | piano 11 × 2 + STRINGS 2 + BRASS 2 | 163,504 | 180,329 (S1…S4) | fits, 16.8 K spare |
| **P4** | piano 14 × 2 + STRINGS 3 + BRASS 3 | 218,842 | 180,329 | **short by 38.5 K** |

Re-funded on what is actually still available — 888 free + S2 + S4 + C1 − 5,120 engine = **103,220 B** —
**every one of P1…P4 is short**, P1 by 33.7 K. The piano plan needs a new funding source before §13
Phase C can be scheduled; Phase A (the pilot, which *frees* 16,543 B by looping the existing zones)
now has no PIANO set to pilot on, so it needs a set fetched first (§13.2 has the recipe).

**You cannot have both erik37's zone density and an orchestra.** That is the entire trade, and it is
a trade in flash, not in identity.

### 12.6 Step 0 — the pilot: it is free of *flash*, not of code

Re-render the *existing* 5 PIANO zones looped at 0.45 s: **24,806 B instead of 41,349** (frees 16,543).

Looping alone is not enough. A piano loop sits ~20 dB down, so without a decay model a held note
would sustain flat — an organ, not a piano. Looping therefore *requires* flatten-and-re-apply, which
means ~600 B of code and a per-zone decay descriptor. `smp_zone_t` is also the **user-sample record
format** (`SMP_USER`, parsed from flash, written by the companion), so the decay fields cannot go in
the struct — they belong in a parallel factory-only table, 16 B per zone, keyed by zone index, with
user zones reading zero (no re-application). Zero format change, zero companion change.

That is the pilot: it de-risks exactly the DSP the real piano engine needs (decay fit, block
multiplier with a knee, predictor-preserving loop wrap) on hardware, for 5 zones, before anyone
commits 130 KB to it.

### 12.7 Honest ceiling

The Montage's CFX is measured in GB. 130 KB of 4.25-bit ADPCM is roughly 1/10,000 of that. What the
decay-flattening trick buys is that it does not *sound* like 130 KB of one-shots — it sounds like a
1990s high-end rompler piano, which is a real, gigable instrument for pop, funk and jazz comping
through a PA. It is **not** a solo-ballad, lid-open, exposed piano. If that is the use case, the
answer is the M7 and this is the backup.

---

## 13. Implementation plan — firmware only, VST untouched

Scope decision: the companion is already the "unlimited SMK-37" and stays out of this. Everything
below is `firmware/` + `tools/`. No VST source is touched, no parity obligation is created until a
piano patch ships, and if it never ships the VST is unaffected.

### 13.1 The asset that makes VST-free development possible

`host/sk_headless_cli.c` renders a WAV offline from the **same firmware C** (`sk_host_render_f32`,
44.1 kHz). So the engine, the codec and the decay fit can all be listened to and A/B'd natively, at
full precision, before anything is flashed. That is the reference loop for this plan — not the VST.

### 13.2 Material is already sourced, and it is already CC0

`tools/fetch_cc0.py` `PICK` already maps every set this plan needs:

| set | source | status |
|---|---|---|
| PIANO | VCSL, Grand Piano Kawai, Sustains, `_C1..C5_v2_rr1` | shipped — **single velocity (`v2`), 5 roots** |
| STRINGS | VSCO-2-CE, Violin Section `susVib`, `_A2/A3/B4_v1` | registry only, not fetched |
| BRASS | VSCO-2-CE, Trumpet `sus`, `_A2/C3/A4_v1_rr1` | registry only, not fetched |
| FLUTE / SAX | shipped | looped, 0.95 s, 10,474 B/zone |
| PIZZ | VSCO-2-CE, Violin Section Pizz | registry only (bonus articulation) |

Two consequences:

- **The orchestra costs no new licensing and no new integration.** `STRINGS` and `BRASS` are registry
  lines that were written and never built.
- **The shipped piano's real defect is the source, not the encoding**: one velocity layer means the
  dynamic timbre cannot change, no matter how many zones you add. A second layer means fetching
  `_v1_`/`_v6_` variants from the same directory — the same CC0 library, the same code path.

### 13.3 Toolchain gap

`tools/gen_samples.py` is pure Python (`sampleio.py`: `ima_encode`, `resample`, `onset`,
`key_split`). The decay fit, the 2nd-order predictor search and the loop correlation search are all
FFT/vector workloads; `numpy`/`scipy` are **not** installed here. Either add them to the `BUILDING.md`
prerequisites (they are already required by erik37's builder, so the pattern is proven) or accept a
multi-minute pure-Python bank build. **Decide before Phase B; nothing else in the plan depends on it.**

### 13.4 Phases, each with its exit gate

| phase | work | exit gate | cost |
|---|---|---|---|
| **A — pilot** | decay descriptor table + block multiplier with knee + predictor-preserving loop, applied to the 5 shipped PIANO zones, re-rendered looped at 0.45 s | `studiokeys.bin` ≤ (its own measured baseline) − 16,543 B; host WAV A/B vs today; `tests/cpu_baseline.txt` unchanged or regenerated with a note | ~600 B code, 80 B table, −16,543 B bank |
| **B — builder** | `tools/build_piano.py`: VCSL fetch for N roots × 2 velocity layers, two-slope decay fit, flatten, 2nd-order block ADPCM encode, loop search, zone table emit; `--report` with per-zone SNR | bank size within 2 % of §12.4; SNR report shows the 4.25-bit path beating IMA on the same source | tool only, 0 B firmware |
| **C — engine** | `eng_piano.c` (~1.2 KB decoder + ~400 B decay + voice state), `ENG_PIANO` appended at index 14, 12 `param_desc_t`, 7 `preset_t`, `ENGINE_ORDER` row | host A/B; 20-voice CPU within budget; `nm` shows engine ≤ 5,120 B; every existing test still green | 5,120 B + bank |
| **D — orchestra** | `STRINGS` + `BRASS` registry fetches, `CC0_SETS` lines, 2 zones each at 0.95 s | 2 × 20,948 B; SAMPLE knob-2 list and `tests/ui_test.c` order updated in the same commit | ~42 KB |
| **E — funding** | apply the §12.5 sources in order S1 → S2 → S3 → S4 only as far as the chosen package needs | image fits `APP_SLOT` with ≥ 8 KB spare; phase-1 preflight green | the cuts |

Order matters: A before B proves the decay DSP on hardware; B before C proves the bank before the
engine that has to read it; C before E means the funding cuts are only taken once the piano actually
exists and its true size is known. Nothing in A–D requires a funding cut.

### 13.5 Decision matrix — all four packages stay on the table

| | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| piano zones × layers | 12 × 2 | 10 × 2 | 11 × 2 | 14 × 2 |
| worst stretch | ±4.0 st | ±4.9 st | ±4.5 st | ±3.4 st |
| STRINGS / BRASS | — | 2 / 2 zones | 2 / 2 zones | 3 / 3 zones |
| funding | S1 S2 | S1 S2 S3 | S1 S2 S3 S4 | needs +38.5 K beyond everything |
| spare after | 6.4 K | 8.4 K | 16.8 K | −38.5 K |
| RAM delta | +1.9 K | +1.9 K | +1.9 K | +1.9 K |

P4 is not reachable without a fifth source (the 184,320 B VM window, i.e. a loader change). If that
ever becomes acceptable, P4 is what it buys.

### 13.6 Risks, in order of how likely they are to bite

1. **Loop seams on a piano.** Mitigated by predictor-history restore at the wrap — `sample_next()`
   already does this for FLUTE/SAX, so the mechanism is proven in this codebase, not theoretical.
2. **CPU on 20 voices.** Proven at `ERK_VOICES 20` on the same silicon, but our render path also
   carries the 4-part mixer, insert chain and FX. Gate on `tests/cpu_baseline.txt`; drop poly to 16
   if it does not fit — 16 voices is still a playable piano.
3. **IMA vs 2nd-order quality.** Not a risk if Phase B's `--report` shows the SNR delta, which is why
   that report is a gate and not a nicety.
4. **Zone stretch being audible.** The honest per-package numbers are in §13.5; P2's ±4.9 st is the
   one to listen to before committing.
5. **Toolchain (numpy).** Resolve in Phase B; blocks nothing else.

### 13.7 What is deliberately *not* planned

- Sympathetic/string resonance and pedal-noise layers — excluded by requirement, and they are the two
  most expensive things in a real piano library.
- A bespoke piano page — §12.3 shows the generic descriptor already covers it.
- Any VST-side work, including making the companion model the piano engine. Deferred until a piano
  patch actually ships, at which point it becomes a normal parity item.
- Raising `APP_SLOT`, touching the loader, or mapping the VM window.



---

## 14. Polyphony

Two ceilings, and neither of them is a voice *count*: RAM bounds the arena, the CPU bounds how much
of it can sound at once.

### 14.1 What exists: one arena, one pot

```startLine:20:28:firmware/src/voice_budget.h
#ifndef SKM_VOICE_POOL
#define SKM_VOICE_POOL 48
#endif
#ifndef SKM_VOICE_LOAD
#define SKM_VOICE_LOAD 6312
#endif
```

`VOICE_POOL` (`core.h`) is **one arena of 48 voices for all four parts** — the four `voice_t v[12]`
arrays are gone; a voice says which part owns it (`voice_t.part`, `VOICE_FREE = 0xFF`) and which slot
of that part it is (`pi`). The pool is therefore not a cap per part: one part may hold all 48 (a
pedalled piano, a UNISON belt) and the other three get what it leaves.

What decides *which* voices may sound is `SKM_VOICE_LOAD`, **the CPU pot, in instructions per sample**.
Every engine declares `engine_t.cost` (the measured column of `tests/cpu_baseline.txt`, idle
subtracted, per voice) and, where the same engine has cheap and expensive waves, a `cost_of(t)`
(ANALOG: 130 for the two-oscillator waves, 526 for the seven-saw supersaw). `voice_load()` adds up
the sounding voices' cost and `voice_alloc()` takes a new voice only if it fits; `voices_busy()` is
now a diagnostic, not a rule. This is Korg's `Voice Reserve` / per-engine "cost" idea: the number the
instrument plays is what its clock sustains, not a guessed count.

Two ceilings still apply on top, because their *side state* is per voice and does not fit 48 of them:
`engine_t.poly` — PHYS 3, GRAIN 3, FM6 6, FORMANT 4, WHEEL / SLICE / DIGITAL 8. `trk_nvoice()` takes
`min(poly, VOICE_POOL)`, so raising the pool never raises them.

`voice_t` is 84 B (unchanged — `part` / `pi` / `sslot` fit in the padding the five old bytes left),
and a piano needs **no side table** (unlike PHYS at 4,296 B/voice or GRAIN at 2,340 B/voice):
position, fraction, the two predictor histories, the block multiplier and the loop points all fit in
the existing `ph[3]` + `s[8]`.

The pot's start value is the worst case the old count budget (4 x 12) could ever be asked for: 12
voices, every one of them the heaviest voice on the instrument, the ANALOG supersaw at 526 apiece
(6,312 host instr/sample). So no preset plays fewer voices than before, and every lighter engine
plays more. `audio.c` moves it down 1/8 per overrun and back up 1/64
per light half (`voice_budget_shed` / `voice_budget_rest`); the companion does the same from its own
callback time. The same header is read by the companion's Limited mode, so the plugin cannot keep
emulating an older keyboard.

### 14.2 RAM: the actual limit (the earlier claim here was wrong)

| `VOICE_POOL` | N × 84 B | vs 48 |
|---|---|---|
| **48 (today)** | **4,032** | **—** (exactly what 4 × 12 `t->v[]` cost) |
| 56 | 4,704 | +672 |
| 64 | 5,376 | +1,344 |
| 96 | 8,064 | +4,032 |
| 128 | 10,752 | +6,720 |

Three things this table hides, all verified against `nm -S build/studiokeys.elf`:

1. **The arena lives in `.bss`, not `.pool`** (`trk` at `0x01C08964`, now `vpool` next to it).
   Moving it needs a `voice_t *` in `track_t` — and `project_t` *contains* `track_t`
   copies (`project.c:734`), so a pointer there is a save-format change. Moving all of `trk` costs
   12,448 B of pool.
2. **The pool cannot pay for it.** `tools/build.py:278` keeps **≥ 8,192 B of the 344,064 B pool
   spare** (4 KiB for the bench-only UAC input ring, 4 KiB of slack). With 8,836 B free, that is
   **644 B of genuinely spendable pool** — not 8,836.
3. **Side state scales too, unless each engine caps itself.** At a pool of 96, uncapped side arrays
   would grow by SLICE `slc_rbuf` +8,192, WHEEL `drw_v` +5,888, DIGITAL `digital_*` +1,280 on top of
   the 4,032 above. They are capped like PHYS/GR/FM6 already were: `SLC_POLY 8`, `DRW_POLY 8`,
   `DGT_POLY 8` (each with an `engine_t.poly` and a `_Static_assert`), so a bigger arena costs RAM
   and buys only heavy-engine slots the pot will rarely fund anyway.

So: **the 48-slot arena fits with 340 B of RAM to spare** (97,964 / 98,304, measured on the
2026-10-22 build) — and it is the same
4,032 B the four `v[12]` arrays used, so pooling cost nothing. What is *not* free is more slots:
64 is 940 B short, 96 is 3,628 B short. The two clean ways to fund a bigger arena, with what each
costs:

| way | frees | cost |
|---|---|---|
| `DLY_LEN` 65,536 → 32,768, then `vpool` into `.pool` | 65,536 pool (RAM −4,032) | FX1 delay max 1.49 s → 0.74 s; the "1/4 note at 40 BPM" trick is gone |
| SONG chain 4 → 2 sources (`song_chain.c` `chain.source[4]`) | 5,720 RAM | SONG mode chains two saved projects instead of four |

Either lands 128 slots (10,752 B) with room to spare (the delay route leaves 12,852 B RAM and
61,924 B pool free). Neither is free, and neither buys CPU: with the pot at 6,312 the 48 slots are
what binds for light engines and the pot is what binds for heavy ones (12 supersaw, 48 ANALOG) — the
arena is headroom, not a promise of 48 sounding voices.

### 14.3 CPU: the real limit

**First, a trap in the existing baseline.** `cpu/SAMPLE/00_PIANO` reads 488 for 8 voices, i.e. 22.6
per voice — and that number is wrong by 4×. The PIANO zone is `16537` samples at 22.05 kHz =
**0.75 s, one-shot**, while `job_cpu` starts counting at 0.5 s and measures one second. The voice is
silent for 75 % of the window. Corrected for its real duty cycle it is **90.5 per voice**, which
brackets the two *looped* sets that do sound throughout:

| preset | host instr/sample, 8 voices | per voice |
|---|---|---|
| SAMPLE 00_PIANO (one-shot, silent 75 % of the window) | 488 | 22.6 → **90.5 corrected** |
| SAMPLE 02_FLUTE (looped) | 830 | **65.4** |
| SAMPLE 03_SAX (looped) | 951 | **80.5** |
| 3 parts full + drums (the band) | 2,983 | — (it has its 8 + 8 + 4 voices now) |

**Use 65–80, never the PIANO line.** Any piano estimate calibrated against `00_PIANO` will be
optimistic by a factor of four.

The 2nd-order ADPCM decoder is *cheaper* than the IMA one at identical post-processing — measured on
the two inner loops in isolation (host instructions per 44.1 kHz output sample, LPF and gain applied
to both):

| stepq (decodes/sample) | IMA | 2nd-order ADPCM |
|---|---|---|
| 32,768 (0.5 — a 22.05 kHz zone) | 44.7 | **40.8** |
| 48,000 (0.73) | 56.3 | **49.9** |
| 65,536 (1.0 — a 44.1 kHz zone) | 69.6 | **60.7** |

So the break-even is real: **−13 % per voice**, because IMA's reconstruction needs three conditional
accumulates and two clamps per sample, while the predictor is two multiplies. A piano voice is
therefore **≈ 58–72 host instructions/sample**, not more expensive than the pluck/flute it replaces.

Expressing polyphony against the pot (`SKM_VOICE_LOAD` 6,312) and against the band mix §14.3
measures — clock-free, because the firmware never sets its own clock (§14.6):

| piano voices | per voice | piano only | piano + the band (2,983) | fits the 6,312 pot? |
|---|---|---|---|---|
| 8 | 60–75 | 480–600 | 3,463–3,583 | yes |
| 16 | 60–75 | 960–1,200 | 3,943–4,183 | yes |
| 24 | 60–75 | 1,440–1,800 | 4,423–4,783 | yes |
| 32 | 60–75 | 1,920–2,400 | 4,903–5,383 | yes |
| 40 | 60–75 | 2,400–3,000 | 5,383–5,983 | yes, at the edge |
| 48 | 60–75 | 2,880–3,600 | 5,863–6,583 | no — the piano takes ~4 voices from the band |

The last column is not a failure mode: the pot is **shared**, so a band playing against a piano
holding 24 notes does not get 24 + 20 — it gets 6,312 split between them, and `voice_victim` takes
the oldest *released* voice first, and never the lowest held note of a part. That is exactly the
behaviour a pianist wants: the tail of the chord you let go is what gets recycled, and a 24-note
pedalled chord still costs the band its own released tails, not its lead.

### 14.4 What to set

| knob | value | why |
|---|---|---|
| `SKM_VOICE_LOAD` | **6,312 now** | the worst case the 4 × 12 count budget could be asked for (12 × the heaviest voice, 526); `voice_budget_shed` trims it on the instrument down to 1/4 of it, so this is a start value, not a cap |
| `SKM_VOICE_POOL` | **48 now** | 4,032 B, exactly what the four `v[12]` arrays cost; the arena is RAM, the pot is CPU |
| piano `engine_t.poly` | 24 | the engine's own cap (its side state is nil, so it can afford it): the pot decides whether 24 fit |
| `P_VOICE` for piano presets | **POLY** | `SK_UNISON 8` gives a UNISON note up to 8 voices — one chord and the pot is gone |
| shed | leave at 85 % | `voice_victim` already prefers *released* voices first, then extra UNISON, then the oldest held note that is not the bass — correct for a pedal-down piano |

### 14.5 The lever that buys voices and flash at once: zone rate

Decode work per output sample is `source_rate / 44100 × pitch ratio`:

| zone rate | decodes/sample at unison | at +3 st |
|---|---|---|
| 16,000 | 0.36 | 0.43 |
| 22,050 (today) | 0.50 | 0.59 |
| 32,000 | 0.73 | 0.86 |

So a piano bank that keeps **16 kHz below C3, 22.05 kHz through C6, 28–32 kHz above** costs ~30 % less
CPU *and* ~30 % fewer bytes than the current flat 22.05 kHz — the two budgets moving the same way for
once. (16 kHz on a zone rooted at A1, where the fundamental is 55 Hz and the useful harmonics stop
below 8 kHz, is not a quality compromise.)

### 14.6 One measurement calibrates everything

The firmware **never sets its own clock** — no `clk_set`, no `SYS_CLK`, nothing in `firmware/hal/`.
It runs at whatever UBOOT leaves behind, and two candidates differ by 40 % (192 vs 320 MHz). So the
absolute budget is unknown, and every "% of realtime" figure above is therefore expressed as a
**ratio to the mix the instrument already plays**. That ratio is clock-free.

To get the absolute number, one reading collapses it:

```text
DIAG page -> cpu_pct   (or: console `cpu_pct`, firmware/src/console.c:198)
```

Play `3 parts full + drums` (the 1,581-instruction mix) and read `cpu_pct`:

```text
host instructions that fit in the deadline  =  1581 / (cpu_pct / 100)
instructions/sample at 100 %                =  1581 / (cpu_pct / 100)
host -> target ratio                        =  ...  (one equation, one unknown per clock candidate)
```

With that single number, §14.3's table converts from "× today's mix" to an absolute voice count, and
`fm1_alnk0_irq` tells you whether there is headroom left in the ISR itself.

Also worth reading while there: **`voice_kills`**. If it is climbing during normal play, the pot is
already too small for the existing presets and the engines' `cost` values are the place to look, not
a bigger pool.

### 14.7 Making it lighter — ordered by (saving ÷ work)

| # | lever | saves | work | note |
|---|---|---|---|---|
| **1** | The pool + the pot (**done**): one 48-slot arena, `engine_t.cost` per engine, and the side state capped (`SLC_POLY` / `DRW_POLY` / `DGT_POLY` 8) | nothing: 48 × 84 B is the 4,032 B the four `v[12]` arrays cost | S | the caps are what keep a bigger arena from multiplying three engines' arrays; see §14.9 for what the pot funds per engine |
| **2** | Hoist the three per-sample branches out of the loop: `if (sh)` (BITS), `if (drv)` (DRV), and the LPF `v->s[7]` | ~8–10 instr/voice/sample when all three are off, which is every piano preset | M | three loop variants chosen once per block, `sample_render` today tests all three every sample (`eng_sample.c:224-227`) |
| **3** | Fold the block decay multiplier into the amplitude instead of the sample | 1 mul + 1 shift per sample per voice | S | the multiplier is constant for 32 samples; `amp × bm` once per block |
| **4** | `ownenv = 1` + the `amp` callback (FM6 already does this) | the ADSR ramp in `voice.c` (`m.amp0`/`m.amp1`, `voice_amp`) per sample | S | a decay-modelled piano has no ADSR; the block multiplier *is* the amplitude |
| **5** | Zone rate by register: 16 kHz below C3 | −28 % decodes on half the keyboard, **−30 % bytes** | S | §14.5; the only lever that moves flash and CPU together |
| **6** | One-layer attack instead of decoding both velocity layers for 0.3 s | one whole extra decode per attacking voice | M | erik37 prices `two_layers` as a separate budget line for this reason |
| **7** | Precompute the voice's end from the decay model | not CPU: **polyphony** | S | the multiplier reaches −60 dB at a known sample index; free the voice there instead of waiting for a steal. Needs one `s[]` slot |
| **8** | Never ship UNISON on a piano preset | up to 8× the cost of one note | S | `SK_UNISON 8`; POLY only |

Levers 2–4 apply to the **existing** `sample_render`, so they speed up the six factory SAMPLE presets
and any user sample slot as well — they are not piano-specific. Lever 2 alone is worth roughly a
fifth of a voice for every sampled voice on the instrument.

### 14.8 Do not do

- **Raise the sample rate.** 44.1 kHz is 1.0 decode/sample per voice at unison; the zone rates are the
  knob, and they are already below the output rate.
- **Give the piano a side table per voice.** PHYS costs 4,296 B/voice, GRAIN 2,340 B/voice; the piano
  fits in the 84 B `voice_t`. That is the whole trick.
- **Assume the pool can buy polyphony.** The arena is RAM, the pot is CPU: 48 slots with a 6,312 pot
  is 48 ANALOG voices or 12 supersaw ones (§14.9). More slots cost `.bss` and buy nothing until the pot
  can pay for them, and the pot can only be raised by making the engines cheaper (§14.7).
- **Size `SKM_VOICE_LOAD` on the host alone.** It is a start value; `voice_budget_shed` trims it on
  the unit, and the companion does the same from its callback time.

### 14.9 The answer: the pot decides, per engine

Every engine declares what one of its voices costs (`engine_t.cost`, `tests/cpu_baseline.txt` minus
the 307-instruction idle mix, divided by the voices that sounded), and the pot is 6,312. What that
buys, before the engine's own cap (`engine_t.poly`) and the arena (48 slots):

| engine | `cost` | voices the pot funds | what binds first? |
|---|---|---|---|
| ANALOG (two-oscillator waves) | 130 | 48 | the arena |
| ANALOG supersaw / hype / ballad (`cost_of`) | **526** | **12** | the pot — and 12 is what the old count budget played |
| PHASE | 165 | 38 | the pot |
| TRIO | 150 | 42 | the pot |
| FM6 | 142 | 44 | **its cap, 6** |
| NOISE | 122 | 48 | the arena |
| PHYS | 117 | 53 | **its cap, 3** |
| DRUM | 115 | 48 | the arena (lanes: `DV_NLANE`) |
| DIGITAL | 90 | 48 | **its cap, 8** |
| SLICE | 90 | 48 | **its cap, 8** |
| FORMANT (VOICE) | 89 | 48 | **its cap, 4** |
| WHEEL | 86 | 48 | **its cap, 8** |
| GRAIN | 83 | 48 | **its cap, 3** |
| SAMPLE | 80 | 48 | the arena |
| LOFI | 69 | 48 | the arena |

Three things fall out of that table:

- **The count budget is gone, and that is the point.** 12 PHASE voices and 12 GRAIN voices were never
  the same load; the pot charges each what it costs, so a light engine plays its 48 slots and a
  supersaw plays 12 — the same rule in every engine, one number to tune. And 12 supersaw voices is
  exactly what the old count budget gave that patch, so the change is a gain everywhere, a loss
  nowhere.
- **The engine caps still bind, and they are what keep the heavy engines honest.** `PHYS_POLY 3`,
  `GR_POLY 3`, `FM6_POLY 6`, FORMANT `poly 4`, `SLC_POLY / DRW_POLY / DGT_POLY 8` are per-voice side
  state, not CPU: `trk_nvoice()` takes `min(poly, VOICE_POOL)`, so a bigger pot never multiplies them.
- **The piano at 58–72 fits in the SAMPLE class**, next to FLUTE at 65, so its `cost` is 80 and the
  pot funds ~20 of it beside the band (§14.3) — which is the number the piano work is aiming at.

The pot moves on the instrument (`voice_budget_shed` on two overloaded halves in a row, back up 1/64
per light half, and never below a quarter of 6,312 — 1,578, about 12 ANALOG voices — so a bad reading
cannot choke the instrument down to nothing), so the value in the header is a **start value measured
on the host**, not a claim about the AC6926's clock. If `cpu_pct` (§14.6) says the worst mix runs
above ~85 %, the pot has already trimmed itself and the number that matters is what it settled at —
read it (`voice_budget_cap()` in the tests, `sk_host_voice_load()` in the companion), do not guess it.

---

## 15. Bluetooth — BLE-MIDI only

Scope is settled: **BLE-MIDI (Apple BLE-MIDI 1.0), and nothing else.** Bluetooth audio is out of this
tree permanently — no A2DP sink, no SBC decoder, no second I2S consumer. `audio_in.c`'s one producer is
USB (`SKM_UAC_PLAY`).

That decision buys less than it looked like it would. Dropping A2DP from *our* side removes the sink,
the decoder and the second audio clock domain, but it does not remove them from the **stack**: the
measured `btstack_init()` closure carries 329 classic-profile symbols, and the SDK ships no BLE-only
archive. §15.3 has the numbers; the short version is that the link is larger than the app slot.

Status: **the device-side adapter is written and tested; the radio cannot be linked into this image.**

- `firmware/src/ble_midi.c` — the packet codec and both directions of the adapter, behind
  `SKM_BLE_MIDI`. Default **0**, and at 0 the file compiles to nothing: today's image pays not one byte
  for it. `tests/midi_ble_test.c` drives it with `SKM_BLE_MIDI=1` (17 checks: both timestamp widths,
  running status inside and across packets, realtime, SysEx, the malformed cases, the ring and its
  resync, the outbound packets and the disconnect).
- `usb.c` `midi_out_event()` queues to it from the audio ISR; `main.c` `fm1_timer5_irq` drains it into
  the same queue as USB and TRS, tagged source 4.
- Not wired: `ble_midi_rx` / `ble_midi_flush` / `ble_midi_set_notify` have no caller, because they are
  what the stack adapter calls. `tools/bt_spike.c` + `tools/bt_spike.sh` exist to measure the stack and
  **have never been run** (they need `$AC79_SDK` and a working container runtime; neither is on this
  machine). The BT key still answers `BT: NO RADIO YET` (`ui_input.c:1221`).

### 15.1 Where the code would come from

Not from `m-vave/smk37_open`: every Bluetooth entry point there (`fm1_bluetooth_configure`,
`fm1_ble_set_role`, `fm1_ble_scan`, the `FM1_BLE_*` states and errors) is declared in
`sdk/include/fm1.h`, i.e. the closed binary. Their GPL app holds only the UI and a retry loop
(`app/project.c:859-865`), which is still the best available specification for the state machine.

The implementable route is JieLi's own `cpu/wl82/liba/btstack.a` + `btctrler.a` (Apache-2.0, the same
SDK this tree already takes `uboot.boot` / `cfg_tool.bin` / `eq_cfg_hw.bin` from), driven by our code.
One build-system consequence: those archives are **LLVM bitcode**, not object code, so the link needs
JieLi's gold plugin (`ld --plugin .../LLVMgold.so`) — `tools/bt_spike.sh` already does exactly that,
which is why it is the right place to start rather than linking code first.

### 15.2 The shape the adapter takes

The two rules it is built on, both already true elsewhere in this tree:

- **One writer for `midi_in_q`.** `midi_enqueue()` is called only from the TIMER5 ISR (`usb_poll`,
  `uart_midi_poll`, `smk_panel`). So the GATT write callback, which runs in the stack's own task, does
  not enqueue: it copies the payload into a 64-byte ring and the TIMER5 ISR drains it, exactly as
  `midi_uart.c` does with the UART DMA ring.
- **No radio work in an ISR.** `midi_out_event()` (the audio ISR) only queues a packet; the notify is
  built and sent from the stack's task. A GATT notify from the audio ISR would stall the render.

Already in place, needing no new buffers: `midi_enqueue(pkt, source)` (`usb.c:151`) takes a per-slot
source tag (1 USB, 2 TRS, 3 panel, 4 BLE) on a 64-entry ring, and `midi_out_event()` already fans one
packet to TRS and USB — BLE is a third branch with a 16-packet ring of its own.

RAM the adapter itself wants: 64 B ring + 64 B packet ring + indices and counters ≈ **150 B**, plus
the stack's heap and task stack (below). Flash: ~600 B of code when `SKM_BLE_MIDI=1`.

**Not done, deliberately:** `BLE` is not a clock source. `song.g[G_CLOCK]` indexes
`params.c` `N_CLOCK` `{"INT","USB","TRS"}`, so clock messages arrive from a phone and are not used for
tempo (notes and CC are). Adding `"BLE"` to that list is the follow-up, and it needs the companion's
own copy of the list in the same commit — which is why it is not in this change.

### 15.3 The bill

**Measured, not estimated** (`tools/bt_spike.sh`, AC79 SDK + JieLi toolchain under an amd64 container,
2026-10-28). The spike closes over `btctrler_task_init()` + `btstack_init()`; archive members are pulled
only to resolve those, so these are transitive-closure figures, not the whole 317 MB library:

| link | code | const | bss/stack | total | still undefined |
|---|---|---|---|---|---|
| `bt_only` — `btstack.a` + `btctrler.a` | **402.1 KB** | 140.3 KB | 153.1 KB | **695.4 KB** | 279 syms |
| `bt_with_os` — + `system` `event` `cpu` `common_lib` `fs` `cfg_tool` | **623.7 KB** | 343.1 KB | 176.1 KB | **1142.9 KB** | 292 syms |

Against the slot: **581,564 B total, 888 B free.** `bt_only` is 695 KB — it does not fit in the app
slot *as a whole*, before a single byte of synth, UI or sample data. The code section alone (402 KB) is
41× the entire cut catalogue.

| budget | free now | measured ask (the BLE-minimum `bt_only` link) | paid by |
|---|---|---|---|
| Flash | **888 B** | **402.1 KB** code + **140.3 KB** const | the cut catalogue is **110,277 B**: ~3.6× short on the code section alone |
| RAM | **340 B** + 644 B pool | **153.1 KB** bss/stack | the whole polyphony row (R1+R2+R4+R5+R6 = **138,224 B**) is still 15 KB short, and then the synth has one voice |
| CPU | `SKM_VOICE_LOAD` 6,312 of an unknown absolute | BT ISRs share the core with the 24 MHz render | unmeasured (§14.6) |

Two facts that decide the shape of the answer:

1. **`btstack_init()` drags the classic profiles in.** 329 A2DP / AVRCP / HFP / HSP / SBC / RFCOMM
   symbols are inside the `bt_only` closure. Dropping Bluetooth *audio* from our side does not drop it
   from the stack: one init function references all of them, so `--gc-sections` cannot collect them.
2. **There is no BLE-only archive to link instead.** `cpu/wl82/liba` holds exactly one `btstack.a` and
   one `btctrler.a`; there is no `btstack_le.a` or config-stripped variant.

**Conclusion: BLE-MIDI cannot be linked into this image, at any catalogue depth.** It is not a
"cut the samples to make room" problem — the RAM bill is 200× over and the flash bill is larger than
the slot that holds the synth, the UI and the samples together. It is a different-product problem:
either a firmware image that is *only* the MIDI side (no engine, no samples, no GUI budget), or a
BLE-only stack build obtained from JieLi. The transport in §15.2 is already written and tested, so
the day either exists it is a link-line change plus `ble_midi_set_notify()`.

### 15.4 Gate: result

1. ~~Run `tools/bt_spike.sh`~~ → done, 15.3 above. The environment it needed: no Docker Desktop on this
   machine, so `colima start --arch aarch64 --vm-type=vz --vz-rosetta` (the VM is aarch64 and Rosetta
   translates the `--platform linux/amd64` container; an x86_64 *VM* needs qemu and is not the way).
   Then `AC79_SDK=~/jl-ref/fw-AC79_AIoT_SDK JIELI_TOOLCHAIN=~/.jieli/toolchain tools/bt_spike.sh`.
2. Read `cpu_pct` on the band mix (§14.6) — still open, and now moot for BLE-MIDI's own cost.
3. Pick the funding row — **no row exists that funds this.** 15.3 closes the question.

`SKM_BLE_MIDI` stays 0. It is not waiting on a budget line any more; it is waiting on a product
decision.

## 16. Where the slot stands after the TEST MODE / wheel-calibration overflow

The uncommitted TEST MODE + wheel calibration work did not fit: `region XIP overflowed by 3928 bytes`
against `XIP 0x8DFBC` (581,564 B). HEAD itself fits with **736 B** free, so the two features had to be
paid for. Measured, in this order:

| step | delta | XIP used | free |
|---|---|---|---|
| HEAD (`3e9f872`) | — | 580,828 | 736 |
| + TEST MODE + wheel calibration (uncommitted) | +3,928 | 585,492 | **−3,928 over** |
| `SKM_TEST_MODE=0` (§4, the QA screen only) | −2,016 | 583,476 | −1,912 over |
| `--gc-sections` on the app link | −304 | 583,172 | −1,608 over |
| `SKM_ICON24=0` (§4 C4b) | −3,264 | **579,908** | **+1,656** |

`--gc-sections` is new and was the surprise: the loader has always linked with it, the app never did,
even though `CFLAGS` emits a section per function. It bought 304 B — most of this image is reachable
from one translation unit, so there is no dead code to collect, only the unreachable bodies that a
`#if` leaves behind. It stays on: it is what makes the next `#if`-ed feature free rather than paid for.

Two rows of the catalogue were taken, both reversible from the build line:

- **`SKM_TEST_MODE=0`** (default): the panel's QA screen (`ui_input.c` `sk_test_mode`, its 6 helpers and
  the `tm` struct) is compiled out. The MENU row stays and is inert — moving `MI_TEST` would shift
  `MI_ABOUT`, break `tests/menu_sync_test.py` and change SYSTEM's row height (`ui_menu.c mr_h`, 9 rows
  at 16 px → 8 at 21 px). A bench build sets `SKM_TEST_MODE=1`.
- **`SKM_ICON24=0`** (default): the 24 px atlas is not generated and no C names it. The header battery
  and the USB plug drop to 16 px (the battery was the glyph the 24 px set existed for: wide and short,
  at 16 px its body is 12 x 6), and the FX map's eight effect cells drop with it — those eight are now
  in `BIG_DEFAULT`, so they are 16 px cells rather than falling back to 12. `SKM_ICON24=1` restores it
  for 4,228 B.

Wheel calibration itself was not touched: it is the feature that has to ship. Its bill is the
difference between the first and second rows, and it is paid for by the two cuts above.

Verified after the cuts: `./build.sh` links `studiokeys.bin` at **579,908 B** (1,656 B free);
`tests/ui_render` 123 screens x 16 palettes, 0 lint findings and 1,990,505 alignment measurements with
0 off by 1 px; `tests/menu_sync_test.py` 25 rows and the companion's MENU still in sync; `tests/ui_test`
all passed.
