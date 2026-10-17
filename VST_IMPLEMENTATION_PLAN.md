# StudioKEYS companion — implementation plan

Status: **field-test ready (2026-10-12).** Software + in-process checks green;
CFW + VST3 rebuilt same day (`build/studiokeys.fwsc`, `~/Library/Audio/Plug-Ins/VST3/StudioKEYS.vst3`,
both `v1.0.0`). Hardware/DAW E2E is the field sheet (`FIELD_TEST.md`). Replaces the open
question in `MASTER_PLAN_DISASM_CFW_VST.md` Phase 7.

| Phase | State |
|---|---|
| 0 web reference-only | Done (`web/README.md`; firmware build does not launch emu) |
| 1 host library | Done: Unleashed, lock, desk/page, preset-hash, palette, NOR, SKL/SKS loopback, OCT note-off, rename hard-gate in `check_phase1.sh`; wasm optional |
| 2 VST3 shell + host audio | Mac arm64 `.vst3` + host float gold; Win/Linux presets + hash stubs (DAW bounce needs those OSes) |
| 3 keyboard UI | Done: panel + A0–C8 + COLOR; **`check_hit_targets.py`** ≥44 px (wheels strip 100) |
| 4 desks | Done: live labels, Pg± / page knobs, SAMPLE desk, desk↔`ui` sync; **LANG EN/ID** (= CFW `MENU > LANG`) |
| 5 power | Done: Limited=8; Unleashed → NVOICE=128 |
| 6 lock | Soft-done: SNAP_* + CoreMIDI + SetWire + loopback; **USB device E2E needs keyboard** |
| 7 librarian | Done: NOR scan + browse + Load; VST-only refuse when locked |
| 8 import bay | Done: store/export/Bulk32 + BayDrag + `check_bay_bulk.py` |
| 9 Live + HUI | Soft-done: FW + Ableton + SKS (`01/02/11/12`) + `check_surface_map.py`; **DAW E2E needs Live + keyboard** |
| 10 standalone | Soft-done: APP needs Xcode.app; headless CLI ships; VST3 is the product path |

The plugin is the instrument. It runs the same firmware the Elite runs, makes the
sound with or without an SMK-37 plugged in, and, when the keyboard is plugged in,
keeps the hardware on the same screen and the same controls as the window. The
plugin is the one that makes the audio.

From here the firmware and the plugin move together. A parameter, a setting, an
engine control, or a stored patch field lands in both in the same change. A
control that exists on one side and not the other fails the check for that change.

## Boundaries

- The window is a native plugin UI. The format for now is VST3, on Windows,
  Linux, and Mac. AU and CLAP wait on the backlog. There is no VST2.
- The JieLi CPU is not emulated. The firmware's C is compiled for the host, the same
  way `host/studiokeys_host.c` drives it. The native host is the
  method. The plugin is the product.
- The editor SysEx (`F0 7D 46 4C …`) stays. The lock
  session speaks it.
- The product is not named after the other keyboard or the firmware it was
  ported from. Those names come out of the firmware, the host, and the plugin:
  symbols, files, screen text, and comments. The GitHub license pass restates
  the copyright lines so the licenses stay intact. Behavior and on-disk formats
  stay.
- One analog engine. FM6 is the six-operator engine. HYPE stays wave index 8.
  Four tracks. All 13 playable engines have a desk.
- License text is added when this tree is published to GitHub. The firmware
  notices that are already in the sources stay. This plan does not redesign them.

## How the firmware runs

`host/studiokeys_host.c` is the method. The audio side asks for
samples, the device clock advances, the audio interrupt fills each half of 128
frames at 44.1 kHz, and the real main loop runs every 15 ms. The screen is a
240×240 RGB565 buffer. Flash is a 1 MiB image behind `storage.c`.

That browser file stubs a panel this keyboard does not have. The plugin stubs the
SMK-37 scan in `firmware/hal/smk_board.h`: 37 keys, 8 encoders, 8 pads (each pad
has a front and a back sensor), 4 faders, pitch, mod, sustain, and the printed
buttons. The on-device knob map stays the one the hardware uses.

The host library, called from the audio thread only:

