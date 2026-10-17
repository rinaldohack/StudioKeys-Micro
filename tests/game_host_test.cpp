/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* P19 / P20: the host side of the easter eggs (sk_host_game_*), the surface the companion's
 * GameWindow drives:
 *   - open / close: the kind, the mute gate, the snapshot the window draws
 *   - FLAPPY: the bird falls with time, a flap lifts it, a dead bird restarts on the next flap
 *   - TETRIS: a fresh board, the four inputs (left / rotate / right / drop), gravity from the clock
 *   - the mute: the same key that sounds before the game is silent while it is open
 * The rendering of the panel / window and the link packets are the firmware's own tests
 * (tests/ui_test.c, game.c) and the plug-in's; this only checks the API the VST sees.
 * Built by tests/run_tests.sh. */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "sk_host.h"

/* audio.c's hook (firmware/src/game.c); the host build includes game.c into sk_host.c */
extern "C" uint8_t sk_game_mute(void);

static int fails;
static float L[512], R[512];

static void check(const char* what, int ok, const std::string& note = {})
{
  std::printf("game host: %-62s %s", what, ok ? "ok" : "FAIL");
  if (!note.empty())
    std::printf("  (%s)", note.c_str());
  std::printf("\n");
  fails += !ok;
}

/* run the device for n samples: the audio ISR advances fm1_ms, the frame loop ticks the game */
static void run(uint32_t n)
{
  while (n)
  {
    const uint32_t k = n < 512u ? n : 512u;
    std::memset(L, 0, sizeof L);
    std::memset(R, 0, sizeof R);
    sk_host_render_f32(L, R, k);
    n -= k;
  }
}

static float peak(const float* b, uint32_t n)
{
  float p = 0.f;
  for (uint32_t i = 0; i < n; ++i)
    p = b[i] < 0.f ? (p > -b[i] ? p : -b[i]) : (p > b[i] ? p : b[i]);
  return p;
}

/* like run(), but reports the loudest sample over the whole stretch */
static float run_peak(uint32_t n)
{
  float p = 0.f;
  while (n)
  {
    const uint32_t k = n < 512u ? n : 512u;
    std::memset(L, 0, sizeof L);
    std::memset(R, 0, sizeof R);
    sk_host_render_f32(L, R, k);
    const float c = peak(L, k) > peak(R, k) ? peak(L, k) : peak(R, k);
    if (c > p)
      p = c;
    n -= k;
  }
  return p;
}

static sk_game_view_t view()
{
  sk_game_view_t v {};
  sk_host_game_view(&v);
  return v;
}

int main()
{
  sk_host_init();
  sk_host_boot();

  check("nothing is open at boot", sk_host_game_kind() == 0 && !sk_game_mute());

  /* a note sounds, so the mute check below means something */
  sk_host_keys_mask(1u << 4);
  const float loud = run_peak(48000u);
  sk_host_keys_mask(0u);
  run(24000u);
  check("the instrument sounds with no game", loud > 0.001f);

  /* ---- FLAPPY ------------------------------------------------------------------------- */
  sk_host_game_open(SK_GAME_FLAPPY);
  check("FLAPPY opens", sk_host_game_kind() == SK_GAME_FLAPPY);
  check("the mute gate is on", sk_game_mute() != 0);

  const uint32_t rev0 = sk_host_game_rev();
  sk_game_view_t v = view();
  check("the snapshot names the game", v.kind == SK_GAME_FLAPPY && v.alive && !v.started);
  check("the bird starts mid screen", v.bird_y == 120);
  const int16_t y0 = v.bird_y;
  bool gatesOk = true;
  for (int i = 0; i < 3; ++i)
    gatesOk = gatesOk && v.gh[i] >= 54 && v.gx[i] >= 0;
  check("three gates with a legal gap", gatesOk);

  /* 120 ms: about seven 16 ms ticks, gravity pulls the bird down */
  run(5760u);
  v = view();
  check("time advances the state", sk_host_game_rev() != rev0);
  check("the bird falls", v.bird_y > y0);

  sk_host_game_input(SK_GAME_IN_FLAP);
  v = view();
  check("a flap lifts the bird", v.bird_vy < 0 && v.started);

  /* the same key is silent now: every output is muted while a game is open */
  sk_host_keys_mask(1u << 4);
  const float quiet = run_peak(24000u);
  sk_host_keys_mask(0u);
  run(48000u);
  check("all outputs are muted while FLAPPY runs", quiet == 0.f);

  /* the bird eventually dies (no more flaps), and a flap restarts the round */
  run(4u * 48000u);
  v = view();
  check("a bird that never flaps dies", !v.alive);
  sk_host_game_input(SK_GAME_IN_FLAP);
  v = view();
  check("a flap after death restarts", v.alive && v.bird_y == 120 && v.score == 0);

  sk_host_game_close();
  check("FLAPPY closes", sk_host_game_kind() == 0 && !sk_game_mute());

  /* ---- TETRIS ------------------------------------------------------------------------- */
  sk_host_game_open(SK_GAME_TETRIS);
  v = view();
  check("TETRIS opens", sk_host_game_kind() == SK_GAME_TETRIS && v.kind == SK_GAME_TETRIS);
  check("a fresh board is empty", !v.over && v.score == 0 && v.lines == 0);
  bool empty = true;
  for (int i = 0; i < SK_GAME_TET_COLS * SK_GAME_TET_ROWS; ++i)
    empty = empty && v.board[i] == 0;
  check("no settled cells yet", empty);
  /* y = 0 is where the four bits of the packed shape can start; the piece rides above it */
  sk_game_view_t t0 = view();
  check("an active piece with a queue", t0.piece < 7 && t0.next0 < 7 && t0.next1 < 7);

  const uint8_t px0 = t0.px, rot0 = t0.rot, py0 = t0.py;
  sk_host_game_input(SK_GAME_IN_LEFT);
  v = view();
  check("LEFT moves the piece left", px0 > 0 && v.px + 1u == px0);
  sk_host_game_input(SK_GAME_IN_ROT);
  v = view();
  check("ROTATE turns it", v.rot != rot0 || v.px != px0);
  sk_host_game_input(SK_GAME_IN_RIGHT);
  sk_host_game_input(SK_GAME_IN_DROP);
  v = view();
  check("DROP sends it down", v.py > py0 || v.rev != t0.rev);

  /* gravity: the piece must not hang in the air when the clock moves */
  const uint32_t py1 = view().py;
  run(2u * 48000u);
  check("gravity follows the clock", view().py > py1 || view().rev != t0.rev);

  /* and the mute covers TETRIS too */
  sk_host_keys_mask(1u << 4);
  const float quietTet = run_peak(24000u);
  sk_host_keys_mask(0u);
  run(24000u);
  check("all outputs are muted while TETRIS runs", quietTet == 0.f);

  sk_host_game_close();
  check("TETRIS closes", sk_host_game_kind() == 0 && !sk_game_mute());
  sk_host_game_open(99);
  check("an unknown kind is refused", sk_host_game_kind() == 0 && !sk_game_mute());

  sk_host_keys_mask(1u << 4);
  const float back = run_peak(24000u);
  sk_host_keys_mask(0u);
  check("the instrument sounds again after the game", back > 0.001f);

  std::printf("\n%s\n", fails ? "GAME HOST TESTS FAILED" : "game host tests passed");
  return fails ? 1 : 0;
}
