#!/usr/bin/env bash
# Phase-1 exit checks for the native host (VST_IMPLEMENTATION_PLAN.md §1).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build/host
GEN=(-Ibuild/gen -Ifirmware/src -Ihost)
CCFLAGS=(-O2 -ffp-contract=off -w -DNVOICE=128)

echo "== desk labels vs firmware =="
python3 tools/gen_desk_params.py --check

echo "== page inspector regenerate + count =="
python3 tools/gen_page_inspector.py
python3 - <<'PY'
from pathlib import Path
t = Path("plugin/StudioKEYS/PageInspector.h").read_text()
assert "kNumPages = 37" in t, "expected 37 firmware pages"
print("PageInspector.h ok (37 pages)")
PY

echo "== 44 px hit-target probe =="
python3 tools/check_hit_targets.py

echo "== import bay Bulk32 (DX7 structural) =="
python3 tools/check_bay_bulk.py

echo "== import bay Virus TI (ANALOG structural) =="
python3 tools/check_virus_bay.py

echo "== librarian SeedDemo glide (phase 7) =="
python3 tools/check_librarian_glide.py

echo "== MIDI cable map =="
python3 tools/check_midi_cables.py

echo "== surface map (Ableton vs SurfaceCard) =="
python3 tools/check_surface_map.py

echo "== product rename hard-gate =="
bash tools/rename_product_scan.sh

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

echo "== polyphony under dense chord =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_poly_smoke.c host/sk_host.c -o build/host/sk_poly_smoke -lm
./build/host/sk_poly_smoke

echo "== native_check (heavy song → .f32) =="
if [ -f web/emu/native_check.c ]; then
  if [ ! -x build/emu/native_check ] && [ ! -x build/host/native_check ]; then
    echo "(building native_check)"
    mkdir -p build/emu
    cc "${CCFLAGS[@]}" -Ibuild/gen -Ifirmware/src -o build/emu/native_check web/emu/native_check.c -lm \
      || echo "(native_check build failed — continue)"
  fi
  if [ -x build/host/native_check ]; then
    ./build/host/native_check build/host/native_check.f32 || true
  elif [ -x build/emu/native_check ]; then
    ./build/emu/native_check build/emu/native_check.f32 || true
  else
    echo "(native_check binary missing)"
  fi
fi

echo "== lock snapshot smoke =="
cat > build/host/lock_smoke.c <<'EOF'
#include "sk_host.h"
#include <stdio.h>
int main(void) {
  sk_host_init(); sk_host_boot();
  if (!sk_host_lock_claim_instance()) return 1;
  sk_host_lock_ask();
  sk_host_lock_choose(1);
  if (sk_host_lock_get() != SK_LOCK_ON) {
    fprintf(stderr, "lock want ON got %d (%s)\n", sk_host_lock_get(), sk_host_lock_reject_reason());
    return 1;
  }
  if (!sk_host_lock_plugin_clock()) { fprintf(stderr, "plugin clock off while locked\n"); return 1; }
  sk_host_lock_autosave();
  sk_host_ui_t ui; sk_host_ui_get(&ui);
  printf("lock ok desk=%u eng=%s page=%s\n", ui.desk, ui.engine_name, ui.page_title);
  sk_host_lock_drop();
  return 0;
}
EOF
cc "${CCFLAGS[@]}" "${GEN[@]}" build/host/lock_smoke.c host/sk_host.c -o build/host/lock_smoke -lm
./build/host/lock_smoke

echo "== preset float hash (Mac host; DAW OS hashes still open) =="
bash host/check_preset_hash.sh

echo "== SKL/SKS link loopback =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_link_loopback.c host/sk_host.c -o build/host/sk_link_loopback -lm
./build/host/sk_link_loopback

echo "== page param labels =="
cat > build/host/page_label_smoke.c <<'EOF'
#include "sk_host.h"
#include <stdio.h>
#include <string.h>
int main(void) {
  sk_host_init(); sk_host_boot();
  if (sk_host_goto_page(0) != 0) return 1;
  const char *l0 = sk_host_page_param_label(0, 0);
  if (!l0 || !l0[0]) { fprintf(stderr, "ENV slot0 empty\n"); return 1; }
  uint32_t ids[4]; uint32_t n = sk_host_page_params(0, ids);
  if (n < 4) { fprintf(stderr, "ENV want 4 params got %u\n", n); return 1; }
  printf("page labels ok %s/%s/%s/%s\n",
    sk_host_page_param_label(0, 0), sk_host_page_param_label(0, 1),
    sk_host_page_param_label(0, 2), sk_host_page_param_label(0, 3));
  return 0;
}
EOF
cc "${CCFLAGS[@]}" "${GEN[@]}" build/host/page_label_smoke.c host/sk_host.c -o build/host/page_label_smoke -lm
./build/host/page_label_smoke

echo "== headless CLI (APP substitute) =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_headless_cli.c host/sk_host.c -o build/host/sk_headless_cli -lm
./build/host/sk_headless_cli build/host/sk_headless.wav
test -s build/host/sk_headless.wav

echo "== palette smoke =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_palette_smoke.c host/sk_host.c -o build/host/sk_palette_smoke -lm
./build/host/sk_palette_smoke

echo "== NOR roundtrip smoke =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_nor_roundtrip.c host/sk_host.c -o build/host/sk_nor_roundtrip -lm
./build/host/sk_nor_roundtrip

echo "== desk sync smoke =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_desk_sync_smoke.c host/sk_host.c -o build/host/sk_desk_sync_smoke -lm
./build/host/sk_desk_sync_smoke

echo "== lock refuse smoke =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_lock_refuse_smoke.c host/sk_host.c -o build/host/sk_lock_refuse_smoke -lm
./build/host/sk_lock_refuse_smoke

echo "== color smoke =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_color_smoke.c host/sk_host.c -o build/host/sk_color_smoke -lm
./build/host/sk_color_smoke

echo "== OCT± note-off (stored pitch) =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_oct_noteoff_smoke.c host/sk_host.c -o build/host/sk_oct_noteoff_smoke -lm
./build/host/sk_oct_noteoff_smoke

echo "== LANG EN/ID smoke =="
cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_lang_smoke.c host/sk_host.c -o build/host/sk_lang_smoke -lm
./build/host/sk_lang_smoke

echo "== wasm bit-match (optional) =="
if [ -f build/emu/felucca.wasm ] && [ -f web/emu/emu_test.mjs ]; then
  node web/emu/emu_test.mjs
else
  echo "(skip: build/emu/felucca.wasm not present — run web/emu/build.sh)"
fi

echo "phase1 host checks done"