| Stub | What the firmware sees |
|---|---|
| Clock | the millisecond clock and the 24 MHz tick advance with the samples the DAW asked for |
| Audio | the audio interrupt writes int32 halves; the host converts to float and resamples 44.1 kHz to the host rate |
| Panel | 37 keys with the stored octave pitch, 8 pads, 8 encoders, 4 faders, pitch and mod wheels, sustain pedal, every printed button |
| Screen | the existing fill and blit into a 240×240 RGB565 buffer the window copies out |
| Flash | 1 MiB image: settings, projects, user presets, user samples |
| USB | absent inside the process. MIDI from the DAW enters the same queue the keyboard uses. MIDI from the hardware arrives on the message thread and is queued across |

UI, SysEx, and the window never call into the firmware on the message thread. They
push input into a lock-free queue. The audio thread drains it where `web_ms` drains
the browser's input. A framebuffer copy comes back the other way for the widget.

Exit test for this library, before any window exists: a fixed song matches
`host/native_check.c` against the device render, and the SMK note-off path uses the
pitch stored at note-on.

## The window

Two regions, one process, one `ui` struct.

```
┌─────────────────────────────────────────────┐
│  editor desk, full width of the keyboard     │
│  FM6: six operators, algorithm, LFO, pitch   │
│  ANALOG: the OsTirus-style desk in this band │
├─────────────────────────────────────────────┤
│  a compact SMK-37, then a short Kontakt keybed │
└─────────────────────────────────────────────┘
```

The panel resembles the SMK-37. It is not a full-size copy, and it does not
fill the window. The editor desk above it is the large part. The wireframe is
[the plugin window](/Users/rinaldojonathan/.cursor/projects/Users-rinaldojonathan-Documents-smk37/canvases/smk37-plugin-wireframe.canvas.tsx).
Pitch and mod sit beside the keys, as on a Kontakt strip. The screen stays where it is, and what is
drawn inside it is the firmware's own 240×240 layout, scaled, not a second
design. The six buttons stay under the screen. The eight knobs stay above the
eight pads. The four faders stay at the right, and the twelve buttons stay
under them. Left to right the faders are the ones the probe measured: 2, 3, 4, 1.

The keybed under that panel is a Kontakt strip, not the hardware's 37 keys.
White keys are about a third of the height in the first wireframe. The range
is the piano, A0 through C8, so the plugin can play more than C1–C4. A click
plays that pitch. The hardware keybed, when the session is locked, is still
C1–C4 and still follows OCT− / OCT+.

A pad, knob, fader, wheel, or button is a hit target, including the holds the
firmware already implements. Those targets stay at least 44 px. The piano
strip is shorter and its keys are narrower, the way Kontakt draws them. The
plugin does not grow a second arrangement for a phone.

The panel, the inspector, and the screen share the firmware palette. COLOR
paints all three. The mark has two versions, dark ink and light ink, and the
lightness of the palette picks which one is drawn. Chassis themes (WHITE, RED, PINK, BLACK, RGB, ELITE) are
that same palette, so ELITE is grey and orange on the panel, in the inspector,
and on the screen together. The drawing language is the firmware's: flat
surfaces, the five palette roles, pill labels, the same type. The inspector
does not invent a second theme.

The inspector is generated from the firmware tables, not a hand-copied list:
`PAGES[]` in `params.c`, each engine's edit descriptors, `menu_items.c`, the mixer
(LEVEL PAN REV DLY, mute as Globe + the track key), the step and pattern data, the
FM6 packed voice, and the settings block. A host test fails if a parameter id
exists in the firmware and is missing from the inspector.

The two regions are the same session.

- Turning a virtual knob, or editing a number above, writes the parameter the
  firmware writes. The framebuffer updates, so the drawn keyboard lands on that
  page. Editing the analog sound moves the virtual keyboard onto the ANALOG pages.
  Opening MIXER on the virtual keyboard shows the mixer above.
- The inspector follows `ui` (page, track, engine, menu). It does not keep its own
  page.

The FM6 picture in the wireframe is one desk, not the only one. The desk follows
the firmware page. The keyboard on MIXER shows the mixer above. The keyboard on
a PHASE page shows the PHASE desk. Changing the desk moves the keyboard onto
that page. Changing COLOR in the plugin changes the theme on the plugin and, in
a locked session, on the SMK-37.

There are 13 playable engines, in the order the keyboard shows them. DIGITAL is
reserved and is not one of them. Each gets a desk. The four cards on the device
stay the device's view. The desk shows every control those cards do not, from
the same parameter tables.

