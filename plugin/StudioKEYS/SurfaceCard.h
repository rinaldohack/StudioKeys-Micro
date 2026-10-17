/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "MidiCables.h"
#include "sk_host.h"
#include "sk_sks.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

/** Plugin Live / HUI surface → link cable 3 → keyboard → DAW (plan phase 9).
 * Note/CC numbers match plugin/ableton/StudioKEYS/midi.py and firmware sk_live.c / sk_hui.c. */
namespace sksurface {

enum class Mode : uint8_t { Off = 0, Live = 1, Hui = 2 };
enum class LiveFace : uint8_t { Session = 0, Mixer = 1 };

inline const char* ModeName(Mode m)
{
  switch (m)
  {
    case Mode::Live: return "Ableton Live";
    case Mode::Hui: return "Mackie HUI";
    default: return "Synth";
  }
}

/* Live (cable 1) — see midi.py */
enum {
  kPad0 = 36,
  kStopAll = 44,
  kBankM = 46,
  kBankP = 47,
  kFace = 70,
  kPadFace = 71,
  kKeys = 72,
  kPlay = 80,
  kStop = 81,
  kRec = 82,
  kMetro = 83,
  kCapture = 84,
  kCcKnob0 = 21,
  kCcFader0 = 41
};

/* HUI notes (cable 2) — sk_hui.c */
enum {
  kHuiRec0 = 0x00,
  kHuiSolo0 = 0x08,
  kHuiMute0 = 0x10,
  kHuiSel0 = 0x18,
  kHuiBankL = 0x2E,
  kHuiBankR = 0x2F,
  kHuiPlay = 0x4E,
  kHuiStop = 0x4D,
  kHuiRecord = 0x4F
};

class SurfaceBridge
{
public:
  using WireFn = std::function<void(const uint8_t* data, uint32_t n)>;

  /** Optional USB mirror for SKS frames (plugin→keyboard on cable 3). */
  void SetWire(WireFn wire) { mWire = std::move(wire); }

  void SetKeyboardConnected(bool on) { mKb = on; }
  bool KeyboardConnected() const { return mKb; }

  Mode GetMode() const { return mMode; }
  LiveFace GetFace() const { return mFace; }
  int Bank() const { return mBank; }
  const char* StatusLine() const { return mStatus.c_str(); }

  void PulseHold(Mode want)
  {
    if (want == Mode::Live)
      mHoldMask = (1u << SK_X_RIGHT);
    else if (want == Mode::Hui)
      mHoldMask = (1u << SK_X_FADER);
    else
      mHoldMask = 0;
    mHoldFrames = 220;
    mPending = want;
  }

  void Tick()
  {
    if (mHoldFrames > 0)
    {
      sk_host_smk_extra(mHoldMask);
      --mHoldFrames;
      if (mHoldFrames == 0)
      {
        sk_host_smk_extra(0);
        if (mPending != Mode::Off)
          mMode = (mMode == mPending) ? Mode::Off : mPending;
        mPending = Mode::Off;
        RefreshStatus();
      }
    }
  }

  bool Refuse(std::string& refuse) const
  {
    if (!mKb)
    {
      refuse = "Keyboard not connected — Live/HUI leave on the keyboard's ports.";
      return true;
    }
    refuse.clear();
    return false;
  }

  bool SendSkS(uint8_t cmd, uint8_t a, uint8_t b, skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (Refuse(refuse))
      return false;
    const uint8_t sx[] = {
      0xF0, SK_SKS_HDR0, SK_SKS_HDR1, SK_SKS_HDR2, SK_SKS_HDR3, cmd, a, b, 0xF7};
    sk_host_link_push(sx, sizeof sx);
    if (mWire)
      mWire(sx, sizeof sx);
    midi.Enqueue(skmidi::Cable::Link, 0xF0, a, b);
    return true;
  }

  bool SendLiveNote(uint8_t note, uint8_t vel, skmidi::MidiRouter& midi, std::string& refuse)
  {
    return SendSkS(SK_SKS_LIVE_NOTE, note, vel, midi, refuse);
  }

  bool SendLiveCc(uint8_t cc, uint8_t val, skmidi::MidiRouter& midi, std::string& refuse)
  {
    return SendSkS(SK_SKS_LIVE_CC, cc, val, midi, refuse);
  }

