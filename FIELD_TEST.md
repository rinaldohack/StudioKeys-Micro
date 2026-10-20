# StudioKEYS Micro — hardware test card

**Build under test:** `flash-2026-10-18/` — identity `SKM_1001`, version `v1.0.0`,
USB product `StudioKEYS Micro`, app PID `0x4B4D` (**Elite**), flash part `0x93000` B.
SHA-256 of the four files: see `SHA256SUMS` (verified again on 2026-10-19; a fresh build from
HEAD reproduces these bytes).

Tick each line. A section is done only when every line is **Pass**; write the step number in the
notes when it fails.

**Rules for this session**

1. On this Mac the install path is **USB-MIDI OTA** (`tools/fm1_install.py`). `jl-uboot-tool`'s
   SCSI layer has no macOS support, so a raw flash dump/write is not possible from this laptop.
   Do not try; use the OTA path.
2. Never power off or unplug while the installer is writing. If it is cut off, leave the keyboard
   in update mode and run the same command again — it resumes.
3. Do not touch SETTINGS → **CALIBRATION**. BTIF / calibration (`0xC0000–0xC1FFF`) are outside
   everything the package writes.
4. One instance per keyboard. A second instance must be refused, not share it.

---

## 0. One-time setup on the laptop

- [ ] `python3 -m venv ~/.venvs/sk-ota && ~/.venvs/sk-ota/bin/pip install mido python-rtmidi`
- [ ] `bash tools/field_check.sh` → *local side OK* (release checksums, VST3 id + signature)
- [ ] Run Terminal.app for anything MIDI. CoreMIDI fails inside a sandboxed agent shell.

## A. Install, then prove the install

- [ ] **A1** `bash tools/field_check.sh ../flash-2026-10-18` with the keyboard plugged in →
      four ports found, installer reaches the keyboard.
- [ ] **A2** `~/.venvs/sk-ota/bin/python tools/fm1_install.py --info` → identity starts with
      `SKM_`. An `ota-…` identity means the keyboard is still in update mode: run A3 to finish it.
      A different model (`FM-1`, `MKE`, `STARRYKEY`) means this package is not for this unit.
- [ ] **A3** Install:
      `~/.venvs/sk-ota/bin/python tools/fm1_install.py ../flash-2026-10-18/studiokeys.fwsc`
      Expect: progress to the end, exit 0, the keyboard restarts **by itself**.
- [ ] **A4** `--info` again after the restart → identity `SKM_1001` (no `ota-` prefix).
- [ ] **A5** SYSTEM → **ABOUT** (menu, SYSTEM tab, OCT+) shows `v1.0.0` and the StudioKEYS line.
- [ ] **A6** Power-cycle three times. Boots to the home screen every time, no
      `bootguard`/rollback message, settings and theme kept.
- [ ] **A7** User data survived: USER page still lists the previous `Uxx` presets, PROJECT still
      lists the saves, Bluetooth name unchanged (pair to a phone once).
- [ ] **A8** Audio out (phones/line) works on all four parts; keys, pads, encoders, four faders,
      all 18 panel buttons respond.

**Abort rule:** if A3 exits non-zero or A4 shows the loader identity, stop and re-run A3 before
testing anything else.

## B. VST3 in the DAW

- [ ] **B1** Live scans and shows `StudioKEYS` (VST3). Insert it on a MIDI track.
- [ ] **B2** Status line reads `v1.0.0 · Limited · 91 voices · …`; no "not linked" error when nothing is
      connected (standalone operation).
- [ ] **B3** DAW MIDI plays the engine; keybed strip follows; octave, velocity and sustain behave.
- [ ] **B4** Remove/re-insert the plugin, restart the DAW: settings, theme and patch reload
      (`~/Library/Application Support/StudioKEYS/nor.bin`).

## C. Sample rates and the resampler

Run the same short phrase at each rate, with a tuner after the plugin.

- [ ] **C1** 44.1 kHz — bit-exact bypass path: pitch A4 = 440 Hz ±1 cent, no clicks.
- [ ] **C2** 48 kHz — same pitch and level; no aliasing on C7 with `SUPER`/`NEON LEAD`.
- [ ] **C3** 88.2 / 96 kHz — same pitch; high notes clean; block boundaries silent.
- [ ] **C4** Change the session rate *while* a note is held — note continues, no jump or crash.
- [ ] **C5** 60 s sustained pad at 48 kHz: no drift (tuner stays put), no periodic ticking.

## D. Link and lock (the point of the companion)

Ports on the keyboard: **StudioKEYS** (perf), **StudioKEYS Live**, **StudioKEYS HUI**,
**StudioKEYS Link**.

- [ ] **D1** **LINK** → pick `StudioKEYS Link` → **Connect** → status
      `Connected · StudioKEYS Link · unlocked`; the sync dialog appears.
- [ ] **D2** **Plugin → Keyboard** → status `locked`, save indicator on. The keyboard's screen
      follows the plugin's current page.
