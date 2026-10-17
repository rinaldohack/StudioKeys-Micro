/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "sk_host.h"
#include "sk_skl.h"
#include <cstring>
#include <functional>
#include <string>

/** USB lock UI + SKL link protocol + one-instance seat (plan phase 6).
 *
 * Cable-3 SysEx: F0 7D 53 4B 4C cmd … F7  (see host/README.md / sk_skl.h)
 *   01 HELLO / 02 HELLO_ACK — version strings
 *   03 CRC / 04 CRC_ACK   — snapshot CRC32 (5×7-bit)
 *   05 CHOOSE / 06 RESULT — direction; refuse until CRC matches
 *   07 EVENT              — param id + value (event log)
 *   08 DROP / 09 DISMISS
 */
class LockSession
{
public:
  using WireFn = std::function<void(const uint8_t* data, uint32_t n)>;

  /** Optional USB mirror for SKL frames (plugin→keyboard on cable 3). */
  void SetWire(WireFn wire) { mWire = std::move(wire); }

  bool Claim()
  {
    if (!mClaimed)
      mClaimed = sk_host_lock_claim_instance() != 0;
    return mClaimed;
  }

  void Release()
  {
    if (mClaimed)
    {
      PushCmd(SK_SKL_DROP, nullptr, 0);
      Drain();
      sk_host_lock_release_instance();
      mClaimed = false;
    }
  }

  bool OwnsSeat() const { return mClaimed; }

  void OnPlugDetected()
  {
    if (!mClaimed && !Claim())
    {
      mMessage = "Refuse: another instance already owns a keyboard lock.";
      return;
    }
    /* Plugin → host HELLO; host answers HELLO_ACK + CRC_ACK (in-process). */
    PushHello(sk_host_version());
    Drain();
    if (sk_host_lock_get() == SK_LOCK_ASK)
      mMessage = "Sync: Plugin → Keyboard, or Keyboard → Plugin?";
    else if (sk_host_lock_reject_reason()[0])
      mMessage = sk_host_lock_reject_reason();
    else
      mMessage = "Link hello sent — waiting for peer.";
  }

  /** Test helper: HELLO with an older build (host refuses). */
  void SimulateOldPeer()
  {
    if (!mClaimed)
      Claim();
    PushHello("v0.0.0-old");
    Drain();
    mMessage = sk_host_lock_reject_reason()[0] ? sk_host_lock_reject_reason()
                                               : "Peer reports an older build.";
  }

  void ChoosePluginToHw()
  {
    const uint8_t dir = SK_SKL_DIR_PLUGIN_TO_HW;
    PushSnap(dir);
    PushCrc(sk_host_state_crc());
    PushCmd(SK_SKL_CHOOSE, &dir, 1);
    Drain();
    if (sk_host_lock_get() == SK_LOCK_ON)
      mMessage = "Locked. Snapshot saved. Continuous save on.";
    else
      mMessage = sk_host_lock_reject_reason()[0] ? sk_host_lock_reject_reason()
                                                 : "Sync rejected.";
  }

  void ChooseHwToPlugin()
  {
    const uint8_t dir = SK_SKL_DIR_HW_TO_PLUGIN;
    /* Keyboard → plugin: host loads its project; still announce CRC then CHOOSE. */
    PushCrc(sk_host_state_crc());
    PushCmd(SK_SKL_CHOOSE, &dir, 1);
    Drain();
    if (sk_host_lock_get() == SK_LOCK_ON)
      mMessage = "Locked. Loaded keyboard snapshot. Continuous save on.";
    else
      mMessage = sk_host_lock_reject_reason()[0] ? sk_host_lock_reject_reason()
                                                 : "Sync rejected.";
  }

  void Dismiss()
  {
    PushCmd(SK_SKL_DISMISS, nullptr, 0);
    Drain();
    mMessage = "Dismissed. Both sides stay independent.";
  }

  void Drop()
  {
    PushCmd(SK_SKL_DROP, nullptr, 0);
    Drain();
    mMessage = "Lock dropped.";
  }