  bool SendHuiStrip(uint8_t strip, uint8_t fader, skmidi::MidiRouter& midi, std::string& refuse)
  {
    return SendSkS(SK_SKS_HUI_FADER, strip, fader, midi, refuse);
  }

  bool SendHuiNote(uint8_t note, uint8_t vel, skmidi::MidiRouter& midi, std::string& refuse)
  {
    return SendSkS(SK_SKS_HUI_NOTE, note, vel, midi, refuse);
  }

  bool ToggleFace(skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (!SendLiveNote(kFace, 127, midi, refuse))
      return false;
    mFace = (mFace == LiveFace::Session) ? LiveFace::Mixer : LiveFace::Session;
    RefreshStatus();
    return true;
  }

  /** Live key-routing: when on, keybed notes leave on the performance port for Live. */
  bool ToggleKeys(skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (!SendLiveNote(kKeys, 127, midi, refuse))
      return false;
    mKeysToLive = !mKeysToLive;
    RefreshStatus();
    return true;
  }

  bool KeysToLive() const { return mKeysToLive; }

  bool BankLeft(skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (mMode == Mode::Hui)
    {
      if (!SendHuiNote((uint8_t)kHuiBankL, 127, midi, refuse))
        return false;
    }
    else if (!SendLiveNote((uint8_t)kBankM, 127, midi, refuse))
      return false;
    if (mBank > 0)
      --mBank;
    RefreshStatus();
    return true;
  }

  bool BankRight(skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (mMode == Mode::Hui)
    {
      if (!SendHuiNote((uint8_t)kHuiBankR, 127, midi, refuse))
        return false;
    }
    else if (!SendLiveNote((uint8_t)kBankP, 127, midi, refuse))
      return false;
    if (mBank < 7)
      ++mBank;
    RefreshStatus();
    return true;
  }

  bool Transport(uint8_t which, skmidi::MidiRouter& midi, std::string& refuse)
  {
    /* which: 0 play, 1 stop, 2 record */
    if (mMode == Mode::Hui)
    {
      const uint8_t note = which == 0 ? (uint8_t)kHuiPlay
                         : which == 1 ? (uint8_t)kHuiStop
                                      : (uint8_t)kHuiRecord;
      return SendHuiNote(note, 127, midi, refuse);
    }
    const uint8_t note = which == 0 ? (uint8_t)kPlay
                       : which == 1 ? (uint8_t)kStop
                                    : (uint8_t)kRec;
    return SendLiveNote(note, 127, midi, refuse);
  }

  bool PadClip(int pad0_7, uint8_t vel, skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (pad0_7 < 0 || pad0_7 > 7)
      return false;
    return SendLiveNote((uint8_t)(kPad0 + pad0_7), vel, midi, refuse);
  }

  bool Macro(int i0_7, uint8_t val, skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (i0_7 < 0 || i0_7 > 7)
      return false;
    return SendLiveCc((uint8_t)(kCcKnob0 + i0_7), val, midi, refuse);
  }

  bool Fader(int i0_3, uint8_t val, skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (i0_3 < 0 || i0_3 > 3)
      return false;
    if (mMode == Mode::Hui)
      return SendHuiStrip((uint8_t)((mBank * 4 + i0_3) & 7), val, midi, refuse);
    return SendLiveCc((uint8_t)(kCcFader0 + i0_3), val, midi, refuse);
  }

private:
  void RefreshStatus()
  {
    char buf[96];
    if (!mKb)
    {
      mStatus = "Surface: keyboard offline";
      return;
    }
    if (mMode == Mode::Off)
    {
      mStatus = "Surface: synth";
      return;
    }
    std::snprintf(buf, sizeof buf, "Surface: %s · %s · bank %d%s", ModeName(mMode),
                  mFace == LiveFace::Session ? "Session" : "Mixer", mBank,
                  mKeysToLive ? " · keys→Live" : "");
    mStatus = buf;
  }

  bool mKb = false;
  Mode mMode = Mode::Off;
  Mode mPending = Mode::Off;
  LiveFace mFace = LiveFace::Session;
  bool mKeysToLive = false;
  int mBank = 0;
  uint32_t mHoldMask = 0;
  int mHoldFrames = 0;
  std::string mStatus = "Surface: synth";
  WireFn mWire;
};

} // namespace sksurface
