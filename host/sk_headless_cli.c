/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * Headless standalone substitute when iPlug APP cannot build (no Xcode ibtool).
 * Renders a short middle-C burst to a WAV — proves host audio without a GUI. */
#include "sk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_wav(const char *path, const float *L, const float *R, uint32_t n, uint32_t sr)
{
  FILE *f = fopen(path, "wb");
  if (!f)
    return;
  const uint32_t data_bytes = n * 2u * 2u;
  const uint32_t riff = 36u + data_bytes;
  unsigned char hdr[44] = {
    'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
    16,0,0,0, 1,0, 2,0, 0,0,0,0, 0,0,0,0, 4,0, 16,0,
    'd','a','t','a',0,0,0,0
  };
  hdr[4] = (unsigned char)(riff); hdr[5] = (unsigned char)(riff >> 8);
  hdr[6] = (unsigned char)(riff >> 16); hdr[7] = (unsigned char)(riff >> 24);
  hdr[24] = (unsigned char)(sr); hdr[25] = (unsigned char)(sr >> 8);
  hdr[26] = (unsigned char)(sr >> 16); hdr[27] = (unsigned char)(sr >> 24);
  const uint32_t br = sr * 4u;
  hdr[28] = (unsigned char)(br); hdr[29] = (unsigned char)(br >> 8);
  hdr[30] = (unsigned char)(br >> 16); hdr[31] = (unsigned char)(br >> 24);
  hdr[40] = (unsigned char)(data_bytes); hdr[41] = (unsigned char)(data_bytes >> 8);
  hdr[42] = (unsigned char)(data_bytes >> 16); hdr[43] = (unsigned char)(data_bytes >> 24);
  fwrite(hdr, 1, 44, f);
  for (uint32_t i = 0; i < n; i++) {
    float a = L[i], b = R[i];
    if (a > 1.f) a = 1.f; if (a < -1.f) a = -1.f;
    if (b > 1.f) b = 1.f; if (b < -1.f) b = -1.f;
    int16_t sl = (int16_t)(a * 32767.f), sr16 = (int16_t)(b * 32767.f);
    fwrite(&sl, 2, 1, f);
    fwrite(&sr16, 2, 1, f);
  }
  fclose(f);
}

int main(int argc, char **argv)
{
  const char *out = argc > 1 ? argv[1] : "build/host/sk_headless.wav";
  const uint32_t sr = SK_HOST_FS;
  const uint32_t n = sr; /* 1 second */
  float *L = (float *)calloc(n, sizeof(float));
  float *R = (float *)calloc(n, sizeof(float));
  if (!L || !R) {
    free(L); free(R);
    return 1;
  }
  sk_host_init();
  sk_host_boot();
  sk_host_midi(0x90, 60, 100);
  for (uint32_t off = 0; off < n; ) {
    uint32_t chunk = 128;
    if (off + chunk > n)
      chunk = n - off;
    if (off == sr / 2)
      sk_host_midi(0x80, 60, 0);
    sk_host_render_f32(L + off, R + off, chunk);
    off += chunk;
  }
  write_wav(out, L, R, n, sr);
  printf("headless ok version=%s wrote %s (%u frames)\n", sk_host_version(), out, n);
  free(L);
  free(R);
  return 0;
}