  void Autosave()
  {
    if (sk_host_lock_get() == SK_LOCK_ON)
      sk_host_lock_autosave();
  }

  /** Drain host→plugin outbox (HELLO_ACK, CRC_ACK, RESULT, EVENT).
   * Also mirrors each frame on the USB wire when SetWire was installed. */
  void Drain()
  {
    uint8_t buf[256];
    uint32_t n;
    while ((n = sk_host_link_pop(buf, sizeof buf)) > 0)
    {
      if (mWire)
        mWire(buf, n);
      ConsumeOut(buf, n);
    }
  }

  /** Log a param change on the link while locked (plugin → host EVENT). */
  void LogParam(uint32_t id, int32_t value)
  {
    if (sk_host_lock_get() != SK_LOCK_ON)
      return;
    uint8_t pay[4];
    uint32_t u = (uint32_t)(value < -8192 ? 0 : (value > 8191 ? 16383 : value + 8192));
    pay[0] = (uint8_t)(id & 0x7Fu);
    pay[1] = (uint8_t)((id >> 7) & 0x7Fu);
    pay[2] = (uint8_t)(u & 0x7Fu);
    pay[3] = (uint8_t)((u >> 7) & 0x7Fu);
    PushCmd(SK_SKL_EVENT, pay, 4);
  }

  int State() const { return sk_host_lock_get(); }
  const std::string& Message() const { return mMessage; }
  uint32_t PeerCrc() const { return mPeerCrc; }

private:
  void PushHello(const char* ver)
  {
    uint8_t pay[32];
    uint32_t k = 0;
    if (!ver)
      ver = "";
    for (; ver[k] && k + 1u < sizeof pay; k++)
      pay[k] = (uint8_t)ver[k];
    pay[k++] = 0;
    PushCmd(SK_SKL_HELLO, pay, k);
  }

  void PushCrc(uint32_t crc)
  {
    uint8_t pay[5];
    PutU32_7(pay, crc);
    PushCmd(SK_SKL_CRC, pay, 5);
  }

  static void PutU32_7(uint8_t* d, uint32_t v)
  {
    d[0] = (uint8_t)(v & 0x7Fu);
    d[1] = (uint8_t)((v >> 7) & 0x7Fu);
    d[2] = (uint8_t)((v >> 14) & 0x7Fu);
    d[3] = (uint8_t)((v >> 21) & 0x7Fu);
    d[4] = (uint8_t)((v >> 28) & 0x0Fu);
  }

  static uint32_t Crc32(const uint8_t* p, uint32_t n)
  {
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++)
    {
      c ^= p[i];
      for (int b = 0; b < 8; b++)
        c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
  }

  /** Project snapshot over SKL (nybble-packed chunks for 7-bit MIDI safety). */
  void PushSnap(uint8_t dir)
  {
    uint8_t blob[4096];
    const uint32_t n = sk_host_snap_bytes(blob, sizeof blob);
    if (!n || n > sizeof blob)
      return;
    const uint32_t crc = Crc32(blob, n);
    uint8_t begin[11];
    begin[0] = dir;
    PutU32_7(begin + 1, n);
    PutU32_7(begin + 6, crc);
    PushCmd(SK_SKL_SNAP_BEGIN, begin, 11);
    /* 24 source bytes → 48 nybbles per CHUNK (seq index in payload). */
    uint32_t off = 0;
    uint16_t seq = 0;
    while (off < n)
    {
      uint8_t pay[50];
      uint32_t pn = 0;
      pay[pn++] = (uint8_t)(seq & 0x7Fu);
      pay[pn++] = (uint8_t)((seq >> 7) & 0x7Fu);
      for (uint32_t k = 0; k < 24u && off < n; k++, off++)
      {
        pay[pn++] = (uint8_t)((blob[off] >> 4) & 0x0Fu);
        pay[pn++] = (uint8_t)(blob[off] & 0x0Fu);
      }
      PushCmd(SK_SKL_SNAP_CHUNK, pay, pn);
      seq++;
    }
    uint8_t end[5];
    PutU32_7(end, crc);
    PushCmd(SK_SKL_SNAP_END, end, 5);
    Drain();
  }