| Engine | Desk |
|---|---|
| ANALOG | OsTirus as the layout reference. SUP stays the supersaw. HYPE stays wave index 8 |
| FM6 | Texed / Dexed as the layout reference. Six operators, the algorithm chart, the 128-byte voice |
| SAMPLE | Every stored control, plus a simple audio editor: the waveform the patch plays, start and end, and audition |
| PHASE, LOFI, VOICE, TRIO, WHEEL, GRAIN, PHYS, NOISE, SLICE, DRUM | Every stored control of that engine, in this same language |

The settings page is a desk too: COLOR, MASTER EQ, DEBUG PORT, AUDIO OUT ON
REMOTE MODE, and every other menu row. The plugin edits them. The keyboard
applies them.

## Power

A switch in the plugin.

- Limited: the plugin uses the keyboard's budget. Eight voices shared across
  four tracks. A locked session can sound the same.
- Unleashed: the plugin spends what the host CPU can hold. The cap is measured
  from the audio callback, not a fixed number chosen here. The keyboard stays
  at eight voices. Parameters and the screen still lock. Polyphony can differ,
  and the window says so.

## Librarian, import, export

The keyboard stores factory sounds, 100 user slots, and projects. The plugin has
two libraries: hardware-ready, and VST-only. A patch shows which it is. VST
mode has no hardware ceiling, so a sound that will not fit is edited on the
plugin and kept in the VST-only library.

DX7 SysEx and Virus TI SysEx open into an import bay, not into a keyboard slot.
Each file sits there as the original dump plus the sound it became. Dragging a
row from the bay into the librarian stores it in the plugin's own format. The
original dump stays beside it.

Export is the other direction, from a librarian row or from the sound that is
open:

- an FM6 sound writes a DX7 voice, and a bank of them writes a 32-voice bulk
  dump (`F0 43 00 09 20 00`)
- an ANALOG sound writes a Virus TI dump

A byte the destination has and this instrument does not is filled with a stated
default. A byte this instrument has and the destination does not stays in the
plugin's copy. The original dump is what a round trip uses when it is still
attached.

## Portamento and mono, per patch

On a DX7, portamento and mono belong to the cartridge, not to each voice. Here
they belong to the patch, on the firmware and on the plugin.

`preset_t` already stores mono. It does not store glide time or glide mode.
Those are track values (`P_GLIDE`, `P_GLMODE`) and do not travel with the
sound. Both sides gain them on the preset. Loading a patch sets voice mode,
glide time, and glide mode. Editing them on one patch leaves the others alone.

A DX7 import that carries one portamento value copies it onto each voice as
that voice's own starting value. After that, the voices diverge. A Virus TI
import does the same for the analog patch it becomes.

## Locked session

Four MIDI ports. Performance stays off the link, the way a Montage M keeps ESP
on its own cable.

| Port | Who uses it |
|---|---|
| 1 (cable 0) | Performance. Notes the DAW records. The synth. |
| 2 (cable 1) | Ableton Live. The port the remote script opens. |
| 3 (cable 2) | Mackie HUI. The port the DAW's HUI opens. |
| 4 (cable 3) | The plugin link. Sync, the lock, and surface commands on their way to the keyboard. Never notes. |

The link is the fourth port, not Live's. Live already has cable 1.

Unplugged, the plugin is the whole instrument.

Plugging in does not lock by itself. The window asks which side is the start:
copy the plugin onto the keyboard, or copy the keyboard onto the plugin. The
chosen snapshot is written, then both sides check it. The session starts only
when they match. Dismissing the question, or a transfer that does not match,
rejects the connection. Each side keeps running on its own. Nothing is mirrored
until a sync has succeeded.

After that, both sides show the same firmware UI and answer the same controls.
The plugin makes the sound. The keyboard's jack and USB audio stay muted unless
AUDIO OUT ON REMOTE MODE is on (`favorites.factory[15][27]`). If the checksum
later disagrees, the window asks again. Declining that resync drops the
connection. Two different states are not a session.

```
plugin surface ──cable 4──▶ keyboard ──its own Live or HUI port──▶ the DAW
keyboard hands ──────────▶ that same port, and back on cable 4 to the plugin
```

Both sides apply that one log and do not also apply their local click. A knob on
the desk and a knob in the window are the same event.

