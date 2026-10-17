# StudioKEYS field test (keyboard + Ableton)

Short pass/fail for Rinaldo. Button names match `plugin/StudioKEYS/StudioKEYS.cpp`.

## Preflight

- [ ] **Pass** · **Fail** — CFW flashed from today’s `build/studiokeys.fwsc` (ABOUT shows `v1.0.0` + StudioKEYS companion line)
- [ ] **Pass** · **Fail** — VST3 present: `~/Library/Audio/Plug-Ins/VST3/StudioKEYS.vst3` (build also writes `plugin/StudioKEYS/build/out/StudioKEYS.vst3`)
- [ ] **Pass** · **Fail** — `bash host/check_phase1.sh` green (includes rename gate, OCT note-off, lock refuse, color)
- [ ] **Pass** · **Fail** — Ableton script: `bash tools/install_ableton_script.sh` → restart Live → Control Surface **StudioKEYS**, I/O **StudioKEYS Live**

## Steps

| # | Action | Expect | ☐ |
|---|--------|--------|---|
| 1 | Plug keyboard; plugin **Link** | Status: USB MIDI link open (or in-process if no USB); **Sync keyboard?** dialog | Pass / Fail |
| 2 | Sync **Plugin → Keyboard** | Lock **Locked**, continuous save on | Pass / Fail |
| 3 | Drop lock; **Link** again; Sync **Keyboard → Plugin** | Plugin follows keyboard; lock ON | Pass / Fail |
| 4 | **Desk** → **SETTINGS** → **COLOR** knob (0–18) | Palette changes on plugin (and HW when locked) | Pass / Fail |
| 4b | Chrome **EN/ID** (or SETTINGS → **LANG**) | Menu labels switch informal ID; CUTOFF/ATK/etc stay English | Pass / Fail |
| 5 | Toggle chrome **Limited** → **Unleashed** | Status shows Unleashed; warn: *Polyphony won’t match the keyboard* | Pass / Fail |
| 6 | Librarian: **DX7 demo** / **Virus demo** → **Bay→Lib** or drag bay strip → **Load** | Bay label updates; lib shows patch; desk/engine loads | Pass / Fail |
| 7 | Bay drag strip → drop on librarian band | Same as **Bay→Lib** (VST-only store) | Pass / Fail |
| 8 | **Live** (or hold `>` ~0.5 s on HW) | Surface Ableton Live; pads/transport → Live via **StudioKEYS Live** | Pass / Fail |
| 9 | **Face** / **Keys** while Live | Face flips Session↔Mixer; Keys toggles keys→Live routing | Pass / Fail |
| 10 | **HUI** (or hold Fader Bank) | Mackie HUI on **StudioKEYS HUI**; faders move tracks | Pass / Fail |
| 11 | Hold a key, tap **OCT+** / **OCT−**, release key | Note stops (stored pitch note-off; no hang) | Pass / Fail |

## Notes

- Without keyboard: phase1 + **Link** in-process only; Live/HUI need USB.
- **Dismiss** on sync leaves unlocked. **Face** / bank `<` `>` need keyboard connected.