  void PushCmd(uint8_t cmd, const uint8_t* payload, uint32_t np)
  {
    uint8_t f[96];
    uint32_t n = 0, i;
    f[n++] = 0xF0;
    f[n++] = SK_SKL_HDR0;
    f[n++] = SK_SKL_HDR1;
    f[n++] = SK_SKL_HDR2;
    f[n++] = SK_SKL_HDR3;
    f[n++] = cmd;
    for (i = 0; i < np && n + 1u < sizeof f; i++)
      f[n++] = (uint8_t)(payload[i] & 0x7Fu);
    f[n++] = 0xF7;
    sk_host_link_push(f, n);
    if (mWire)
      mWire(f, n);
  }

  void ConsumeOut(const uint8_t* src, uint32_t n)
  {
    for (uint32_t i = 0; i < n; i++)
    {
      uint8_t b = src[i];
      if (b == 0xF0u)
      {
        mSxOn = true;
        mSxN = 0;
      }
      if (!mSxOn)
        continue;
      if (mSxN < sizeof mSx)
        mSx[mSxN++] = b;
      if (b == 0xF7u)
      {
        mSxOn = false;
        HandleFrame(mSx, mSxN);
        mSxN = 0;
      }
    }
  }

  void HandleFrame(const uint8_t* f, uint32_t n)
  {
    if (n < 7u || f[1] != SK_SKL_HDR0 || f[2] != SK_SKL_HDR1 || f[3] != SK_SKL_HDR2 ||
        f[4] != SK_SKL_HDR3)
      return;
    switch (f[5])
    {
      case SK_SKL_HELLO_ACK:
        if (n > 7u)
        {
          char ver[32];
          uint32_t m = 0;
          for (uint32_t i = 6; i + 1u < n && m + 1u < sizeof ver; i++)
          {
            if (!f[i])
              break;
            ver[m++] = (char)f[i];
          }
          ver[m] = 0;
          sk_host_lock_set_peer_version(ver);
        }
        break;
      case SK_SKL_CRC_ACK:
        if (n >= 12u)
        {
          mPeerCrc = (uint32_t)f[6] | (uint32_t)f[7] << 7 | (uint32_t)f[8] << 14 |
                     (uint32_t)f[9] << 21 | (uint32_t)(f[10] & 0x0Fu) << 28;
        }
        break;
      case SK_SKL_RESULT:
        if (n >= 8u && f[6] != SK_SKL_OK && sk_host_lock_reject_reason()[0])
          mMessage = sk_host_lock_reject_reason();
        break;
      case SK_SKL_EVENT:
        /* USB peer / outbox EVENT: apply into host mirror (idempotent if already set). */
        if (n >= 11u && sk_host_lock_get() == SK_LOCK_ON)
        {
          const uint32_t id = (uint32_t)f[6] | (uint32_t)f[7] << 7;
          const int32_t v = (int32_t)((uint32_t)f[8] | (uint32_t)f[9] << 7) - 8192;
          if (id == SK_SKL_META_PALETTE)
            sk_host_set_palette((uint32_t) v);
          else if (id == SK_SKL_META_LANG)
            sk_host_set_lang(v ? 1 : 0);
          else if (id == SK_SKL_META_RATE_PPM)
            sk_host_set_hw_rate_ppm(v);
          else if (id == SK_SKL_META_RATE_TRIM)
            sk_host_set_hw_rate_trim(v);
          else
            sk_host_set_param(id, v);
        }
        break;
      default:
        break;
    }
  }

  bool mClaimed = false;
  std::string mMessage {"Unplugged. Plugin is the whole instrument."};
  uint32_t mPeerCrc = 0;
  uint8_t mSx[128] {};
  uint32_t mSxN = 0;
  bool mSxOn = false;
  WireFn mWire;
};
