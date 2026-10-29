# StudioKEYS Micro

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**StudioKEYS Micro** — multi-engine synthesizer firmware for the SMK-37 (Elite), with a
native VST3 companion under `plugin/StudioKEYS/`. Based on
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments
(GPL-3.0). Behavior and on-disk formats from that lineage stay; product naming is StudioKEYS / StudioKEYS Micro.

**TL;DR:** Build with [BUILDING.md](BUILDING.md). The companion plugin is VST3-only for now.

Copyright in this tree is held jointly: **Rinaldo Jonathan (LangitCinta! StageLabs)** for the
StudioKEYS Micro work — the VST3 companion, the native host bridge, the SMK-37 surface work and
the UI revamp — and **Hügelton Instruments (Leo Kuroshita)** for the Felucca lineage this tree
derives from. Every source file carries both notices; the bundled fonts, samples and ported DSP
keep their own licences ([LICENSES/](LICENSES/)).

## Features

- **Thirteen engines** (below), each with its own factory presets
- **Four tracks**, one synth part each with its own engine and sound (drums are the DRUM engine);
  48 voices in one pool shared between them, each charged what its engine costs (a light patch
  plays all 48, a seven-saw supersaw pad 12). ALGORITHM selects the track on every page
- **Sequencer:** 64 steps per track with chords, ties, accent, slide, per-step chance and ratchets
  (a step played 2, 3 or 4 times in its length); a piano roll of the steps; a drum grid (white keys =
  steps, black keys = lanes); automation (formerly motion):
  recording of knob moves per step (an automation icon marks the cards it drives); live loop recording
  with overdub; REC held on the SEQ pages clears the sequence (it asks first, and SAVE held undoes it);
  divisions listed by length, 4 bars to 1/32; loading a sound never touches your patterns
- **Songs:** chain patterns A–D
- **Chord keys:** one finger plays an in-key chord (triads or sevenths of the scale, or fixed chord
  shapes), with voicings; on the keys, MIDI in, recording and the arpeggiator
- **Arpeggiator** with REPEAT and a beat LED, 16 scales with a white-key mode, glide,
  MONO / LEGATO / UNISON
- **Modulation matrix:** 4 slots per track, MIDI controllers as sources
- **Effects:** distortion and the SLICER per track; chorus, delay and reverb sends (the reverb as
  ROOM or SPRING); master limiter
- **FX layer:** hold FX for repeat, reverse, filter sweeps, tape stop, freeze and a harmonizer
  (OCT UP / OCT DN with shimmer), and mutes on the black keys; MENU > FX LATCH makes them toggle, so
  nothing has to stay held
- **Quick layers:** hold FX, GLO, SCL or EDIT for shortcuts on the keys and knobs, or double-tap it to
  keep the layer open; one-step undo (SAVE held); REC on every page; OCT+ confirms, OCT− goes back;
  what can be pressed breathes softly instead of blinking
