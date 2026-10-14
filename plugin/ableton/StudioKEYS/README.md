# StudioKEYS — Ableton Live MIDI Remote Script

Control surface for **StudioKEYS Micro**. Speaks the firmware Live protocol on the
**StudioKEYS Live** USB port (cable 1). Product identity is StudioKEYS.

## Install

1. Copy this folder so Live sees:

   `~/Music/Ableton/User Library/Remote Scripts/StudioKEYS/`

   containing `__init__.py`, `elements.py`, `mappings.py`, `midi.py`, etc.

2. Restart Ableton Live.

3. **Link/Tempo/MIDI** (Live 12: Settings → Link, Tempo & MIDI):
   - Control Surface: **StudioKEYS**
   - Input: **StudioKEYS Live**
   - Output: **StudioKEYS Live**

4. On the keyboard: hold **>** (~0.5 s) to open the Live card.

Performance notes stay on **StudioKEYS** (cable 0). HUI is a separate port
(**StudioKEYS HUI**); enable Mackie HUI / DrivenByMoss on that port if you need it.

## What it maps

| Hardware | Live |
|----------|------|
| Pads | Clips (Session) or drum rack (**SCALE** toggles) |
| 4 faders | Track volumes (bank of 4; **&lt;** / **&gt;** bank) |
| 8 knobs | Device macros, or pans/sends in Mixer (**PARA** toggles face) |
| SEQ PLAY / PATCH / SEQ REC / ARP / CHORD | Play / Stop / Record / Metronome / Capture MIDI |
| GLOBE | Keys → synth vs armed Live track (keys leave on Performance port) |

See [PROTOCOL.md](PROTOCOL.md) for the MIDI map.

## Plugin + keyboard smoke (phase 9)

With the VST3 loaded and StudioKEYS Micro plugged in:

1. Plugin **Link** → CoreMIDI finds StudioKEYS / SMK → status shows USB MIDI open.
2. Sync dialog → **Plugin → Keyboard** → lock ON, continuous save.
3. Plugin **Live** (or hold RIGHT ~0.5 s on the keyboard) → Surface: Ableton Live.
4. Pad1 / transport / knobs in the plugin surface strip → SKS on cable 3 → keyboard
   forwards on **StudioKEYS Live** (cable 1) → Live remote script reacts.
5. **HUI** (or hold F.BANK) → faders speak Mackie HUI on cable 2.
6. Drop lock → both sides independent; Live/HUI leave stay on the keyboard ports.

Without a keyboard, `host/sk_link_loopback` (via `host/check_phase1.sh`) proves
HELLO→lock ON and SKS note/CC/HUI frames dispatch in-process.
