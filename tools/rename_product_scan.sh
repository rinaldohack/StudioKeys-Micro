#!/usr/bin/env bash
# Product-surface Felucca / FM-1 rename gate (HAL symbols + lineage docs ignored).
# Exit 1 if any hits remain after filters; 0 if clean. Counts every hit (no head truncate).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "== product-surface rename scan (UI / plugin / screen copy) =="
# Intentional noise filtered below: FELUCCA_* build macros, felucca.wasm/bin artifacts.
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
set +e
rg -n -i 'felucca|fm-1|fm1 ' \
  --glob '!**/third_party/**' \
  --glob '!**/build/**' \
  --glob '!VST_IMPLEMENTATION_PLAN.md' \
  --glob '!CHANGELOG.md' \
  --glob '!LICENSE*' \
  --glob '!LICENSING.md' \
  --glob '!README.md' \
  --glob '!BUILDING.md' \
  --glob '!host/preset_hash*' \
  --glob '!host/sk_host.c' \
  --glob '!host/README.md' \
  --glob '!firmware/hal/**' \
  --glob '!firmware/src/felucca.c' \
  --glob '!firmware/src/audio.c' \
  --glob '!firmware/src/usb.c' \
  --glob '!firmware/src/main.c' \
  --glob '!firmware/src/console.c' \
  --glob '!firmware/src/core.h' \
  --glob '!web/emu/**' \
  --glob '!tests/**' \
  --glob '!tools/gen_panel_diagram.py' \
  --glob '!tools/fm1_*.py' \
  plugin host/sk_*.h host/check_*.sh firmware/src/ui_menu.c firmware/src/menu_items.c \
  2>/dev/null \
  | rg -v -e 'FELUCCA_(CDC|CDC_DEFAULT|VERSION|ON_DEVICE|UAC|OTA|FLASH|USB_LAYOUT|ICONS|FM4|SLICE|SMK_PANEL)\b' \
         -e 'felucca\.(wasm|bin)\b' \
         -e 'build/emu/felucca' \
  >"$tmp"
set -e

n=0
if [ -s "$tmp" ]; then
  n="$(wc -l <"$tmp" | tr -d ' ')"
fi

if [ "$n" -gt 0 ]; then
  cat "$tmp"
  echo "rename scan FAIL: $n product-surface hit(s) remain (lineage docs / HAL / FELUCCA_* macros / binary artifacts excluded)"
  exit 1
fi
echo "rename scan ok (0 product-surface hits)"
exit 0
