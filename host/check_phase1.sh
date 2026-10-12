#!/usr/bin/env bash
# Phase-1 exit checks for the native host (VST_IMPLEMENTATION_PLAN.md §1).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build/host
GEN=(-Ibuild/gen -Ifirmware/src -Ihost)
CCFLAGS=(-O2 -ffp-contract=off -w -DNVOICE=128)

echo "== sk_host smoke (middle-C) =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_host.c host/sk_host_check.c -o build/host/sk_host_check -lm
./build/host/sk_host_check build/host/sk_host_check.f32

echo "== Unleashed opens to ceiling (not 32) =="
cat > build/host/budget_smoke.c <<'EOF'
#include "sk_host.h"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  sk_host_init(); sk_host_boot();
  if (sk_host_voice_budget() != 8) { fprintf(stderr, "Limited want 8 got %u\n", sk_host_voice_budget()); return 1; }
  sk_host_set_unleashed(1);
  if (sk_host_voice_budget() != sk_host_voice_ceiling()) {
    fprintf(stderr, "Unleashed want ceiling %u got %u\n", sk_host_voice_ceiling(), sk_host_voice_budget());
    return 1;
  }
  if (sk_host_voice_ceiling() < 64) {
    fprintf(stderr, "ceiling too small: %u\n", sk_host_voice_ceiling());
    return 1;
  }
  puts("budget ok");
  return 0;
}
EOF
cc "${CCFLAGS[@]}" "${GEN[@]}" build/host/budget_smoke.c host/sk_host.c -o build/host/budget_smoke -lm
./build/host/budget_smoke

echo "== native_check (heavy song → .f32) =="
if [ -f web/emu/native_check.c ]; then
  # native_check is built like the web host (its own main); reuse emu build if present
  if [ -x build/host/native_check ]; then
    ./build/host/native_check
  else
    echo "(build native_check via web/emu/build.sh or tests/run_tests.sh — binary missing)"
  fi
fi

echo "== wasm bit-match (optional) =="
if [ -f build/emu/felucca.wasm ] && [ -f web/emu/emu_test.mjs ]; then
  node web/emu/emu_test.mjs
else
  echo "(skip: build/emu/felucca.wasm not present — run web/emu/build.sh)"
fi

echo "phase1 host checks done"
