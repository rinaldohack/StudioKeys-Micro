# StudioKEYS VST3 — Windows / Linux

Mac arm64 VST3 is the current ship path. These notes scaffold the other OS builds
from the same `plugin/StudioKEYS` CMake project (iPlug2).

## Prerequisites

- Clone iPlug2 into `plugin/third_party/iPlug2` (same as Mac).
- Install the VST3 SDK under iPlug2 Dependencies (already used on Mac).
- **Windows:** VS 2019+ with C++ desktop, CMake ≥ 3.14, Ninja optional.
- **Linux:** g++/clang, CMake, ALSA/JACK for RtAudio if building APP later.
  NanoVG/GL or NanoVG/Cairo per iPlug2 docs.

## Configure (presets)

```bash
cd plugin/StudioKEYS
cmake --preset linux-release   # → build-linux/
cmake --preset win-release     # → build-win/  (on Windows)
cmake --preset mac-release     # → build/
```

Or classic:

```bash
cmake -B build-win -G "Visual Studio 17 2022" -A x64
cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
```

APP format stays off unless full Xcode.app is installed (`STUDIOKEYS_BUILD_APP`).
CLT `ibtool` alone is not enough. Force with:

```bash
cmake -B build -DSTUDIOKEYS_BUILD_APP=ON
```

Headless host audio (no GUI) without APP:

```bash
cc -O2 -DNVOICE=128 -Ibuild/gen -Ifirmware/src -Ihost \
  host/sk_host.c host/sk_headless_cli.c -o build/host/sk_headless_cli -lm
./build/host/sk_headless_cli build/host/sk_headless.wav
```

## Build VST3 only

```bash
cmake --build --preset linux-vst3
cmake --build --preset win-vst3
cmake --build --preset mac-vst3
```

## USB MIDI link (cable 3)

- **macOS:** CoreMIDI in `UsbMidiLink.h` (open on plugin Link).
- **Windows:** WinMM device scan by name is compiled in; sysex open/send still TODO
  (plugin falls back to in-process SKL/SKS until `midiInOpen` / `midiOutLongMsg` land).
- **Linux:** ALSA sequencer stub message until wired.

## Preset hash (phase 2 check)

On each OS, after a DAW loads the VST3 and renders the known middle-C host
preset, hash the bounce and compare to `host/preset_hash.sha256` (host-side
float dump) or store a DAW-specific gold beside it:

```
host/preset_hash.sha256          # Mac host float gold (check_preset_hash.sh)
host/preset_hash.win.sha256      # stub — replace PLACEHOLDER after Win bounce
host/preset_hash.linux.sha256    # stub — replace PLACEHOLDER after Linux bounce
```

`host/check_phase1.sh` already runs the Mac host gold. Win/Linux DAW hashes
stay open until those binaries are built on CI or a local machine.
