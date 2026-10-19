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
#include <algorithm>
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

/* a flap every few ticks keeps the bird near the gap it is about to meet: enough to see the gates
 * recycle (the test below only needs the world to keep moving, not a high score) */
static void fly(uint32_t n)
{
  while (n)
  {
    const sk_game_view_t v = view();
    if (n <= 3072u)
      break;
    if (!v.alive)
      sk_host_game_input(SK_GAME_IN_FLAP);
    else
    {
      int best = -1;
      for (int i = 0; i < 3; ++i)
        if (v.gx[i] + 34 > 51 && (best < 0 || v.gx[i] < v.gx[best]))
          best = i;
      if (best >= 0 && v.bird_y > v.gy[best])
        sk_host_game_input(SK_GAME_IN_FLAP);
      else if (best < 0 && v.bird_y > 120)
        sk_host_game_input(SK_GAME_IN_FLAP);
    }
    run(3072u);
    n -= 3072u;
  }
  run(n);
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
  /* the gates start at 240 / 336 / 432; a byte-sized view (the old one) cut the last two to 80
   * and 176, and the window drew them where the bird never met them */
  check("the view keeps the gates' x past a byte", v.gx[0] == 240 && v.gx[1] == 336 && v.gx[2] == 432,
        std::to_string((int) v.gx[1]) + "/" + std::to_string((int) v.gx[2]));

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

  /* a longer flight, flapping at the gap ahead: a gate recycles in place, so its x stays inside
   * the screen's own span. The old spawn_x grew 96 px a gate and wrapped int16 after ~9 minutes,
   * throwing a gate to a place the panel can never show. */
  {
    int32_t lo = 9999, hi = -9999;
    for (int k = 0; k < 12; ++k)
    {
      fly(48000u);
      const sk_game_view_t q = view();
      for (int i = 0; i < 3; ++i)
      {
        lo = std::min(lo, (int32_t) q.gx[i]);
        hi = std::max(hi, (int32_t) q.gx[i]);
      }
    }
    check("a long flight keeps the gates on the screen's span", lo >= -60 && hi <= 440,
          std::to_string((int) lo) + ".." + std::to_string((int) hi));
  }

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

  /* every op the window sends bumps the state: the window repaints on rev, and while the link is
   * locked the authority ships the board on it. A move that left rev alone was drawn nowhere
   * until gravity ticked — the piece looked stuck, then skipped. */
  {
    const uint32_t r0 = sk_host_game_rev();
    sk_host_game_input(SK_GAME_IN_LEFT);
    const uint32_t r1 = sk_host_game_rev();
    sk_host_game_input(SK_GAME_IN_ROT);
    const uint32_t r2 = sk_host_game_rev();
    sk_host_game_input(SK_GAME_IN_RIGHT);
    const uint32_t r3 = sk_host_game_rev();
    check("LEFT, ROTATE and RIGHT each bump the state", r1 != r0 && r2 != r1 && r3 != r2);
  }

  /* the two halves of one tetris snapshot: same id, and a half without its twin commits nothing
   * (the old id was a field bumped nowhere — always 0 — so a lone T0 committed a torn board) */
  {
    uint8_t h0[96], h1[96];
    const uint32_t n0 = sk_host_game_pack_test(h0, sizeof h0, 0);
    const uint32_t n1 = sk_host_game_pack_test(h1, sizeof h1, 1);
    const bool sameId = n0 >= 4u && n1 == n0 && h0[1] == h1[1] && h0[2] == h1[2] && h0[3] == h1[3];
    check("both halves carry one non-zero snapshot id", sameId && (h0[1] | h0[2] | h0[3]) != 0);

    const uint8_t pxPacked = view().px;
    check("T1 on its own commits nothing", sk_host_game_apply_test(h1, n1) == 0 && view().px == pxPacked);
    check("T0 on its own commits nothing", sk_host_game_apply_test(h0, n0) == 0 && view().px == pxPacked);

    /* move the authority away from the packed state, then replay the stale half: no commit */
    sk_host_game_input(SK_GAME_IN_RIGHT);
    const uint8_t pxMoved = view().px;
    check("the packed snapshot is now stale", pxMoved != pxPacked);
    check("a lone half of a stale snapshot changes nothing",
          sk_host_game_apply_test(h0, n0) == 0 && view().px == pxMoved);

    /* the pair commits it: the receiver now shows what the authority packed */
    check("T0 then T1 commits the snapshot",
          sk_host_game_apply_test(h1, n1) == 1 && view().px == pxPacked);

    /* and the same pair again is not committed a second time */
    sk_host_game_input(SK_GAME_IN_RIGHT);
    const uint8_t pxAfter = view().px;
    sk_host_game_apply_test(h0, n0);
    sk_host_game_apply_test(h1, n1);
    check("a snapshot commits once", view().px == pxAfter);
  }

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

  /* a new game must not reuse a snapshot id: the mirror drops one it has already committed, so a
   * reset rev left the first board of the next game unshown until gravity moved it */
  {
    const uint32_t rLast = sk_host_game_rev();
    sk_host_game_open(SK_GAME_TETRIS);
    check("a second game's ids follow the first game's", sk_host_game_rev() > rLast,
        std::to_string(sk_host_game_rev()) + " <= " + std::to_string(rLast));
    sk_host_game_close();
  }

  sk_host_keys_mask(1u << 4);
  const float back = run_peak(24000u);
  sk_host_keys_mask(0u);
  check("the instrument sounds again after the game", back > 0.001f);

  std::printf("\n%s\n", fails ? "GAME HOST TESTS FAILED" : "game host tests passed");
  return fails ? 1 : 0;
}
