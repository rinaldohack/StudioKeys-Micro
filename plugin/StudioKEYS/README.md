# StudioKEYS Micro (VST3 + APP)

iPlug2 companion for **StudioKEYS Micro**. Links `../../host` (`sk_host`) for
firmware audio, framebuffer, NOR, and lock. UI covers plan phases 3–8.

## Layout

| Path | Role |
|------|------|
| `StudioKEYS.cpp` / `.h` | Plugin shell, desk, panel, librarian, lock UI |
| `SkPanelControls.h` | Screen, pads, encoders, faders, hold buttons |
| `HostEngine.h` | `sk_host` render + 44.1 kHz → DAW resample |
| `LockSession.h` | SKL cable-3 lock (HELLO/CRC/CHOOSE/EVENT) + one-instance seat |
| `Librarian.h` | Hardware-ready / VST-only libraries + NOR helpers |
| `ImportBay.h` | DX7 bulk → FM6_PACKED; Virus TI/classic → ANALOG pack; framed export |
| `DeskParams.h` | Generated engine/page tables from firmware |
| `MidiCables.h` | Four USB-MIDI cables |
| `../third_party/iPlug2` | iPlug2 SDK |

## Build

```bash
cd plugin/StudioKEYS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target StudioKEYS-vst3 -j
```

Output: `build/out/StudioKEYS.vst3` (also copied to `~/Library/Audio/Plug-Ins/VST3/`).

Standalone APP format is enabled (`StudioKEYS-app`); build when needed for Phase 10.
