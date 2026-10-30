# StudioKEYS licensing

StudioKEYS is free software, licensed under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). That covers the code and its own assets. A few
bundled or ported parts keep their own licences; they are listed below, and their licence
texts are in `LICENSES/`.

Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS Micro, the VST3 companion,
the native host bridge and the surface / UI work.
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments — the Felucca lineage this
tree is based on.

Notices to both appear in the sources this tree inherits; the files written for StudioKEYS carry the
StageLabs notice. The GPL-3.0-only terms below cover the tree as a whole.

## What is GPL-3.0-only

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the native host: `host/`
- the host tests: `tests/`

Three source files are ports and keep the licence of their originals:
`firmware/src/phys_dsp.c` (DaisySP, MIT), `firmware/src/phys_symp.c` (Rings, MIT) and
`firmware/src/fm6_core.c` (msfa, Apache-2.0). The firmware built with them is GPL-3.0-only as
a whole.

You may use, study, change and share StudioKEYS under the GPL. If you distribute StudioKEYS, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified StudioKEYS inside.

## LangitCinta! StageLabs' own work

All by Rinaldo Jonathan (LangitCinta! StageLabs) in collaboration with Cursor AI Agents
(DeepSeek Flash, Claude, Google Gemini, and Cursor Auto/OpenRouter), in this tree:

| What | Licence | Where |
| --- | --- | --- |
| StudioKEYS Micro: the VST3 companion (desks, chrome, Program Bay, undo, motion window, easter eggs) | GPL-3.0-only | `plugin/StudioKEYS/` |
| The native host bridge the companion links: device bring-up, render, framebuffer, NOR image | GPL-3.0-only | `host/` |
| The SMK-37 surface and the UI revamp (P1..P20): pages, pads, legend, performance mode, split, metronome, chords, motion, games | GPL-3.0-only | `firmware/src/`, `tests/` |
| The build / packaging scripts and the desktop tools | GPL-3.0-only | `build.sh`, `tools/` |

## Hügelton Instruments' own work

Felucca, the firmware lineage this tree derives from. All by Hügelton Instruments (Leo Kuroshita), in this tree:

