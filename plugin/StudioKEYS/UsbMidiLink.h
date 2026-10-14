#pragma once

#include "MidiCables.h"
#include "sk_host.h"
#include "sk_skl.h"
#include "sk_sks.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

/** Real USB-MIDI bridge for cable 3 (link) + SKL/SKS wire helpers.
 * Mac: CoreMIDI. Windows: WinMM device scan (sysex I/O wired next).
 * Linux: stub until ALSA seq lands. */
namespace skusb {

class MidiLink
{
public:
  ~MidiLink() { Close(); }

  bool Open(const char* preferName = "StudioKEYS")
  {
    Close();
#if defined(__APPLE__)
    OSStatus err = MIDIClientCreate(CFSTR("StudioKEYS Micro"), nullptr, nullptr, &mClient);
    if (err)
    {
      mErr = "MIDIClientCreate failed";
      return false;
    }
    err = MIDIInputPortCreate(mClient, CFSTR("SK Link In"), ReadProc, this, &mInPort);
    if (err)
    {
      mErr = "MIDIInputPortCreate failed";
      Close();
      return false;
    }
    err = MIDIOutputPortCreate(mClient, CFSTR("SK Link Out"), &mOutPort);
    if (err)
    {
      mErr = "MIDIOutputPortCreate failed";
      Close();
      return false;
    }
    const ItemCount nSrc = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < nSrc; ++i)
    {
      MIDIEndpointRef ep = MIDIGetSource(i);
      if (NameHas(ep, preferName) || NameHas(ep, "SMK") || NameHas(ep, "Link"))
      {
        MIDIPortConnectSource(mInPort, ep, nullptr);
        mSrc = ep;
        break;
      }
    }
    const ItemCount nDst = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < nDst; ++i)
    {
      MIDIEndpointRef ep = MIDIGetDestination(i);
      if (NameHas(ep, preferName) || NameHas(ep, "SMK") || NameHas(ep, "Link"))
      {
        mDst = ep;
        break;
      }
    }
    mOpen = (mSrc != 0 || mDst != 0);
    if (!mOpen)
      mErr = "No StudioKEYS / SMK MIDI endpoint found";
    else
      mErr.clear();
    return mOpen;
#elif defined(_WIN32)
    /* Enumerate WinMM devices by name; full sysex open lands with DAW CI. */
    const UINT nIn = midiInGetNumDevs();
    const UINT nOut = midiOutGetNumDevs();
    for (UINT i = 0; i < nIn; ++i)
    {
      MIDIINCAPS caps {};
      if (midiInGetDevCaps(i, &caps, sizeof caps) != MMSYSERR_NOERROR)
        continue;
      if (NameHasWin(caps.szPname, preferName) || NameHasWin(caps.szPname, "SMK") ||
          NameHasWin(caps.szPname, "Link"))
      {
        mWinInId = i;
        break;
      }
    }
    for (UINT i = 0; i < nOut; ++i)
    {
      MIDIOUTCAPS caps {};
      if (midiOutGetDevCaps(i, &caps, sizeof caps) != MMSYSERR_NOERROR)
        continue;
      if (NameHasWin(caps.szPname, preferName) || NameHasWin(caps.szPname, "SMK") ||
          NameHasWin(caps.szPname, "Link"))
      {
        mWinOutId = i;
        break;
      }
    }
    mOpen = (mWinInId >= 0 || mWinOutId >= 0);
    if (!mOpen)
      mErr = "No StudioKEYS / SMK WinMM endpoint found";
    else
      mErr = "WinMM device found — sysex I/O not opened yet (use DAW MIDI for now)";
    /* Keep closed for I/O until midiInOpen/midiOutOpen sysex path is finished. */
    mOpen = false;
    return false;
#else
    (void) preferName;
    mErr = "USB MIDI link not built for Linux yet (ALSA seq TODO)";
    return false;
#endif
  }

  void Close()
  {
#if defined(__APPLE__)
    if (mInPort)
      MIDIPortDispose(mInPort);
    if (mOutPort)
      MIDIPortDispose(mOutPort);
    if (mClient)
      MIDIClientDispose(mClient);
    mClient = 0;
    mInPort = 0;
    mOutPort = 0;
    mSrc = 0;
    mDst = 0;
#elif defined(_WIN32)
    mWinInId = -1;
    mWinOutId = -1;
#endif
    mOpen = false;
  }

  bool IsOpen() const { return mOpen; }
  const std::string& Error() const { return mErr; }

  /** Send raw SysEx (or short message) to the keyboard link destination. */
  bool Send(const uint8_t* data, uint32_t n)
  {
    if (!mOpen || !data || !n)
      return false;
#if defined(__APPLE__)
    if (!mDst)
      return false;
    /* CoreMIDI packet list: small SysEx fits one packet; larger use MIDISendSysex. */
    if (n <= 256u)
    {
      MIDIPacketList pktList;
      MIDIPacket* pkt = MIDIPacketListInit(&pktList);
      pkt = MIDIPacketListAdd(&pktList, sizeof pktList, pkt, 0, n, data);
      if (!pkt)
        return false;
      return MIDISend(mOutPort, mDst, &pktList) == noErr;
    }
    MIDISysexSendRequest req {};
    req.destination = mDst;
    req.data = const_cast<UInt8*>(data);
    req.bytesToSend = n;
    req.complete = false;
    req.completionProc = nullptr;
    req.completionRefCon = nullptr;
    return MIDISendSysex(&req) == noErr;
#else
    (void) data;
    (void) n;
    return false;
#endif
  }

  /** Build + send SKL frame on the wire (does not touch in-process host). */
  bool SendSkl(uint8_t cmd, const uint8_t* payload, uint32_t np)
  {
    uint8_t f[96];
    uint32_t n = 0;
    f[n++] = 0xF0;
    f[n++] = SK_SKL_HDR0;
    f[n++] = SK_SKL_HDR1;
    f[n++] = SK_SKL_HDR2;
    f[n++] = SK_SKL_HDR3;
    f[n++] = cmd;
    for (uint32_t i = 0; i < np && n + 1u < sizeof f; ++i)
      f[n++] = (uint8_t)(payload[i] & 0x7Fu);
    f[n++] = 0xF7;
    return Send(f, n);
  }

  /** Build + send SKS surface frame on the wire. */
  bool SendSks(uint8_t cmd, uint8_t a, uint8_t b)
  {
    const uint8_t sx[] = {
      0xF0, SK_SKS_HDR0, SK_SKS_HDR1, SK_SKS_HDR2, SK_SKS_HDR3, cmd, a, b, 0xF7};
    return Send(sx, sizeof sx);
  }

  /** Legacy no-op: SKL/SKS wire traffic goes through LockSession/Surface SetWire
   * so Drain() and PushCmd do not race the outbox. Inbound USB still hits ReadProc. */
  void Pump() {}

