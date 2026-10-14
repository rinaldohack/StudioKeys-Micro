/* SPDX-License-Identifier: GPL-3.0-only
 * Phase 1: note held across octave change releases the pitch stored at note-on
 * (seq.c kb_note[] / key_off). Uses SMK key mask + OCT± panel label bits. */
#include "sk_host.h"
#include <stdio.h>
#include <string.h>

/* Panel label bits match panel.c / SkPanelControls (bit b in sk_host_buttons). */
enum { B_OCTUP = 13 };
/* seq.c kb_map: base = 53 + k → key 7 is MIDI 60 (C4) at octave 0. */
enum { KEY_C4 = 7 };

static void pump(uint32_t blocks, uint32_t frames)
{
  float L[1024], R[1024];
  uint32_t i;
  if (frames > 1024u)
    frames = 1024u;
  for (i = 0; i < blocks; i++)
    sk_host_render_f32(L, R, frames);
}

static double block_energy(uint32_t frames)
{
  float L[1024], R[1024];
  double e = 0.0;
  uint32_t i;
  if (frames > 1024u)
    frames = 1024u;
  memset(L, 0, sizeof L);
  memset(R, 0, sizeof R);
  sk_host_render_f32(L, R, frames);
  for (i = 0; i < frames; i++)
    e += (double)L[i] * (double)L[i] + (double)R[i] * (double)R[i];
  return e;
}

static double sum_energy(uint32_t blocks, uint32_t frames)
{
  double e = 0.0;
  uint32_t i;
  for (i = 0; i < blocks; i++)
    e += block_energy(frames);
  return e;
}

/* Tap OCT+ (HOME: shifts song.octave; edge drained on render). */
static void tap_oct(uint32_t bit)
{
  sk_host_buttons(1u << bit);
  pump(2u, 128u);
  sk_host_buttons(0);
  pump(2u, 128u);
}

int main(void)
{
  double e_hold, e_after_oct, e_off;

  sk_host_init();
  sk_host_boot();
  /* Silence boot splash (~1 s at 44.1 kHz). HOME is set at boot so OCT± shift octave. */
  pump(44100u / 128u, 128u);

  /* Hold physical C4 via SMK key mask (clearer OCT path than DAW MIDI). */
  sk_host_keys_mask(1u << KEY_C4);
  pump(8u, 128u);
  e_hold = sum_energy(24u, 128u);
  if (e_hold < 1e-4) {
    fprintf(stderr, "oct noteoff: no energy while key held (%g)\n", e_hold);
    return 1;
  }

  /* Octave up while the key stays down — kb_note[k] keeps the note-on pitch. */
  tap_oct(B_OCTUP);
  e_after_oct = sum_energy(24u, 128u);
  if (e_after_oct < 1e-4) {
    fprintf(stderr, "oct noteoff: note died on OCT+ (%g → %g)\n", e_hold, e_after_oct);
    return 1;
  }

  /* Same physical key up must release the stored pitch (not a remapped one). */
  sk_host_keys_mask(0);
  pump(100u, 128u); /* envelope release */
  e_off = sum_energy(40u, 128u);
  if (e_off >= e_hold * 0.05) {
    fprintf(stderr, "oct noteoff: hang after key-up hold=%g after_oct=%g off=%g\n", e_hold,
            e_after_oct, e_off);
    return 1;
  }

  /* Optional mirror: MIDI note-off of the original C4 pitch must stay silent (no re-trigger). */
  sk_host_midi(0x80, 60, 0);
  pump(4u, 128u);
  if (sum_energy(8u, 128u) >= e_hold * 0.05) {
    fprintf(stderr, "oct noteoff: energy returned after MIDI off of original pitch\n");
    return 1;
  }

  puts("oct noteoff ok");
  return 0;
}