| What | Licence | Where |
| --- | --- | --- |
| Felucca, the firmware lineage this tree derives from: its engines, DSP, UI tables and tests | GPL-3.0-only | `LICENSE` |
| The PHASE engine's waveforms: a C port of the oscillator of CrispyZebra (<https://github.com/hugelton/CrispyZebra>) | GPL-3.0 | `firmware/src/eng_phase.c` |
| The DRUM voices and kit | GPL-3.0-only | `firmware/src/drum_voice.c`, `firmware/src/eng_drum.c` |
| The Hügelton Sample Pack: StudioKEYS's drum sounds, made by `tools/gen_waves.py` (not CC0) | GPL-3.0-only | `tools/gen_waves.py` |
| The controls picture | GPL-3.0-only | `docs/controls.jpg` |
| The Fukiai icon font (<https://github.com/hugelton/Fukiai>): the firmware's icons (rasterised at build time by `tools/gen_aa_icons.py`) | MIT | `assets/fonts/fukiai.ttf`, `LICENSES/MIT-Fukiai.txt` |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Inter Tight font by The Inter Project Authors: the UI text, rasterised into the firmware at build time (`tools/gen_aa_font.py`; the generated tables are not offered as a font, and the font declares no Reserved Font Name) | SIL OFL 1.1 | `assets/fonts/InterTight[wght].ttf`, `LICENSES/OFL-InterTight.txt` (also `assets/fonts/OFL.txt`) |
| Instrument samples: FLUTE and SAX sets (Versilian Studios VSCO-2 Community Edition and VCSL) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| Instrument samples: STRINGS, HORN and other SAMPLE sets; SLICE material | GPL-3.0-only (own recordings) | `tools/gen_samples.py` |
| DaisySP by Electrosmith, Corp and Emilie Gillet (<https://github.com/electro-smith/DaisySP>): the PHYS engine's modal and string models and the resonator, ported to fixed point | MIT | `firmware/src/phys_dsp.c`, `LICENSES/MIT-DaisySP.txt` |
| Rings by Emilie Gillet (<https://github.com/pichenettes/eurorack>): the PHYS engine's sympathetic strings, ported to fixed point | MIT | `firmware/src/phys_symp.c`, `LICENSES/MIT-Rings.txt` |
| msfa by Google Inc. and Pascal Gauthier, from Dexed (<https://github.com/asb2m10/dexed>): the FM6 engine's synthesis, ported to integer C (Dexed itself is GPL-3.0; only msfa is used; the FM6 factory patches are StudioKEYS's own) | Apache-2.0 | `firmware/src/fm6_core.c`, `LICENSES/Apache-2.0-msfa.txt` |
| klattsch by Tony Gies (<https://github.com/tgies/klattsch>): design reference for the VOICE engine; no code copied. Formant data from Klatt (1980) / Hillenbrand et al. (1995) | MIT (klattsch) | credit only |
| SMK37 OPEN SDK by M-VAVE (<https://github.com/m-vave/smk37_open>): design reference for the pads' rest-level tracking (`padgate` in `app/board/scan.c`); no code copied. StudioKEYS tracks the level by events (a boot run, a confirmed rise, a slew-limited fall) rather than their leaky counter, and sets its thresholds from it instead of a factory calibration block | GPL-3.0 with the GPL-3.0 linking exception | credit only, `firmware/hal/fm1_input.h` |
| X0X by charlesvestal (<https://github.com/charlesvestal/fm1-x0x>), a Felucca fork: the design of the native host (the device clock driven by its audio, the exports); its files credit it in their headers | GPL-3.0 | `host/` |
| JieLi AC79 SDK by JieLi Technology: three of its files go into every `.fwsc` package (below); none are in this tree | Apache-2.0 | `LICENSES/Apache-2.0.txt` |

On the device, HOME held > ABOUT opens the information screen; turning PRESETS scrolls on into
CREDITS, a short list of these authors, licences and source URLs.

## JieLi SDK files in the packages (Apache-2.0)

A `.fwsc` package made by `tools/build.py` (with `tools/fm1pkg_make.py`; this is the package the
web installer installs) holds three unmodified files from the JieLi AC79 SDK
(<https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>, `cpu/wl82/tools/`). They are read from your SDK
checkout at build time (see BUILDING.md); no SDK files are in this tree.

| File in the package | What it is |
| --- | --- |
| `uboot.boot` | the first-stage boot loader (SPL) |
| `cfg_tool.bin` | the chip configuration block |
| `eq_cfg_hw.bin` | the default hardware EQ table |

They are licensed under the Apache License, Version 2.0 (`LICENSES/Apache-2.0.txt`), not under
the GPL, and their copyright stays with JieLi Technology. The SDK has no NOTICE file. Apache-2.0
files may be distributed together with GPL-3.0 code; StudioKEYS itself stays GPL-3.0-only.

Every distribution of a package carries the licence texts with it: the release folder
(`tools/build.py --release`) and the site (`web/make_site.py`, next to the package in
`firmware/` and linked from the installer page).

Linking the radio (`SPACE_BUDGET.md` §15) would add two more files from the same checkout to the
link, not to this tree: `cpu/wl82/liba/btstack.a` and `btctrler.a`, both Apache-2.0. The licence
texts a package ships already cover them.

## No vendor material

No M-VAVE material is part of StudioKEYS. The firmware links no vendor code and contains no vendor
data, and the packages hold only StudioKEYS and the three SDK files above. One mechanism follows a
design published in M-VAVE's own SMK37 OPEN firmware (GPL-3.0 with a linking exception), the pads'
rest-level tracking; it is credited under Third-party material above, and no code or data from that
tree is in this one. The installer's
"Return to official V15" holds only the official file's size and SHA-256: you select the
official firmware file you downloaded yourself, and it is checked and installed in your browser,
never uploaded or redistributed.

## Contributions

Contributions are welcome under GPL-3.0-only.

## Trademarks

"StudioKEYS" and "StudioKEYS Micro" are product names of LangitCinta! StageLabs.
"Hügelton Instruments" and "Felucca" are names of Hügelton Instruments, whose GPL-3.0 firmware
lineage this tree derives from.

"M-VAVE" and "FM-1" are trademarks of their respective owners. StudioKEYS is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

The Wi-Fi radio is never enabled. The Bluetooth radio is not enabled in this image either: the
BLE-MIDI codec and adapter exist (`firmware/src/ble_midi.c`, `SKM_BLE_MIDI=0`) but no stack is
linked, which is why the BT key answers `BT: NO RADIO YET`.

When the stack is linked, the radio is used for **BLE-MIDI only** — one GATT server, no Bluetooth
audio profile, no serial profile, no Wi-Fi. `SPACE_BUDGET.md` §15 is the budget that has to pass
before any of it is linked.
