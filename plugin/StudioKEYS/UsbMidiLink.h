/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "MidiCables.h"
#include "sk_host.h"
#include "sk_skl.h"
#include "sk_sks.h"
#include <algorithm>
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

  /** The MIDI endpoints a user can pick as the companion <-> CFW link (P12).
   *  macOS: CoreMIDI sources by display name. Windows: WinMM input names.
   *  Other hosts: empty (the link falls back to the DAW's own MIDI ports). */
  static std::vector<std::string> ListPorts()
  {
    std::vector<std::string> out;
#if defined(__APPLE__)
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i)
    {
      const std::string name = EndpointName(MIDIGetSource(i));
      if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end())
        out.push_back(name);
    }
#elif defined(_WIN32)
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i)
    {
      MIDIINCAPS caps {};
      if (midiInGetDevCaps(i, &caps, sizeof caps) != MMSYSERR_NOERROR)
        continue;
      const std::string name(caps.szPname);
      if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end())
        out.push_back(name);
    }
#endif
    return out;
  }

  /** Open the link. An empty `preferName` keeps the old name-based auto-detect
   *  (StudioKEYS / SMK / Link); a name picks that endpoint (exact first, then a
   *  substring) so the P12 port selector is honoured. */
  bool Open(const char* preferName = "")
  {
    return OpenNamed(preferName ? std::string(preferName) : std::string());
  }

  bool OpenNamed(const std::string& preferName)
  {
    mPortName = preferName;
    Close();

    auto match = [&](MIDIEndpointRef ep) {
      const std::string name = EndpointName(ep);
      if (preferName.empty())
        return name.find("StudioKEYS") != std::string::npos || name.find("SMK") != std::string::npos
               || name.find("Link") != std::string::npos;
      return name == preferName;
    };
    auto loose = [&](MIDIEndpointRef ep) {
      const std::string name = EndpointName(ep);
      return !preferName.empty() && name.find(preferName) != std::string::npos;
    };

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
    for (int pass = 0; pass < 2 && !mSrc; ++pass)
      for (ItemCount i = 0; i < nSrc; ++i)
      {
        MIDIEndpointRef ep = MIDIGetSource(i);
        if (pass ? loose(ep) : match(ep))
        {
          MIDIPortConnectSource(mInPort, ep, nullptr);
          mSrc = ep;
          if (mPortName.empty())
            mPortName = EndpointName(ep);
          break;
        }
      }
    const ItemCount nDst = MIDIGetNumberOfDestinations();
    for (int pass = 0; pass < 2 && !mDst; ++pass)
      for (ItemCount i = 0; i < nDst; ++i)
      {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        if (pass ? loose(ep) : match(ep))
        {
          mDst = ep;
          break;
        }
      }
    mOpen = (mSrc != 0 || mDst != 0);
    if (!mOpen)
      mErr = preferName.empty() ? "No StudioKEYS / SMK MIDI endpoint found"
                                : ("No MIDI endpoint named " + preferName);
    else
      mErr.clear();
    return mOpen;
#elif defined(_WIN32)
    /* Enumerate WinMM devices by name; full sysex open lands with DAW CI. */
    const UINT nIn = midiInGetNumDevs();
    const UINT nOut = midiOutGetNumDevs();
    for (UINT i = 0; i < nIn && mWinInId < 0; ++i)
    {
      MIDIINCAPS caps {};
      if (midiInGetDevCaps(i, &caps, sizeof caps) != MMSYSERR_NOERROR)
        continue;
      const bool hit = preferName.empty()
                         ? (NameHasWin(caps.szPname, "StudioKEYS") || NameHasWin(caps.szPname, "SMK")
                            || NameHasWin(caps.szPname, "Link"))
                         : NameHasWin(caps.szPname, preferName.c_str());
      if (hit)
      {
        mWinInId = (int) i;
        if (mPortName.empty())
          mPortName = caps.szPname;
      }
    }
    for (UINT i = 0; i < nOut && mWinOutId < 0; ++i)
    {
      MIDIOUTCAPS caps {};
      if (midiOutGetDevCaps(i, &caps, sizeof caps) != MMSYSERR_NOERROR)
        continue;
      const bool hit = preferName.empty()
                         ? (NameHasWin(caps.szPname, "StudioKEYS") || NameHasWin(caps.szPname, "SMK")
                            || NameHasWin(caps.szPname, "Link"))
                         : NameHasWin(caps.szPname, preferName.c_str());
      if (hit)
        mWinOutId = (int) i;
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
  const std::string& PortName() const { return mPortName; }

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
    const std::string name = EndpointName(ep);
    return name.find(needle) != std::string::npos;
  }

  /** The name a user sees in the P12 selector (CoreMIDI display name). */
  static std::string EndpointName(MIDIEndpointRef ep)
  {
    if (!ep)
      return {};
    CFStringRef name = nullptr;
    MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &name);
    if (!name)
      MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name);
    if (!name)
      return {};
    char buf[256] = {};
    CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(name);
    return std::string(buf);
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
  std::string mPortName; /* the endpoint the user picked (P12), "" when auto-detect won */
};

} // namespace skusb
