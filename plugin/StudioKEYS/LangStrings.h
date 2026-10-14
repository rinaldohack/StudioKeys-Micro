#pragma once

#include "sk_host.h"
#include <cstring>

/** CFW MENU > LANG (EN / ID). Labels only — param names (CUTOFF, ATK, …) stay English.
 * Tone: informal daily Indonesian, short. Mirrors firmware menu_items.c lang_str(). */
namespace sklang {

inline bool IsId() { return sk_host_lang() != 0; }

inline const char* T(const char* en)
{
  static const char* const kMap[][2] = {
    /* CFW menu_items.c MAP */
    {"COLOR", "WARNA"},
    {"LANG", "BAHASA"},
    {"DISPLAY", "LAYAR"},
    {"CONTROL", "KONTROL"},
    {"AUDIO", "SUARA"},
    {"SYSTEM", "SISTEM"},
    {"ABOUT", "TENTANG"},
    {"CALIBRATION", "KALIBRASI"},
    {"STYLE", "GAYA"},
    {"LARGE", "BESAR"},
    {"ANIM", "ANIMASI"},
    {"LEDS", "LAMPU"},
    {"HOLD", "TAHAN"},
    {"KNOB ACCEL", "AKSEL KNOB"},
    {"FX LATCH", "KUNCI FX"},
    {"BPM LOCK", "KUNCI BPM"},
    {"USB LEVEL", "LEVEL USB"},
    {"USB SERIAL", "SERIAL USB"},
    {"SETTINGS", "PENGATURAN"},
    {"MASTER EQ", "MASTER EQ"}, /* musician term — keep */
    {"DEBUG PORT", "PORT DEBUG"},
    {"AUDIO OUT ON REMOTE MODE", "AUDIO OUT PAS REMOTE"},
    {"MIDI CH", "CH MIDI"},
    {"CLOCK", "CLOCK"},
    {"TUNE", "TUNE"},
    /* Plugin chrome (informal) */
    {"Locked", "Terkunci"},
    {"Unlocked", "Bebas"},
    {"Sync?", "Sinkron?"},
    {"Limited", "Terbatas"},
    {"Unleashed", "Lepas"},
    {"Polyphony won’t match the keyboard", "Poly keyboardnya nggak bisa nyusul"},
    {"Polyphony won't match the keyboard", "Poly keyboardnya nggak bisa nyusul"},
    {"Keyboard not connected — Live/HUI leave on the keyboard's ports.",
     "Keyboard belum nyambung — Live/HUI cuma lewat port keyboard."},
    {"Refuse: another instance already owns a keyboard lock.",
     "Nolak: instance lain udah pegang lock keyboard ini."},
    {"Unplugged. Plugin is the whole instrument.",
     "Cabut. Plugin jadi instrumennya sendiri."},
    {"VST-only — will not fit the keyboard", "VST-only — nggak muat di keyboard"},
    {"SETTINGS — COLOR paints panel+screen; other rows live in the firmware MENU",
     "PENGATURAN — WARNA ngecat panel+layar; sisanya di MENU firmware"},
    {"SETTINGS — COLOR paints panel+screen; LANG matches CFW MENU > LANG",
     "PENGATURAN — WARNA ngecat panel+layar; BAHASA sama kayak MENU > LANG di CFW"},
    {"SAMPLE — live waveform / start / end / audition",
     "SAMPLE — waveform live / start / end / audition"},
    {"Surface: synth", "Surface: synth"},
    {"Surface: keyboard offline", "Surface: keyboard offline"},
    {"Import bay", "Import bay"},
    {"Librarian · Hardware-ready / VST-only · per-patch glide/glmode/mono",
     "Librarian · Siap hardware / VST-only · glide/glmode/mono per patch"},
    {"MENU", "MENU"},
  };
  if (!IsId() || !en || !en[0])
    return en;
  for (const auto& row : kMap)
  {
    if (std::strcmp(row[0], en) == 0)
      return row[1];
  }
  return en;
}

} // namespace sklang