The screens match pixel for pixel because both sides run this same drawing
code on the same state. The plugin's screen widget is that draw. The keyboard's
LCD is that draw. Nothing sends a picture. The session sends the events and,
when a checksum disagrees, the state both sides already know how to load
(tracks, song, `ui`, settings, the FM6 slot). That blob is a project
`storage.c` already writes.

The first snapshot is whichever side was chosen. After it matches, the event
log is the only way either side changes.

Live and HUI stay available during a lock. They use the other cables. Opening
one does not drop the lock and does not stop the plugin. The desk changes to
that card, on the plugin and on the keyboard together. Holding the same button
again comes back to the synth card. The lock is still there.

While the session is locked, the plugin keeps writing that state to the
keyboard. An accidental unplug leaves the keyboard where the session was, so
it can be played on its own. The plugin keeps sounding in the DAW.

One keyboard belongs to one plugin instance. A second instance in the same DAW
can run, and it cannot lock that keyboard. It connects only when another
SMK-37 is plugged in, and then the sync question is for that keyboard. Two
instances never share one lock. The standalone and the plugin open together
count as two instances.

## What a lock has to survive

These are the cases a session gets wrong if they stay implicit.

The plugin and the keyboard must be the same build. A different version
rejects the connection. The window says which side is older.

While locked, the plugin is the brain. Its audio callback advances musical
time. The keyboard is the display, and you can still play it. It applies the
plugin's time. Its own sequencer does not free-run, or the playhead walks
apart and the screens stop matching.

Performance notes are MIDI channel 1. They do not loop back into the plugin.
The keyboard's lights follow the shared state.

While the cable stays connected, the live state keeps syncing. Undo and redo
in the DAW are part of that, not the only part. A write that dies halfway
keeps the previous good copy. It is not a flash write on every knob tick.

The plugin can run in VST mode, with no hardware ceiling. A patch made there
may be VST-only. The librarian is two libraries: one hardware-ready, one
VST-only, and each patch shows which it is. Editing happens on the plugin. If
you try to put a VST-only patch on the keyboard, the window says it will not
fit, and nothing is half-written.

A control you move can be automated. The DAW learns it the way Dexed and
OsTirus do: move it, and the lane shows up. That includes theme. There is no
separate list of which parameters are allowed.

Unleashed notes beyond eight voices exist in the plugin. The keyboard plays
inside eight. Unplug keeps the extra notes in the plugin and the keyboard
inside its budget. The window already says the polyphony will not match.

## Playing Live, and mixing with HUI

Hold `>` for Ableton Live. Hold Fader Bank for a Mackie HUI. The same hold
returns to the synth. These already switch cables. What they do not do yet is
speak the protocols. Both the keyboard and the plugin grow a real card for
each, and the card is what you look at while you play.

Live is cable 1. The plugin ships a MIDI remote script that Ableton lists as
this instrument. The script is written for this panel. It is learned from the
FANTOM EX and from a Launchkey, and it does not pretend to be either. Ableton's
FANTOM script expects the FANTOM's controls. A Launchkey script expects a
Launchkey. Impersonating them would light the wrong buttons.

What a player gets, taken from those instruments:

- Two faces, as on the FANTOM EX: Session and Mixer. The card names the button
  that flips them. That button does it on the keyboard and on the plugin.
- Pads launch clips. A second pad face plays the drum rack on the selected
  track, the way the FANTOM's pad mode does.
- Four faders are track levels. Banking moves to the next four tracks.
- Eight knobs are the eight macros of the device on the selected track, so a
  sound can be ridden without leaving the keys.
- Transport sits on the card: play, stop, record, metronome, and Capture MIDI,
  which takes what you just played and makes a clip.
- The keybed keeps playing the synth you are hearing, including while the
  session is locked. A control on the Live card, shown on the screen, sends the
  keys to the armed Live track instead, when you mean to play a Live
  instrument. Those notes leave on the performance port. Live's surface
  traffic leaves on cable 1.

HUI is cable 2. It is the real Mackie HUI, not a handful of faders. A HUI strip
is a fader, mute, solo, record, select, a V-pot, and the track name. A HUI has
eight strips, a transport, a jog wheel, and a timecode. This keyboard has four
faders, so the hand banks by four. The plugin card shows all eight strips of
the current bank, the V-pots, the names, the transport, the jog, and the
timecode. The four strips under your fingers are the ones the faders move. The
other four are on the card, and they move too.