- **Presets:** factory presets, 100 user preset slots (U01..U100) and 4 projects, named on the device (an FM6
  sound keeps its own patch in both); projects from every earlier version load. PATCH opens the
  browser and its first knob loads the next sound; on every other page knob *n* is card *n*, left to
  right (so the SEQ pages' first card is the step cursor and the tempo lives on GLO > GLOBAL)
- **Screen:** flat UI with Inter Tight and Fukiai icons, FLAT or LINE style, chassis palettes;
  tracks numbered 1–4 on small cushions;
  MENU > LARGE for tall knob cards with larger labels and values; the menu in four tabs
- **USB:** class-compliant MIDI in and out ("StudioKEYS Micro"), and a 44.1 kHz stereo audio input that
  records the master output on the computer, no driver needed (at a fixed level with MENU > USB LEVEL FIXED;
  on macOS 13–15, set MENU > USB SERIAL to OFF so the audio input appears)
- **MIDI:** USB and device MIDI; with ROUT CH1-4, channels 1–4 play tracks 1–4 and 5–16 are ignored
  (ROUT SEL: every channel plays the selected track), and the keys send on the track's channel; pitch
  bend, sustain, panic; clock from internal, USB or TRS
- **Plugin:** native VST3 companion (`plugin/StudioKEYS/`); the device bring-up host lives under `host/`

## Controls

- Tap a page button for its page, again for the next; HOME returns home
- Hold FX, GLO, SCL or EDIT for its quick layer; hold SAVE to undo, HOME for the menu, SEQ for the song
- Double-tap FX, GLO, SCL or EDIT to keep its layer open without holding; tap it again to close
- Hold REC on SEQ > STEP (or the drum grid), PATTERN or CHANCE to clear the track's sequence: OCT+ clears, OCT− cancels
- On action pages and in dialogs, OCT+ does it and OCT− goes back; in the menu, OCT+ / OCT− change the value and HOME closes it
- Save a sound: stop, tap SAVE, pick a slot with KNOB 1, then OCT+ and OCT+ again (name it with the keys)

## Menu

Hold **HOME** for the menu. It is in four tabs: **DISPLAY** (COLOR to LEDS), **CONTROL** (HOLD to
BPM LOCK), **AUDIO** (SPEAKER EQ, USB LEVEL) and **SYSTEM** (USB SERIAL, HARDWARE CALIBRATION,
CALIBRATE PITCH BEND, CALIBRATE MOD WHEEL, FLAPPY, TETRIS, UPDATE, TEST MODE, ABOUT — ABOUT is the tab's
last row). ALGORITHM moves between the tabs and PRESETS through the rows of one; any of KNOB 1–4, or
OCT+ / OCT−, changes the value (OCT+ opens HARDWARE CALIBRATION, the two wheel calibrations, TEST MODE,
UPDATE and ABOUT); press HOME to close the menu.

- **CALIBRATE PITCH BEND / CALIBRATE MOD WHEEL** (SYSTEM, after HARDWARE CALIBRATION): measure this
  unit's own wheels, in place of the factory numbers. Pitch bend: let the wheel return (its resting count
  is the bend's centre), then push it to the top and pull it to the bottom, each recorded with `>`; the
  screen shows the raw pot count live and what the earlier steps took. The mod wheel has no spring, so
  only its bottom (CC1 0) and top (CC1 127) are recorded. Take each end to where the wiper stops, gently:
  the wizard refuses a travel under 16 counts, and counts past a learned end (a pot that drifts further
  later) are clamped, not extrapolated. The record goes in its own flash object (two sectors at
  0xFE000, `OBJ_WHEELCAL`), so importing settings, restoring a backup or an update never overwrites it;
  a write is deferred to the first transport stop while the sequencer plays. `OCT−` / `<` cancels.
  On the VST the two rows answer with a modal instead: a plug-in has no pot and no spring to measure.

- **TEST MODE** (SYSTEM, the row above ABOUT): the panel's QA screen. It shows all 37 keys,
  the 8 knobs, the 4 faders, pitch / mod / sustain, the 18 buttons and the 16 pads, each lighting while it
  moves and once touched staying marked, with a count per section; every key and pad still plays. OCT+ asks
  START TEST MODE? first. There is no way out but a power cycle (restart the keyboard); nothing is stored.
  On the VST the row answers with a modal instead: the companion has no panel to test.

- **COLOR:** the palette (chassis themes and screen roles)
- **STYLE:** FLAT (filled cards) or LINE (areas divided by thin lines)
- **LARGE:** OFF or ON: on HOME and the value pages the four knob cards grow tall, marked K1–K4, with
  larger labels and values about twice the size; the graph below becomes a strip. Lists, the piano roll,
  the drum grid and the quick layers keep their layout with larger labels
- **ANIM:** ON or OFF; OFF shows every change at once, without rolling digits or a gliding piano roll
- **LEDS:** OFF (no glow), DIM LO, DIM HI (default) or INV (the idle LEDs lit, the active ones dark).
  What can be pressed breathes up to about 60 % of a lit LED, about 30 % with DIM LO
- **HOLD:** how long FX, GLO, SCL or EDIT is held before its map shows
- **KNOB ACCEL:** OFF (one step per click) or ON: a fast, steady turn of a wide value moves 2 to 4 steps
  per click; lists never jump
- **FX LATCH:** ON, FX + an effect key turns the effect on until pressed again, the knob macros stay
  where you leave them, and FX + OCT− turns everything off
- **BPM LOCK:** ON, SELECT no longer changes the tempo; hold GLO and turn SELECT, tap F4 in the GLO
  layer or use GLO > GLOBAL instead
- **SPEAKER EQ:** FLAT, LOWCUT or BASS+, a tone setting for the built-in speaker (not a switch). It also
  shapes the headphone out and USB audio, so keep it on FLAT when recording. The firmware cannot turn
  the speaker off, but headphones in the jack do
- **USB LEVEL:** MASTER (the MASTER knob sets the USB audio level too) or FIXED (USB always at full
  level, MASTER sets only the speaker and headphones)
- **USB SERIAL:** ON or OFF, applied when the menu closes (the device reconnects). OFF leaves out the
  serial console, a developer tool, so the device is a plain audio + MIDI device; this lets macOS 13–15
  see its USB audio input. MIDI and the reference editor work either way
- **HARDWARE CALIBRATION** and **ABOUT**
- **DIAGNOSTICS** (SYSTEM page, the CPU column, OCT+): read-only health of the instrument: the measured
  I2S rate and its ppm against 44,100, the accepted measurement windows, the render's CPU and worst
  interrupt time, late DMA buffers, the boot count with the failed ones and where the previous boot's
  main loop was, and why it restarted. OCT− goes back; nothing on the page can be changed.

## Engines

In the order the device lists them:

- **ANALOG**: virtual analog; two oscillators, noise, drive, resonant low-pass filter
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track, macros on the
  device, an algorithm chart on screen; SLOT picks a factory patch (F1–F8) or the track's own (OWN)
- **PHASE**: phase distortion (ported from CrispyZebra)
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **SAMPLE**: multisampled instruments and 3 user sample slots
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN**: granular textures from the built-in samples or a user slot
- **PHYS**: physical models: modal resonators, strings, struck membranes, sympathetic strings
- **NOISE**: noise from analog to digital: colours, crackle, shift-register and metallic tones
- **SLICE**: a drum break, a piano note (both built in) or your own sample cut into slices, one per key;
  set the slices by hand on the SLICES page
- **DRUM**: an 8-lane kit of synthesized drum voices on the General MIDI key map. KIT picks the
  standard kit or one of five virtual-analog (VA) kits in the manner of
  classic analog drum machines (the numbers are a hint), every lane its own voice: **80** (deep sine kick with a long decay, noisy snare,
  six-square hats), **10** (swept kick, white-noise snare, long-tailed claps, a cymbal), **66** (soft
  round kick, bright snare, noise hats, a conga), **55** (dropping kick, high metal hats, a metal bell)
  and **77** (swelling kick, multi-burst claps, claves, a cymbal). All of them are synthesized, no samples

The DIGITAL engine of 0.9 has been replaced by FM6: projects and presets with DIGITAL sounds load
as FM6 sounds converted from them. The SAMPLE engine's PERC kit was removed in 1.0.2: sounds and
projects that used it load as the DRUM engine's kit, on the same key map. FM6's patch bank (the B
slots) was removed in 1.0.3: user presets keep their own FM6 patch, and presets that used a B slot get
that patch on the first start of 1.0.3. SLICE gained a second built-in sound in 1.0.4, PIANO (the SAMPLE
engine's middle C), next to BREAK. Since 1.0.4 a missing sample (an empty user slot, or a set missing
from the build) plays a plain sine at the note's pitch on SAMPLE, GRAIN and SLICE, and the screen says
NO SAMPLE once. DRUM's KIT variants HAND, CYM and H+CYM were retired in 1.0.5: sounds and projects that
used them play the 66, 10 and 77 kits.

**SLICER** (FX page, every track): a tempo-synced 16-step gate or stutter, with 16 patterns.

## Scale keyboard

On the **SCL** page, set **QNT** to WHITE to play the selected scale using only the
white keys (SNAP keeps every key and rounds it down to the scale). C4 plays **ROOT**; consecutive white keys play consecutive scale notes
above and below it. Black keys are silent, including during live recording and
step entry. **TRN** transposes the resulting notes; the octave buttons shift them
by full octaves. Set QNT to OFF for the normal chromatic keyboard. QNT SEQ snaps the keys like SNAP and
also maps the sequenced notes onto the current ROOT / SCALE as they play (the steps are not changed; drum
kits are never quantized).

Available scales: chromatic (CHR), major (MAJ), natural minor (MIN), Dorian (DOR),
Mixolydian (MIX), major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor
(HARM), Phrygian (PHRY), Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL),
minor blues (BLUES), whole tone (WHOLE), half-whole diminished (DIMHW), and
whole-half diminished (DIMWH). Scales with other than seven notes continue across
the white keys without repeating notes; their roots need not fall on every C key.
Drum kits and incoming MIDI keep their own note mapping.

