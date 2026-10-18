#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
# Product-surface rename gate: no "felucca" returns to the tree post-rename.
# Upstream lineage / attribution lines (hugelton/Felucca URLs, "a Felucca fork",
# the on-disk "[FELUCCA]" package marker) are intentional and filtered out.
# Exit 1 if any hits remain after filters; 0 if clean. Counts every hit (no head truncate).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "== product-surface rename scan (no felucca outside attribution) =="
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
set +e
# Any case-insensitive "felucca" hit anywhere in the tree, minus build output, git, licenses
# and the known intentional attribution / on-disk-marker lines.
# The provenance documents (README.md, LICENSING.md, LICENSE) name the upstream lineage on
# purpose: the plan keeps attribution ("copyright lines are restated, not dropped"), so a hit
# there is not a product-surface leak. They are required to carry the notice instead - see the
# positive check below.
rg -n -i 'felucca' \
  --glob '!tools/rename_product_scan.sh' \
  --glob '!**/third_party/**' \
  --glob '!**/build/**' \
  --glob '!LICENSES/**' \
  --glob '!README.md' --glob '!LICENSING.md' --glob '!LICENSE' \
  --glob '!**/*.png' --glob '!**/*.jpg' --glob '!**/*.ttf' --glob '!**/*.woff' \
  2>/dev/null \
  | rg -v -e 'hugelton/Felucca' \
         -e 'Felucca-WebApp' \
         -e 'a Felucca fork' \
         -e 'charlesvestal' \
         -e '\[FELUCCA\]' \
  >"$tmp"
set -e

n=0
if [ -s "$tmp" ]; then
  n="$(wc -l <"$tmp" | tr -d ' ')"
fi

if [ "$n" -gt 0 ]; then
  cat "$tmp"
  echo "rename scan FAIL: $n product-surface hit(s) remain (attribution / on-disk marker excluded)"
  exit 1
fi

# The other half of the gate: the lineage notice must still be there (a rename pass that deletes
# attribution would otherwise pass silently).
for doc in README.md LICENSING.md; do
  if ! grep -qiE 'H(ü|u)gelton Instruments' "$doc"; then
    echo "rename scan FAIL: $doc lost the Hügelton Instruments lineage notice"
    exit 1
  fi
done
echo "rename scan ok (0 product-surface hits)"
exit 0