A move on the plugin card goes to the keyboard first, on the link port. The
keyboard shows it, then sends it out the Live port or the HUI port. The DAW is
listening to the keyboard, not to the plugin. The same path runs the other way
when your hand is on the hardware. With no keyboard connected, those commands
do nothing, and the window says so. The synth in the plugin still plays.

## Sound

The engine stays at 44.1 kHz and 128-frame halves. The plugin resamples to the
host rate. The audio thread is the device clock. There is no second copy of the
DSP in the UI.

DAW MIDI enters `midi_enqueue` on cable 0, the same queue USB MIDI uses on the
device. The virtual keybed is a panel source, so it does not count as an external
clock, matching `smk_panel.c`.

## Platforms

One C++ plugin target. VST3 only, on Windows, Linux, and Mac.

The UI kit is iPlug2, built for VST3 and not for the other formats. License
wording for the published tree is a GitHub step, not a step in this plan.

State in the DAW is the NOR image plus the plugin's session header (sample rate
of the resample, schema version). A project saved in the DAW restores the
instrument without the keyboard.

## Where it lives

```
studiokeys-smk/host/          the native host (clock, SMK stub, NOR, framebuffer)
studiokeys-smk/plugin/        the window, the inspector, the USB session
```

`host/` is allowed to include `firmware/src` the way `host/studiokeys_host.c` and
`tests/hostsim.c` do. `plugin/` talks to `host/` through the queue and the
framebuffer. It does not include the DSP files itself.

## Phases

Each phase ends with the check under it. Later phases do not start by widening
the one before it.

### 0. Web companion stands down

The browser editor and its `web/` tree are gone from this repository. The CFW and the
VST3 companion are the only product surfaces; no build step opens an editor page.

Check: a clean build of the firmware does not produce a wasm page or open an editor.

### 1. Names, then the host library

Every FM-1 name and every StudioKEYS name goes. That is symbols, macros, filenames,
screen text, and comments in the firmware, the host, and the plugin. The scan,
the clock, the audio interrupt, and the panel tables are included. Behavior and
on-disk formats stay. Copyright lines are restated in the GitHub license pass,
not dropped.

Then the host library: SMK stubs, the on-device knob map, the flash image, the
framebuffer, and a render call the audio thread drives. No window. New code
uses the product's names from the first line.

Check: the heavy song matches the wasm host sample for sample. A note held across
OCT+/OCT− releases the pitch it started, not the pitch the key has now. A search
of the firmware, the host, and the plugin finds neither of those old names.

### 2. Plugin shell

VST3 only. Stereo out. Resample. Silence until the host has booted its 15 ms
frames. No panel artwork yet. MIDI from the DAW plays the engine.

Check: a DAW on each OS loads the VST3 and renders a known preset to a hash.

### 3. The keyboard

The panel widget resembles the hardware and stays compact under the editor.
The keybed is the Kontakt strip, A0–C8, at about a third of the first
wireframe's key height. Pads, knobs, faders, wheels, and buttons are at least
44 px. The screen is the firmware framebuffer.
The panel and the screen use the active palette. Every control injects the stub,
including the holds already in the firmware.

Check: the host UI tests, driven through the stub, still pass. The drawn screen
matches a firmware frame for the same state. Switching COLOR repaints the panel
and the screen together. A 44 px probe hits every button, pad, knob, fader,
and wheel. The piano strip is A0–C8 and its keys are narrower than that.

### 4. Every desk, and settings

One desk per playable engine, plus the settings desk. The desk on screen is the
firmware's current page. COLOR in the plugin changes the palette the keyboard
is using when the session is locked. Hidden controls are on the desk.

Check: each of the 13 engines has a desk, and every parameter id and every menu
row is on one. The SAMPLE desk shows the waveform, start, end, and audition.
Changing a control moves the framebuffer to the page that owns it. Changing
COLOR on the plugin changes `settings.palette` and the drawn screen. The device
follows that theme once the session is locked.

### 5. Power switch

Limited matches the device: 8 voices, 4 tracks. Unleashed follows the host CPU.
The keyboard's voice budget does not change.

Check: limited, a held chord sheds the way the device does. Unleashed, the same
chord keeps the extra notes, and the window says the keyboard cannot match that
polyphony.

### 6. USB lock

