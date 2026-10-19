# Building StudioKEYS Micro

Firmware for **StudioKEYS Micro**. Build artifact names such as `studiokeys.bin` /
`studiokeys.fwsc` stay for scripts; the product name on screen and USB is StudioKEYS Micro.
(Engine lineage includes StudioKEYS GPL code; that is not the product name.)

The build makes four files in `build/`:

| File | What |
| --- | --- |
| `studiokeys.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `studiokeys.fwsc` | the installable package (app + loader) |
| `studiokeys-flash.bin` | the package's flash part alone (0x93000 B): what a programmer writes at flash address 0 |

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time). `build.sh` picks the first `python3`/`python3.X` on `PATH` that has
  both; set `PYTHON=/path/to/python3` to choose one yourself (Xcode's `python3` often has neither).
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for tooling).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.0` makes a release build: the package identity becomes `SKM_910`
and the version string `v1.0`; the package is `build/studiokeys-1.0.fwsc`, and
`build/release-1.0/` holds what a release ships: the package, the app
(`studiokeys-1.0-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

Build options (environment, `0` or `1`; defaults in `firmware/src/studiokeys.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `SKM_FLASH` | 1 | settings, presets and projects in flash |
| `SKM_OTA` | 1 | update entry (needs `SKM_FLASH`) |
| `SKM_CDC` | 1 | USB serial console |
| `SKM_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `SKM_UAC` | 1 | USB audio input (the master output, 44.1 kHz stereo) |
| `SKM_UART` | 1 | TRS MIDI IN |
| `SKM_SLICE` | 1 | the SLICE engine |
| `SKM_ICONS` | 1 | parameter icons on the knob cards |
| `SKM_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |

`SKM_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine and no PIANO in SLICE (the PIANO set's middle C): they play a plain sine
at the note's pitch instead, and the screen says NO SAMPLE.

## Native host

`host/` is the firmware's C compiled for the host process; the companion plugin links it.
`host/studiokeys_host.c` builds the firmware's sources as the host tests do, against a simulated panel,
and `host/native_check.c` renders a heavy song natively to a float32 reference file. Not emulated: USB (audio,
MIDI out, the editor, updates), TRS MIDI and the user sample slots.

## Tests

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests (and, with Emscripten too, the browser emulator's). Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, the
DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/studiokeys.fwsc
python3 tools/fm1_install.py --info          # identity of the connected device
```

Or install your own build with the CLI installer (`tools/fm1_install.py`, needs a MIDI port):

```
python3 tools/fm1_install.py build/studiokeys.fwsc --yes --port <MIDI port>
```

Installing firmware is at your own risk.

---

StudioKEYS Micro firmware and StudioKEYS companion © 2026 Rinaldo Jonathan (LangitCinta! StageLabs) and Hügelton Instruments. The companion VST3, the SMK-37 surface and its UI are Rinaldo Jonathan's work; the GPL engine lineage is Hügelton Instruments'. See `LICENSING.md`.