Press **SCL** again for the **CHORD** page: CHRD picks the chord keys (OFF, the scale's triads or
sevenths, or a fixed shape) and VOIC the voicing.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `plugin/StudioKEYS/` | VST3 companion shell |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | UI font, icon names, CC0 instrument samples |
| `host/` | native host library the companion links: device bring-up, 44.1 kHz render, 240×240 framebuffer, NOR image |
| `tests/` | tests that run on the build machine |
| `LICENSES/` | licence texts of the bundled fonts, icons, ported DSP and SDK files |

## Credits

- **LangitCinta! StageLabs / Rinaldo Jonathan** — StudioKEYS Micro product work: the VST3 companion
  (`plugin/StudioKEYS/`), the native host bridge (`host/`), the SMK-37 surface, the UI revamp
  (P1–P20) and this repository's naming / packaging
- **AI Co-Authors & Collaboration** — Developed with Cursor Agent across multiple models:
  DeepSeek Flash, Claude, Google Gemini, and Cursor Auto/OpenRouter
  (each commit trailer carries explicit `Co-authored-by: Cursor Agent (<model>)` attribution)
- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  StudioKEYS (the firmware lineage this tree is based on); the PHASE engine's waveforms (a C port of the oscillator of
  [CrispyZebra](https://github.com/hugelton/CrispyZebra), GPL-3.0); the DRUM voices and kits; the Hügelton Sample
  Pack (the drum samples, GPL-3.0-only, not CC0); the [Fukiai](https://github.com/hugelton/Fukiai) icon
  font ([MIT](LICENSES/MIT-Fukiai.txt))
- Fonts: [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors, [SIL OFL 1.1](LICENSES/OFL-InterTight.txt);
  the browser emulator's labels: [DotGothic16](https://github.com/fontworks-fonts/DotGothic16) by The DotGothic16 Project Authors, [SIL OFL 1.1](LICENSES/OFL-DotGothic16.txt)
- Samples: [Versilian Studios](https://versilian-studios.com/) [VSCO-2 Community Edition](https://github.com/sgossner/VSCO-2-CE) and [VCSL](https://github.com/sgossner/VCSL), CC0 1.0: the SAMPLE sets, also SLICE's PIANO ([attribution](assets/samples-cc0/ATTRIBUTION.txt))
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- PHYS engine: models ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](LICENSES/MIT-DaisySP.txt)) and from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) code ([MIT](LICENSES/MIT-Rings.txt))
- FM6 engine: msfa from [Dexed](https://github.com/asb2m10/dexed) by Google Inc. and Pascal Gauthier ([Apache-2.0](LICENSES/Apache-2.0-msfa.txt))
- Browser emulator: after [X0X](https://github.com/charlesvestal/fm1-x0x) by [charlesvestal](https://github.com/charlesvestal) (GPL-3.0), a Felucca fork whose browser build showed the way
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)

## Licence

Free software: [GPL-3.0-only](LICENSE), the Hügelton Sample Pack included. The bundled fonts and the
ported DSP keep their own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. StudioKEYS Micro is not affiliated with or
endorsed by them. StudioKEYS and StudioKEYS Micro are product names of LangitCinta! StageLabs;
Felucca is the GPL-3.0 firmware lineage by Hügelton Instruments this tree derives from.

Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS Micro, the VST3
companion and the surface / UI work.
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments — Felucca, the firmware
lineage this tree is based on.
Both notices sit in every source file; neither is dropped by redistribution.
