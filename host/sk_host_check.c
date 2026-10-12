/* SPDX-License-Identifier: GPL-3.0-only
 * Phase 1 exit: boot host, render 1 s, write float32 L/R interleaved. */
#include "sk_host.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    float L[128], R[128];
    FILE *f;
    uint32_t i, frames = 44100u;
    if (argc < 2)
        return 2;
    f = fopen(argv[1], "wb");
    if (!f)
        return 2;
    sk_host_init();
    sk_host_boot();
    /* silence boot splash, then a middle-C note via MIDI */
    for (i = 0; i < 44100u / 128u; i++)
        sk_host_render_f32(L, R, 128);
    sk_host_midi(0x90, 60, 100);
    while (frames) {
        uint32_t n = frames > 128u ? 128u : frames;
        uint32_t k;
        sk_host_render_f32(L, R, n);
        for (k = 0; k < n; k++) {
            float s[2] = {L[k], R[k]};
            fwrite(s, sizeof s, 1, f);
        }
        frames -= n;
    }
    sk_host_midi(0x80, 60, 0);
    fclose(f);
    printf("ok version=%s budget=%u crc=%08x\n", sk_host_version(),
           sk_host_voice_budget(), sk_host_state_crc());
    return 0;
}
