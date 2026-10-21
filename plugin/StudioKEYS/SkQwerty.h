/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS VST3 companion */
/* The computer keyboard's notes (QWERTY: A W S E D F T G Y H U J K O L, Z / X octave), tracked here
 * instead of by IPlug2's IGraphics::SetQwertyMidiKeyHandlerFunc().
 *
 * Why not the helper: its held-key table is a static inside the handler's lambda, so nothing outside
 * can release it. On macOS a parameter's list is a NATIVE menu — IGraphicsMac::CreatePlatformPopupMenu
 * -> [NSMenu popUpMenuPositioningItem:...] — and that menu's tracking loop takes the key events while
 * it is up. A letter held when the list opens never gets its key-up, the handler never sends the
 * note-off, and the voice rings until the sound is reloaded ("played notes, opened the dropdown (POLY
 * to MONO), the note stuck"). The same hole is on Windows / Linux for the platform menus.
 *
 * Holding the table here does two things the helper cannot: a popup's opener drops whatever is still
 * held before the menu takes the keyboard (release_all), and a note that is somehow left down is let
 * go after kHoldCapMs (watchdog, called from the plugin's OnIdle) instead of sounding for ever.
 *
 * The note-offs take the ordinary UI-MIDI path (IPlugAPIBase::DeferMidiMsg -> the next block's
 * ProcessMidiMsg), so the engine, the MIDI out and the on-screen keybed see them like any other note.
 */
#pragma once

#include <algorithm>

#include "IGraphics.h"
#include "IPlugMidi.h"

namespace skqwerty
{
  struct State
  {
    int base = 48;                  /* the octave the letters start on (Z / X move it, 24..96) */
    bool down[128] = {};
    unsigned long long held[128] = {};   /* when each note went on (ms(), the idle clock below) */
  };

  inline State& state()
  {
    static State s;
    return s;
  }

  /* a free-running millisecond clock for the watchdog: no OS calls, only the gaps matter. tick() is
   * one idle frame (the plugin's OnIdle runs at ~30 Hz); a note-on only reads it. */
  inline unsigned long long& ms()
  {
    static unsigned long long t = 0;
    return t;
  }

  inline void tick() { ms() += 1000ull / 30ull; }

  inline void send(IGraphics* g, int pitch, bool on)
  {
    if (!g)
      return;
    IMidiMsg m;
    if (on)
      m.MakeNoteOnMsg(pitch, 127, 0);
    else
      m.MakeNoteOffMsg(pitch, 0);
    g->GetDelegate()->SendMidiMsgFromUI(m);
  }

  /* every letter still held: off now. Called before a popup (the menu eats the key-ups), on an
   * octave switch, and by the watchdog. Safe with nothing down or without a UI. */
  inline void release_all(IGraphics* g)
  {
    State& s = state();
    for (int n = 0; n < 128; ++n)
      if (s.down[n])
      {
        s.down[n] = false;
        send(g, n, false);
      }
  }

  /* a held note nobody can have held this long: the key-up went somewhere else (the menu, another
   * window). Bounded here so a lost key-up cannot outlive the take. */
  static constexpr unsigned long long kHoldCapMs = 20000ull;

  inline void watchdog(IGraphics* g)
  {
    State& s = state();
    tick();
    const unsigned long long now = ms();
    for (int n = 0; n < 128; ++n)
      if (s.down[n] && now - s.held[n] > kHoldCapMs)
      {
        s.down[n] = false;
        send(g, n, false);
      }
  }

  inline int semitone(int vk)
  {
    switch (vk)
    {
      case kVK_A: return 0;
      case kVK_W: return 1;
      case kVK_S: return 2;
      case kVK_E: return 3;
      case kVK_D: return 4;
      case kVK_F: return 5;
      case kVK_T: return 6;
      case kVK_G: return 7;
      case kVK_Y: return 8;
      case kVK_H: return 9;
      case kVK_U: return 10;
      case kVK_J: return 11;
      case kVK_K: return 12;
      case kVK_O: return 13;
      case kVK_L: return 14;
      default: return -1;
    }
  }

  /* IGraphics key handler (pGraphics->SetKeyHandlerFunc): the same map as the IPlug2 helper, with a
   * table we own. Returns true for every key so the OS does not beep. */
  inline bool handle(IGraphics* g, const IKeyPress& key, bool isUp)
  {
    State& s = state();
    if (key.VK == kVK_Z || key.VK == kVK_X)
    {
      if (!isUp)
      {
        s.base = std::clamp(s.base + (key.VK == kVK_Z ? -12 : 12), 24, 96);
        release_all(g);             /* the held notes are at the old octave: let them go, as IPlug2 did */
      }
      return true;
    }
    const int k = semitone(key.VK);
    if (k < 0)
      return true;
    const int pitch = s.base + k;
    if (pitch < 0 || pitch > 127)
      return true;
    if (!isUp)
    {
      if (!s.down[pitch])
      {
        s.down[pitch] = true;
        s.held[pitch] = ms();
        send(g, pitch, true);
      }
    }
    else if (s.down[pitch])
    {
      s.down[pitch] = false;
      send(g, pitch, false);
    }
    return true;
  }
}

/* The on-screen keybed, with the note-off IVKeyboardControl::OnMouseOut() forgets: the base clears
 * its held key when the pointer leaves the keys without sending one, so a key pressed with the mouse
 * and dragged off (onto a parameter, whose menu then opens) rang on. */
class SkKeyboardControl : public IVKeyboardControl
{
public:
  using IVKeyboardControl::IVKeyboardControl;

  void OnMouseOut() override
  {
    if (mLastTouchedKey > -1 && GetDelegate())
    {
      IMidiMsg m;
      m.MakeNoteOffMsg(GetMidiNoteNumberForKey(mLastTouchedKey), 0);
      GetDelegate()->SendMidiMsgFromUI(m);
      SetNoteFromMidi(GetMidiNoteNumberForKey(mLastTouchedKey), false);
    }
    IVKeyboardControl::OnMouseOut();
  }
};