The fourth MIDI port. Connect asks VST → keyboard or keyboard → VST, and rejects the
connection until that snapshot matches. Event log both ways after that. The
live state keeps saving to the keyboard for the whole lock. A later mismatch
asks again, and declining it drops the connection. One instance per keyboard.
Device audio muted unless the remote-audio setting is on. Unplug returns the
plugin to standalone without stopping the DAW audio, and the keyboard still
holds the session.

Check: dismissing the sync dialog leaves both sides independent. A different
build is refused. Choosing either direction, a knob on the hardware and a knob
in the window land on the same parameter and the same screen. Two draws from
that same state match pixel for pixel. COLOR changed in the plugin changes the theme on the device. Unplug
leaves the plugin sounding and the keyboard on that sound. A second instance,
including the standalone beside the plugin, is refused that keyboard. A second
keyboard locks to the second instance only.

### 7. Presets on the device, librarian on the plugin

The plugin reads and writes the keyboard's flash image: factory sounds, 100 user
slots, projects, user samples. The librarian itself has no cap: rows beyond the
keyboard's 100 stay VST-only, and every row is still draggable onto a U.. slot.

Per-patch voice mode, glide time, and glide mode are stored with the preset on
both sides. Loading one patch does not change another's portamento.

Check: a project saved on the keyboard loads in the plugin from the blob, and a
project saved in the plugin pushes back over the lock. Two FM6 patches in the
librarian can have different glide times.

### 8. DX7 and Virus TI

The import bay accepts a DX7 32-voice dump and a Virus TI dump. Nothing is
written into a keyboard slot until a row is dragged into the librarian. Export
writes DX7 from FM6 and Virus TI from ANALOG. The original dump stays attached
to the librarian row.

Check: a known DX7 bank opens in the bay as 32 rows, drag stores them as FM6
patches with their own mono and glide, and export of one row is a DX7 voice
whose name matches. A Virus TI dump does the same for ANALOG.

### 9. Live and HUI

Hold `>` speaks Ableton through the remote script. Hold Fader Bank speaks
Mackie HUI. A command from the plugin card reaches the keyboard on the link
port, and the keyboard sends it out cable 1 or cable 2. With no keyboard, the
command is refused and the window says the keyboard is not connected. A lock
on the link stays up, and the plugin keeps sounding.

Check: in Live, a pad on the plugin launches a clip only while the keyboard is
connected, and Ableton sees that message on the keyboard's Live port. The
faders ride four tracks, and the eight knobs move that track's device.
Flipping to Mixer shows levels and pans. The keys still play the synth until
the card's key-routing control is on. In HUI, four faders move four strips,
the card shows eight, and a DAW that expects a HUI follows the transport and
the timecode on the keyboard's HUI port. Unplugging the keyboard stops those
commands and leaves the synth playing. Doing either while locked does not drop
the lock.

### 10. Standalone

The same host and the same window, opened as an application, with no DAW. The
link port, the sync question, Live, and HUI behave as they do in the plugin.
VST3 remains the plugin format.

Check: a sound made in the standalone opens in the VST3, and a keyboard locked
to the standalone follows the same rules as a lock to the plugin.

## Field testing

Follow **`FIELD_TEST.md`**. Preflight: `bash host/check_phase1.sh` (must print
`phase1 host checks done`). VST3 at
`~/Library/Audio/Plug-Ins/VST3/StudioKEYS.vst3`. Ableton script:
`bash tools/install_ableton_script.sh`.

## Still open after field (other OS / Xcode / optional)

1. Win/Linux DAW middle-C hashes (`host/preset_hash.{win,linux}.sha256`).
2. WinMM / ALSA link sysex send (Mac CoreMIDI is the field path).
3. Standalone APP with full Xcode.app ibtool (headless CLI covers no-GUI audio).
4. Optional wasm bit-match when emscripten is present.

## Additions

Items below are deliberately not in the phases. Add any of them and it gets a
phase or a line under one.

- AU and CLAP, after VST3 is in use.

---

StudioKEYS Micro firmware and StudioKEYS companion © 2026 Rinaldo Jonathan (LangitCinta! StageLabs) and Hügelton Instruments. The companion VST3, the SMK-37 surface and its UI are Rinaldo Jonathan's work; the GPL engine lineage is Hügelton Instruments'. See `LICENSING.md`.
