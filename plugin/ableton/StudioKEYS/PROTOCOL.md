# StudioKEYS Live surface protocol (USB cable 1)

Custom MIDI map for StudioKEYS Micro (firmware `sk_live.c`).

All messages on MIDI channel 1 unless noted. Firmware sends; script listens on the **StudioKEYS Live** port.

## Notes (momentary: vel &gt; 0 = down)

| Note | Function |
|------|----------|
| 36–43 | Session: clip pads (2 scenes × 4 tracks). Drum face: drum rack pads |
| 44 | Stop all clips |
| 46 / 47 | Track bank − / + (steps of 4) |
| 48–51 | Mute tracks in bank |
| 52–55 | Solo |
| 56–59 | Arm |
| 60–63 | Select |
| 70 | **PARA** — Session ↔ Mixer face |
| 71 | **SCALE** — Clips ↔ Drum pad face |
| 72 | **GLOBE** — Keys → synth vs armed Live track (notes leave on Performance port) |
| 80 | Play |
| 81 | Stop |
| 82 | Session record |
| 83 | Metronome |
| 84 | Capture MIDI |
| 85 / 86 | Scene − / + |

## CCs

| CC | Function |
|----|----------|
| 21–28 | Relative knobs (64 + Δ): Session = device macros; Mixer = pan 0–3 + send A 0–3 |
| 41–44 | Absolute faders: track volume for bank |

## Feedback (Live → device)

Same note numbers for LED/state. Fader positions on CC 41–44.

SysEx header `F0 7D 53 4B 4D` (`7D` + `SKM`):

- `01 face pad_face bank keys playing recording metro F7` — state
- `02 strip name[8] F7` — track name (strip 0–3)

## Hardware holds

- Hold **&gt;** ≈ 0.5 s — enter/leave Ableton Live (cable 1)
- Hold **Fader Bank** ≈ 0.5 s — enter/leave Mackie HUI (cable 2)

## HUI (cable 2)

Standard Mackie HUI: pitch-bend faders ch 1–8, V-pot CC 0x10–0x17, strip notes (rec/solo/mute/select), transport 0x4B–0x4F, bank 0x2E/0x2F, jog CC 0x0C, SysEx `F0 00 00 66 05 …`. Physical faders bank by 4 inside the eight-strip window (Fader Bank tap).
