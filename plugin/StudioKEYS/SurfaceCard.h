#pragma once

#include "MidiCables.h"
#include "sk_host.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/** Plugin Live / HUI surface → link cable 3 → keyboard → DAW (plan phase 9). */
namespace sksurface {

enum class Mode : uint8_t { Off = 0, Live = 1, Hui = 2 };

inline const char* ModeName(Mode m)
{
  switch (m)
  {
    case Mode::Live: return "Ableton Live";
    case Mode::Hui: return "Mackie HUI";
    default: return "Synth";
  }
}

class SurfaceBridge
{
public:
  void SetKeyboardConnected(bool on) { mKb = on; }
  bool KeyboardConnected() const { return mKb; }

  Mode GetMode() const { return mMode; }

  /** Enter/leave Live or HUI via SMK side-key hold simulation. */
  void PulseHold(Mode want)
  {
    if (want == Mode::Live)
      mHoldMask = (1u << SK_X_RIGHT);
    else if (want == Mode::Hui)
      mHoldMask = (1u << SK_X_FADER);
    else
      mHoldMask = 0;
    mHoldFrames = 220; /* ~650 ms of ProcessBlock ticks so firmware sees the 500 ms hold */
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
      }
    }
  }

  /** Pad / transport / etc. Returns false and sets refuse if no keyboard. */
  bool SendLiveNote(uint8_t note, uint8_t vel, skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (!mKb)
    {
      refuse = "Keyboard not connected — Live/HUI leave on the keyboard's ports.";
      return false;
    }
    refuse.clear();
    /* Link SysEx: F0 7D 53 4B 53 (SKS surface) cable cmd note vel F7 */
    const uint8_t sx[] = {0xF0, 0x7D, 0x53, 0x4B, 0x53, 0x01, note, vel, 0xF7};
    sk_host_link_push(sx, sizeof sx);
    midi.Enqueue(skmidi::Cable::Link, 0xF0, note, vel);
    return true;
  }

  bool SendLiveCc(uint8_t cc, uint8_t val, skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (!mKb)
    {
      refuse = "Keyboard not connected — Live/HUI leave on the keyboard's ports.";
      return false;
    }
    refuse.clear();
    const uint8_t sx[] = {0xF0, 0x7D, 0x53, 0x4B, 0x53, 0x02, cc, val, 0xF7};
    sk_host_link_push(sx, sizeof sx);
    midi.Enqueue(skmidi::Cable::Link, 0xB0, cc, val);
    return true;
  }

  bool SendHuiStrip(uint8_t strip, uint8_t fader, skmidi::MidiRouter& midi, std::string& refuse)
  {
    if (!mKb)
    {
      refuse = "Keyboard not connected — Live/HUI leave on the keyboard's ports.";
      return false;
    }
    refuse.clear();
    const uint8_t sx[] = {0xF0, 0x7D, 0x53, 0x4B, 0x53, 0x11, strip, fader, 0xF7};
    sk_host_link_push(sx, sizeof sx);
    midi.Enqueue(skmidi::Cable::Link, 0xE0 | (strip & 0x0F), fader, fader);
    return true;
  }

private:
  bool mKb = false;
  Mode mMode = Mode::Off;
  Mode mPending = Mode::Off;
  uint32_t mHoldMask = 0;
  int mHoldFrames = 0;
};

} // namespace sksurface
