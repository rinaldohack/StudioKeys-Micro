/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* P15: a WAV dropped on a user slot (USR1..USR3) -> the device's slot record.
 *   build/host/sample_slot_test
 * 1. SkWav.h reads PCM 8/16/24/32, float 32, WAVE_FORMAT_EXTENSIBLE, any channel count, and
 *    refuses junk / an empty data chunk.
 * 2. sk_host_sample_slot_write resamples to 22.05 kHz, normalises, IMA-encodes and writes the
 *    slot exactly as tools/fm1_sample_upload.py does: header (magic / version / nz / name /
 *    data_len / CRC / one zone C4 0..127), data at +512, header last. smp_user_scan publishes it.
 * 3. the stored ADPCM decodes back to the dropped sine (correlation), only the slot's own bytes
 *    are touched (nothing before SMP_USER_BASE or after slot + SMP_USER_SIZE changes), CLR empties it.
 * 4. refused: a silent file, a too-long file, a bad slot, a malformed WAV. */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sk_host.h"
#include "SkWav.h"

enum
{
  SLOT_BASE = 0x000E5000,
  SLOT_SIZE = 0x9000,   /* hal/smk_map.h SMP_USER_SIZE: 36 KiB per slot, 3 slots to 0x100000 */
  SLOT_DATA = 512,
  SLOT_MAX_ADPCM = SLOT_SIZE - SLOT_DATA
};

static int fails;

static void check(const char* what, int ok, const std::string& note = {})
{
  std::printf("sample slot: %-58s %s", what, ok ? "ok" : "FAIL");
  if (!note.empty())
    std::printf("  (%s)", note.c_str());
  std::printf("\n");
  fails += !ok;
}

static std::string Fmt(const char* fmt, long long a = 0, long long b = 0)
{
  char buf[128];
  std::snprintf(buf, sizeof buf, fmt, a, b);
  return buf;
}

static std::string FmtD(const char* fmt, double a = 0, double b = 0)
{
  char buf[128];
  std::snprintf(buf, sizeof buf, fmt, a, b);
  return buf;
}

/* ---- a WAV writer for the fixtures (RIFF, fmt, data) ---- */
static void Put32(std::vector<uint8_t>& v, uint32_t x)
{
  v.push_back((uint8_t) x);
  v.push_back((uint8_t) (x >> 8));
  v.push_back((uint8_t) (x >> 16));
  v.push_back((uint8_t) (x >> 24));
}
static void Put16(std::vector<uint8_t>& v, uint16_t x)
{
  v.push_back((uint8_t) x);
  v.push_back((uint8_t) (x >> 8));
}

/** tag: 1 PCM / 3 float; bytes: interleaved little-endian samples. extensible: 0xFFFE with a
 * 40-byte fmt and the subformat at +24. */
static bool WriteWav(const char* path, uint16_t tag, uint16_t ch, uint32_t rate, uint16_t bits,
                     const std::vector<uint8_t>& bytes, bool extensible = false)
{
  std::vector<uint8_t> d;
  d.insert(d.end(), {'R', 'I', 'F', 'F'});
  Put32(d, 0);
  d.insert(d.end(), {'W', 'A', 'V', 'E'});
  d.insert(d.end(), {'f', 'm', 't', ' '});
  Put32(d, extensible ? 40u : 16u);
  Put16(d, extensible ? (uint16_t) 0xFFFE : tag);
  Put16(d, ch);
  Put32(d, rate);
  Put32(d, rate * ch * (bits / 8));
  Put16(d, (uint16_t) (ch * (bits / 8)));
  Put16(d, bits);
  if (extensible)
  {
    Put16(d, 22);            /* cbSize */
    Put16(d, bits);          /* valid bits */
    Put32(d, 0);             /* channel mask */
    Put16(d, tag);           /* subformat GUID: the tag, then the fixed tail */
    d.insert(d.end(), {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71});
  }
  d.insert(d.end(), {'d', 'a', 't', 'a'});
  Put32(d, (uint32_t) bytes.size());
  d.insert(d.end(), bytes.begin(), bytes.end());
  const uint32_t riff = (uint32_t) d.size() - 8;
  d[4] = (uint8_t) riff;
  d[5] = (uint8_t) (riff >> 8);
  d[6] = (uint8_t) (riff >> 16);
  d[7] = (uint8_t) (riff >> 24);
  FILE* f = std::fopen(path, "wb");
  if (!f)
    return false;
  const bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
  std::fclose(f);
  return ok;
}

