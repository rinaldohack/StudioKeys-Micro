#pragma once

#include "sk_host.h"
#include "sk_skl.h"
#include <cstring>
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
    PushCrc(sk_host_state_crc());
    PushCmd(SK_SKL_CHOOSE, &dir, 1);
    Drain();
    if (sk_host_lock_get() == SK_LOCK_ON)
      mMessage = "Locked. Continuous save on.";
    else
      mMessage = sk_host_lock_reject_reason()[0] ? sk_host_lock_reject_reason()
                                                 : "Sync rejected.";
  }

  void ChooseHwToPlugin()
  {
    const uint8_t dir = SK_SKL_DIR_HW_TO_PLUGIN;
    PushCrc(sk_host_state_crc());
    PushCmd(SK_SKL_CHOOSE, &dir, 1);
    Drain();
    if (sk_host_lock_get() == SK_LOCK_ON)
      mMessage = "Locked. Continuous save on.";
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

  /** Drain host→plugin outbox (HELLO_ACK, CRC_ACK, RESULT, EVENT). */
  void Drain()
  {
    uint8_t buf[256];
    uint32_t n;
    while ((n = sk_host_link_pop(buf, sizeof buf)) > 0)
      ConsumeOut(buf, n);
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
    pay[0] = (uint8_t)(crc & 0x7Fu);
    pay[1] = (uint8_t)((crc >> 7) & 0x7Fu);
    pay[2] = (uint8_t)((crc >> 14) & 0x7Fu);
    pay[3] = (uint8_t)((crc >> 21) & 0x7Fu);
    pay[4] = (uint8_t)((crc >> 28) & 0x0Fu);
    PushCmd(SK_SKL_CRC, pay, 5);
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
        /* Host→plugin ticks: host already applied via set_param when it emitted.
         * Real USB will apply here into the plugin mirror. */
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
};
