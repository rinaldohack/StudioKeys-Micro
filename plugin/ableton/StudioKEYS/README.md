# StudioKEYS — Ableton Live MIDI Remote Script

Control surface for **StudioKEYS Micro**. Speaks the firmware Live protocol on the
**StudioKEYS Live** USB port (cable 1). Product identity is StudioKEYS — not FANTOM
or Launchkey.

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