private:
#if defined(__APPLE__)
  static void ReadProc(const MIDIPacketList* pktList, void* ref, void*)
  {
    auto* self = static_cast<MidiLink*>(ref);
    if (!self)
      return;
    const MIDIPacket* p = &pktList->packet[0];
    for (UInt32 i = 0; i < pktList->numPackets; ++i)
    {
      sk_host_link_push(p->data, p->length);
      p = MIDIPacketNext(p);
    }
  }

  static bool NameHas(MIDIEndpointRef ep, const char* needle)
  {
    if (!ep || !needle)
      return false;
    CFStringRef name = nullptr;
    MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name);
    if (!name)
      return false;
    char buf[128] = {};
    CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(name);
    return std::strstr(buf, needle) != nullptr;
  }

  MIDIClientRef mClient = 0;
  MIDIPortRef mInPort = 0;
  MIDIPortRef mOutPort = 0;
  MIDIEndpointRef mSrc = 0;
  MIDIEndpointRef mDst = 0;
#elif defined(_WIN32)
  static bool NameHasWin(const char* hay, const char* needle)
  {
    if (!hay || !needle)
      return false;
    return std::strstr(hay, needle) != nullptr;
  }
  int mWinInId = -1;
  int mWinOutId = -1;
#endif
  bool mOpen = false;
  std::string mErr;
};

} // namespace skusb