- [ ] **D3** Turn a plugin knob → same parameter moves on the keyboard (screen value + sound).
      Turn the hardware encoder → the plugin's control moves. Page does not fork.
- [ ] **D4** SETTINGS → **COLOR**: change palette in the plugin; hardware panel repaints too
      while locked and keeps that theme after unlock.
- [ ] **D5** **Disconnect**: both sides keep their own state and keep sounding.
- [ ] **D6** Relink → **Keyboard → Plugin**: plugin loads the keyboard's patch; lock ON.
- [ ] **D7** Second instance (plugin + standalone, or two plugins): the second is refused the
      keyboard; the first keeps the lock.
- [ ] **D8** Declining the sync (dismiss) leaves both independent and unlocked — no partial state.
- [ ] **D9** Unplug mid-lock: plugin keeps sounding, keyboard keeps its patch. Replug → the sync
      question returns; it does not silently relock.
- [ ] **D10** With the lock on, edit for two minutes, quit the DAW, reopen: the plugin's state
      matches the keyboard's, and the keyboard still has it after a power cycle.

## E. Desks, pages, panel

13 engine desks + SETTINGS; 33 firmware pages.

- [ ] **E1** Every desk draws and its eight knobs match the labels the hardware shows: ANALOG,
      FM6, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, NOISE, SLICE, DRUM.
- [ ] **E2** All 33 pages reachable from the plugin: ENV, LFO, MOD, FX, SLICER, DLY, REVERB,
      CHORUS, SCL, CHORD, EDIT, SLICES, OP1–OP4 ENV, VOICE, GLOBAL, SYSTEM, PRESETS, USER,
      PROJECT, TOOLS, PERFORM, SPLIT, ARP, STEP, PATTERN, PHRASES, MIXER, SONG, CHANCE, AUTOMATION.
- [ ] **E3** Changing a control jumps to the page that owns it (both directions while locked).
- [ ] **E4** SETTINGS rows all present and effective: COLOR, STYLE, LARGE, ANIM, LEDS, LANG, HOLD,
      KNOB ACCEL, FX LATCH, BPM SYNC, MASTER, MASTER EQ, USB LEVEL, REMOTE OUT, METRO, COUNT-IN,
      USB SERIAL, CALIBRATION, ABOUT, FLAPPY, TETRIS, UPDATE.
      (Open CALIBRATION only to look; do not save.)
- [ ] **E5** LANG switches EN ↔ ID on the menu; effect labels (CUTOFF, ATK, …) stay English.
- [ ] **E6** COLOR spot-check GREYSCALE, MONO, HI-CON, LCSL BLUE, LUNA, ELITE: panel and screen
      repaint together.
- [ ] **E7** 44 px rule: every button, pad, encoder, fader and wheel is ≥ 44 px and hits on the
      first click, including the holds.
- [ ] **E8** Hold a key, tap **OCT+** then **OCT−**, release: the note stops at the pitch it
      started (no hanging voice).
- [ ] **E9** SPLIT chip opens the zones editor; keybed mirrors the zones; PERFORM pads recall
      U01…U15 and pad 16 jumps to the edit page.
- [ ] **E10** Undo/Redo (⌘Z / ⇧⌘Z, and SAVE hold on hardware) swaps the selected track's sound
      back and forward — engine, preset, params, FM6 patch.
- [ ] **E11** HOME's bottom strip is the **pads' legend**: 16 cells in the pads' order, each in the
      colour its LED shows, labelled with what it does. Turn a drawbar, toggle DRIVE / CHORUS /
      REVERB, cycle the Leslie and press an engine's pads on every engine page: the strip follows
      within a frame, and the labels stay readable on bright and dark fills.
- [ ] **E12** WHEEL: HOME **SOUND** is `16' 5 1/3' 8' 4' 2 2/3' REG DRV CHOR` — the 2 2/3' knob is
      there — and **MORE** is `PERC CLICK ROTR LEVEL PAN DLY REV 1'`, with no disabled cell. The
      Leslie pad is solid red at STOP, and breathes amber (slow) / green (fast) once per
      revolution.
- [ ] **E13** **BPM SYNC off**: start the DAW at another tempo (or drag its tempo) — the keyboard's
      tempo does not move and the sequencer keeps its own; turn SYNC on and it follows.

## F. Sound parity (keyboard vs plugin, same preset)

Method: hardware phones/line out into the interface, record 4 s; then plugin solo, same preset,
same note and velocity. Compare pitch, level (±1 dB), attack/decay shape, filter sweep, FX tail.

- [ ] **F1** ANALOG: `WARM CHORD`, `BALLAD PAD`, `TALK LEAD` (glide + legato).
- [ ] **F2** **UNISON width** (`SUPER`, `DIST`/detune): hardware and plugin have the same spread
      and level. This is the `SK_UNISON 8` fix — 8 voices on both, not 16.