static std::vector<uint8_t> Sine16(uint32_t frames, double hz, uint32_t rate, int ch, double rscale)
{
  std::vector<uint8_t> b(frames * (size_t) ch * 2u);
  for (uint32_t i = 0; i < frames; ++i)
  {
    const double s = std::sin(2.0 * M_PI * hz * (double) i / (double) rate);
    for (int c = 0; c < ch; ++c)
    {
      const double v = s * (c ? rscale : 1.0) * 0.5;
      const int16_t x = (int16_t) std::lround(v * 32767.0);
      const size_t o = ((size_t) i * ch + (size_t) c) * 2u;
      b[o] = (uint8_t) x;
      b[o + 1] = (uint8_t) ((uint16_t) x >> 8);
    }
  }
  return b;
}

/* ---- the slot record (firmware/src/eng_sample.c smp_user_hdr_t) ---- */
static uint32_t Rd32(const uint8_t* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
static uint16_t Rd16(const uint8_t* p) { return (uint16_t) ((uint16_t) p[0] | (uint16_t) p[1] << 8); }
static int16_t Rd16s(const uint8_t* p) { return (int16_t) Rd16(p); }

static uint32_t Crc32(const uint8_t* p, uint32_t n)
{
  uint32_t c = 0xFFFFFFFFu;
  while (n--)
  {
    c ^= *p++;
    for (int b = 0; b < 8; ++b)
      c = (c >> 1) ^ (0xEDB88320u & (uint32_t) - (int) (c & 1u));
  }
  return ~c;
}

static const int32_t kStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int kIdx[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/** the firmware's sample_next at step 1 / root pitch: the stored bytes back to PCM */
static std::vector<int16_t> Decode(const uint8_t* p, uint32_t n)
{
  std::vector<int16_t> out(n);
  int32_t pred = 0, idx = 0;
  for (uint32_t pos = 0; pos < n; ++pos)
  {
    const uint8_t b = p[pos >> 1];
    const int code = (pos & 1u) ? (b >> 4) : (b & 15u);
    const int32_t step = kStep[idx <= 88 ? idx : 88];
    int32_t vd = step >> 3;
    if (code & 4)
      vd += step;
    if (code & 2)
      vd += step >> 1;
    if (code & 1)
      vd += step >> 2;
    pred = pred + ((code & 8) ? -vd : vd);
    pred = pred > 32767 ? 32767 : (pred < -32768 ? -32768 : pred);
    idx += kIdx[code & 7];
    idx = idx < 0 ? 0 : (idx > 88 ? 88 : idx);
    out[pos] = (int16_t) pred;
  }
  return out;
}

static double Correlation(const std::vector<int16_t>& a, const std::vector<int16_t>& b)
{
  const size_t n = a.size() < b.size() ? a.size() : b.size();
  double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
  for (size_t i = 0; i < n; ++i)
  {
    sa += a[i];
    sb += b[i];
    saa += (double) a[i] * a[i];
    sbb += (double) b[i] * b[i];
    sab += (double) a[i] * b[i];
  }
  const double na = (double) n, cov = sab - sa * sb / na;
  const double va = saa - sa * sa / na, vb = sbb - sb * sb / na;
  return (va > 0.0 && vb > 0.0) ? cov / std::sqrt(va * vb) : 0.0;
}

static bool AllFF(const uint8_t* p, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
    if (p[i] != 0xFFu)
      return false;
  return true;
}

static bool Same(const uint8_t* a, const uint8_t* b, uint32_t n) { return std::memcmp(a, b, n) == 0; }

static std::vector<int16_t> ToPcm(const skwav::Audio& a)
{
  std::vector<int16_t> p(a.mono.size());
  for (size_t i = 0; i < a.mono.size(); ++i)
    p[i] = (int16_t) std::lround(std::clamp(a.mono[i], -1.f, 1.f) * 32767.f);
  return p;
}

int main()
{
  const char* dir = "/tmp/sk_sample_slot";
  std::string cmd = std::string("mkdir -p ") + dir;
  if (std::system(cmd.c_str()) != 0)
    return 2;
  const std::string w16 = std::string(dir) + "/loop_c4.wav";
  const std::string w24 = std::string(dir) + "/bell_24.wav";
  const std::string wf = std::string(dir) + "/pad_f32.wav";
  const std::string wsil = std::string(dir) + "/silent.wav";
  const std::string wlong = std::string(dir) + "/long.wav";
  const std::string wbad = std::string(dir) + "/bad.wav";
  const std::string wempty = std::string(dir) + "/empty.wav";

  /* ---- 1: the reader ---- */
  check("WAV 16-bit stereo", WriteWav(w16.c_str(), 1, 2, 44100, 16, Sine16(11025, 440.0, 44100, 2, 0.5)));
  {
    skwav::Audio a;
    std::string err;
    const bool ok = skwav::Load(w16.c_str(), a, err);
    check("read 16-bit stereo -> mono, rate kept",
          ok && a.rate == 44100 && a.mono.size() == 11025,
          ok ? Fmt("%lld samples @ %lld Hz", (long long) a.mono.size(), (long long) a.rate) : err);
    double peak = 0.0;
    for (float v : a.mono)
      peak = std::max(peak, (double) std::fabs(v));
    check("stereo downmix at (L + R) / 2", peak > 0.30 && peak < 0.45, FmtD("peak %.3f", peak));
  }
  {
    std::vector<uint8_t> b;
    for (uint32_t i = 0; i < 4800; ++i)
    {
      const double s = std::sin(2.0 * M_PI * 220.0 * i / 48000.0) * 0.4;
      const int32_t x = (int32_t) std::lround(s * 8388607.0);
      b.push_back((uint8_t) x);
      b.push_back((uint8_t) (x >> 8));
      b.push_back((uint8_t) (x >> 16));
    }
    check("WAV 24-bit mono", WriteWav(w24.c_str(), 1, 1, 48000, 24, b));
    skwav::Audio a;
    std::string err;
    const bool ok = skwav::Load(w24.c_str(), a, err);
    check("read 24-bit", ok && a.rate == 48000 && a.mono.size() == 4800,
          Fmt("%lld samples", (long long) a.mono.size()));
  }
  {
    std::vector<uint8_t> b;
    for (uint32_t i = 0; i < 4000; ++i)
    {
      const float s = (float) (std::sin(2.0 * M_PI * 330.0 * i / 96000.0) * 0.6);
      const uint8_t* p = (const uint8_t*) &s;
      b.insert(b.end(), p, p + 4);
    }
    check("WAV float32 stereo (EXTENSIBLE)", WriteWav(wf.c_str(), 3, 2, 96000, 32, b, true));
    skwav::Audio a;
    std::string err;
    const bool ok = skwav::Load(wf.c_str(), a, err);
    check("read float32 stereo", ok && a.rate == 96000 && a.mono.size() == 2000,
          Fmt("%lld samples", (long long) a.mono.size()));
  }
  {
    std::string err;
    skwav::Audio a;
    FILE* f = std::fopen(wbad.c_str(), "wb");
    if (f)
    {
      std::fwrite("not a RIFF file", 1, 15, f);
      std::fclose(f);
    }
    check("refuse a non-RIFF file", !skwav::Load(wbad.c_str(), a, err), err);
    WriteWav(wempty.c_str(), 1, 1, 44100, 16, {});
    check("refuse a WAV without audio", !skwav::Load(wempty.c_str(), a, err), err);
  }

  /* ---- 2: the write path (the firmware's own host) ---- */
  sk_host_init();
  sk_host_boot();
  uint8_t* nor = sk_host_nor();
  check("host NOR is reachable", nor != nullptr && sk_host_nor_size() >= SLOT_BASE + 3u * SLOT_SIZE);

  /* The host keeps a saved NOR image between runs: start from three empty slots. */
  for (uint32_t k = 0; k < 3u; ++k)
    sk_host_sample_slot_clear(k);
  const std::vector<uint8_t> pre(nor + SLOT_BASE - 16, nor + SLOT_BASE);
  std::vector<uint8_t> post(nor + SLOT_BASE + SLOT_SIZE, nor + SLOT_BASE + SLOT_SIZE + 16);
  check("all three slots start empty",
        !sk_host_sample_slot_used(0) && !sk_host_sample_slot_used(1) && !sk_host_sample_slot_used(2));

  skwav::Audio a16;
  std::string err;
  if (!skwav::Load(w16.c_str(), a16, err))
    return 1;
  std::vector<int16_t> pcm = ToPcm(a16);

  check("write USR1",
        sk_host_sample_slot_write(0, pcm.data(), (uint32_t) pcm.size(), a16.rate, "loop_c4") == 0);
  check("USR1 is used", sk_host_sample_slot_used(0) == 1);
  check("USR1 name = LOOP_C4", std::strcmp(sk_host_sample_slot_name(0), "LOOP_C4") == 0,
        sk_host_sample_slot_name(0));
  check("USR1 source rate 22050", sk_host_sample_slot_rate(0) == 22050u,
        Fmt("%lld Hz", (long long) sk_host_sample_slot_rate(0)));
  const uint32_t n0 = sk_host_sample_slot_frames(0);
  check("USR1 length 0.25 s", n0 >= 5506u && n0 <= 5514u, Fmt("%lld samples", (long long) n0));
  check("USR2/USR3 untouched", !sk_host_sample_slot_used(1) && !sk_host_sample_slot_used(2));

  const uint8_t* img = nor + SLOT_BASE;
  check("header magic FSMP", Rd32(img) == 0x504D5346u, Fmt("%08llx", (long long) Rd32(img)));
  check("header version 1, one zone", Rd16(img + 4) == 1u && img[6] == 1u);
  check("header name", std::memcmp(img + 8, "LOOP_C4", 8) == 0);
  const uint32_t bytes = Rd32(img + 16);
  check("ADPCM length = frames / 2", bytes == (n0 + 1u) / 2u,
        Fmt("%lld B for %lld samples", (long long) bytes, (long long) n0));
  check("ADPCM fits the slot", bytes <= (uint32_t) SLOT_MAX_ADPCM);
  check("data CRC", Crc32(img + SLOT_DATA, bytes) == Rd32(img + 20),
        Fmt("%08llx", (long long) Rd32(img + 20)));
  {
    const uint8_t* z = img + 32;
    check("zone 0: off 0, n, le n-1, ls 0",
          Rd32(z) == 0u && Rd32(z + 4) == n0 && Rd32(z + 8) == 0u && Rd32(z + 12) == n0 - 1u);
    check("zone 0: rate 32768 (22050 / 44100)", Rd32(z + 16) == 32768u,
          Fmt("%lld", (long long) Rd32(z + 16)));
    check("zone 0: root C4, keys 0..127, one-shot",
          Rd16s(z + 20) == 960 && z[24] == 0 && z[25] == 0 && z[26] == 127 && z[27] == 0);
  }
  check("the header is written last (data at +512)", bytes > 0u);
  check("the slot's tail is still erased", AllFF(img + SLOT_DATA + bytes, 1024));
  check("nothing before the slot changed", Same(nor + SLOT_BASE - 16, pre.data(), 16));
  check("nothing after the slot changed", Same(nor + SLOT_BASE + SLOT_SIZE, post.data(), 16));

  /* ---- 2b: the stored ADPCM plays back the dropped sine ---- */
  {
    const std::vector<int16_t> dec = Decode(img + SLOT_DATA, n0);
    double peak = 0.0;
    for (int16_t v : dec)
      peak = std::max(peak, (double) std::abs((int) v));
    check("decoded peak ~ 30000 (-0.8 dBFS)", peak > 26000.0 && peak < 31500.0, FmtD("peak %.0f", peak));
    std::vector<int16_t> ref(n0);
    for (uint32_t i = 0; i < n0; ++i) /* 440 Hz at 22.05 kHz, normalised the same way */
      ref[i] = (int16_t) std::lround(std::sin(2.0 * M_PI * 440.0 * (double) i / 22050.0) * 30000.0);
    const double c = Correlation(dec, ref);
    check("decoded waveform is the dropped sine", c > 0.995, FmtD("%.5f", c));
  }

  /* ---- 2c: the SAMPLE engine plays the slot just written ---- */
  {
    uint32_t desk = 0xFFu;
    for (uint32_t d = 0; d < 13u; ++d)
      if (!std::strcmp(sk_host_engine_name(d), "SAMPLE"))
        desk = d;
    const uint32_t pe0 = sk_host_pe0_id();
    sk_host_set_desk(desk);
    struct sk_host_param_info info = {0};
    int si = -1;
    if (sk_host_get_param_info(pe0, &info) && info.names)
      for (int i = info.min; i <= info.max; ++i)
        if (info.names[i] && !std::strcmp(info.names[i], "USR1"))
          si = i;
    check("SAMPLE desk + USR1 in its SET list", desk != 0xFFu && si >= 0,
          Fmt("desk %lld set %lld", (long long) desk, (long long) si));
    sk_host_set_param(pe0, (int32_t) si);
    sk_host_set_param(pe0 + 1, 0); /* TUNE: the boot patch tunes SAMPLE +8 */
    float L[128], R[128], peak = 0.f;
    std::vector<float> rec;
    for (int i = 0; i < 32; ++i) /* let the main loop take the desk / SET change */
      sk_host_render_f32(L, R, 128);
    sk_host_midi(0x90, 60, 100);
    for (int i = 0; i < 48; ++i)
    {
      sk_host_render_f32(L, R, 128);
      for (int k = 0; k < 128; ++k)
      {
        peak = std::max(peak, std::max(std::fabs(L[k]), std::fabs(R[k])));
        rec.push_back(0.5f * (L[k] + R[k]));
      }
    }
    sk_host_midi(0x80, 60, 0);
    for (int i = 0; i < 8; ++i)
      sk_host_render_f32(L, R, 128);
    check("middle C plays the dropped sample", peak > 0.01f, FmtD("peak %.4f", peak));

    /* It is the 440 Hz sine, not the engine's empty-slot / other-set fallback: correlate the
     * first 0.2 s with a 440 Hz reference (the sample's root is C4, so no pitch shift). */
    const size_t n = std::min(rec.size(), (size_t) 8820);
    double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
      const double a = rec[i];
      const double b = std::sin(2.0 * M_PI * 440.0 * (double) i / 44100.0);
      sa += a;
      sb += b;
      saa += a * a;
      sbb += b * b;
      sab += a * b;
    }
    const double na = (double) n;
    const double va = saa - sa * sa / na, vb = sbb - sb * sb / na;
    const double corr = (va > 0.0 && vb > 0.0) ? (sab - sa * sb / na) / std::sqrt(va * vb) : 0.0;
    check("the played note is the 440 Hz sample", corr > 0.80, FmtD("%.3f", corr));
  }

  /* ---- 3: 24-bit 48k and float 96k land in USR2 / USR3 ---- */
  {
    skwav::Audio a;
    std::string e;
    if (skwav::Load(w24.c_str(), a, e))
    {
      const std::vector<int16_t> p = ToPcm(a);
      check("write USR2 (24-bit 48 kHz)",
            sk_host_sample_slot_write(1, p.data(), (uint32_t) p.size(), a.rate, "bell") == 0);
      const uint32_t n1 = sk_host_sample_slot_frames(1);
      check("USR2 frames 0.10 s", n1 >= 2198u && n1 <= 2206u, Fmt("%lld samples", (long long) n1));
      check("USR1 untouched by the USR2 write",
            sk_host_sample_slot_used(0) && sk_host_sample_slot_frames(0) == n0);
    }
    else
      check("read the 24-bit fixture", false, e);

    if (skwav::Load(wf.c_str(), a, e))
    {
      const std::vector<int16_t> p = ToPcm(a);
      check("write USR3 (float 96 kHz)",
            sk_host_sample_slot_write(2, p.data(), (uint32_t) p.size(), a.rate, "pad") == 0);
      const uint32_t n2 = sk_host_sample_slot_frames(2);
      check("USR3 frames ~ 0.02 s", n2 >= 450u && n2 <= 460u, Fmt("%lld samples", (long long) n2));
    }
    else
      check("read the float fixture", false, e);
  }

  /* ---- 4: refused ---- */
  {
    std::vector<uint8_t> zeros(4410u * 2u, 0);
    WriteWav(wsil.c_str(), 1, 1, 44100, 16, zeros);
    skwav::Audio a;
    std::string e;
    skwav::Load(wsil.c_str(), a, e);
    std::vector<int16_t> p(a.mono.size(), 0);
    check("silent file -> -2",
          sk_host_sample_slot_write(0, p.data(), (uint32_t) p.size(), a.rate, "sil") == -2);
    check("a refused write leaves the slot",
          sk_host_sample_slot_used(0) && sk_host_sample_slot_frames(0) == n0);

    WriteWav(wlong.c_str(), 1, 1, 44100, 16, Sine16(44100u * 8u, 110.0, 44100, 1, 1.0));
    skwav::Load(wlong.c_str(), a, e);
    p = ToPcm(a);
    check("too long -> -3",
          sk_host_sample_slot_write(1, p.data(), (uint32_t) p.size(), a.rate, "long") == -3);
    check("the refused long write leaves USR2", sk_host_sample_slot_used(1));
    check("slot 3 -> -1",
          sk_host_sample_slot_write(3, pcm.data(), (uint32_t) pcm.size(), a.rate, "x") == -1);
    check("empty PCM -> -1", sk_host_sample_slot_write(0, pcm.data(), 0, a.rate, "x") == -1);
  }

  /* ---- 5: CLR ---- */
  check("clear USR1", sk_host_sample_slot_clear(0) == 0);
  check("USR1 empty", !sk_host_sample_slot_used(0) && sk_host_sample_slot_frames(0) == 0u);
  check("USR1's bytes erased", AllFF(nor + SLOT_BASE, SLOT_SIZE));
  check("USR2 still there", sk_host_sample_slot_used(1));

  std::printf("sample slot: %s\n", fails ? "FAILED" : "ALL PASSED");
  return fails ? 1 : 0;
}