- [ ] **F3** MONO / LEGATO / POLY: legato does not retrigger; glide time matches; poly voice
      stealing matches at the same note count.
- [ ] **F4** FM6: cartridge patch loads on both, operator envelopes (OP1–OP4 ENV) identical.
- [ ] **F5** PHASE, LOFI, SAMPLE (start/end + audition), VOICE, TRIO, WHEEL, GRAIN, PHYS,
      NOISE, SLICE, DRUM — each sounds like its hardware counterpart and its page edits the same
      parameter.
- [ ] **F6** FX: per-track DIST/CHORUS/DLY/REV and global DLY/REVERB/CHORUS; tail lengths match.
- [ ] **F7** Mod matrix: four slots, source/dest/amount behave the same; velocity and aftertouch.
- [ ] **F8** Voice budget: Limited mirrors the device's cost budget (a supersaw pad stops at 12
      voices, a SAMPLE or WHEEL patch plays 48); Unleashed keeps the chord
      and shows `Polyphony won't match the keyboard`.
- [ ] **F9** Fast knob sweeps produce no zipper noise or dropouts (audio-thread hygiene).

## G. Librarian, import/export, projects

- [ ] **G1** DX7 32-voice `.syx` → bay shows 32 rows; drag to librarian stores FM6 patches with
      names; the original dump stays attached.
- [ ] **G2** Virus TI dump → ANALOG rows; export one row back, name matches.
- [ ] **G3** Export program (`.skp`), export USER bank, re-import both.
- [ ] **G4** USER slots to U100: save, load, favorite, erase.
- [ ] **G5** Project saved on the keyboard loads in the plugin; a project saved in the plugin
      pushes back over the lock; A/B slots keep their own data.
- [ ] **G6** Per-patch glide/voice mode: two patches with different glide times do not leak into
      each other when switching.

## H. Live and HUI

- [ ] **H1** Hold `>` 0.5 s → `ABLETON LIVE`. Script installed, Live surface = StudioKEYS.
- [ ] **H2** Pads and transport reach Live on the **StudioKEYS Live** port; `Face` flips
      Session ↔ Mixer; `Keys` routes the keys to Live and back.
- [ ] **H3** Four faders ride four tracks; eight knobs move the selected device; Mixer shows
      level and pan.
- [ ] **H4** Hold **Fader Bank** 0.5 s → `MACKIE HUI`; four faders + transport/timecode follow.
- [ ] **H5** Unplug: remote commands stop, the synth keeps playing, and a lock survives.
- [ ] **H6** In remote mode the keyboard's own audio is muted; SETTINGS → **REMOTE OUT** on
      restores it. Toggle both ways.

## I. USB audio and console

- [ ] **I1** macOS shows the keyboard as a USB audio input (44.1 kHz, 16-bit stereo). Record two
      minutes while playing; no dropouts, level matches the line out.
- [ ] **I2** SETTINGS → **USB LEVEL** changes the capture level.
- [ ] **I3** CDC console (optional): `screen /dev/tty.usbmodem* 115200` prints the boot banner.
      (USB *playback* to the keyboard is not in this build — `SKM_UAC_PLAY=0`; not a failure.)

## J. Soak

- [ ] **J1** 30 minutes of playing, page switching and lock on/off; no crash, no audio dropout.
- [ ] **J2** OPEN FLAPPY and TETRIS for 5 minutes each, then close: audio returns, UI redraws.
- [ ] **J2b** FLAPPY: every pipe passed scores (+1, the score in the header). MIDI notes from the
      DAW (and the panel keys) flap the bird. TETRIS: hold a piece against either wall and rotate
      it — it never disappears; a note-on drops / moves it by octave.
- [ ] **J3** 20 lock ↔ unlock cycles, including unplug/replug.
- [ ] **J4** Power-cycle after the soak: same patch, same theme, same page.

## Known gaps (do not count as failures)

- Windows/Linux DAW hashes are still placeholders (`host/preset_hash.{win,linux}.sha256`).
- Standalone APP needs full Xcode (`ibtool` absent here).
- AU/CLAP are not built.
- USB audio playback into the keyboard (`SKM_UAC_PLAY`) is off in this image.
- Raw flash dump/write from macOS: not supported by `jl-uboot-tool`.

## If something fails

Capture, in this order: the step number, a photo of the keyboard screen, the DAW's plugin window
(status line), and `~/Library/Logs/DiagnosticReports/*.ips` if the plugin or DAW crashed.
Then `bash host/check_phase1.sh` and `AC79_SDK=~/jl-ref/fw-AC79_AIoT_SDK bash tests/run_tests.sh`
(both were green at this commit) so we know whether the host side reproduces it.

Old backups that still exist if a flash ever goes wrong: `recovery/elite_016/elite_brick_dump.bin`
(full 1 MiB), `btif_usrtrim_C0000.bin`, `restore_0_9D000.bin`.
