#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
# Local half of the hardware test card (FIELD_TEST.md): what can be checked with the
# keyboard plugged in but before playing a note. Read-only: no flash write, no lock.
#
#   bash tools/field_check.sh [RELEASE_DIR]
#
# Needs CoreMIDI access, so run it from Terminal.app, not from a sandboxed agent shell.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
REL="${1:-$ROOT/../flash-2026-10-18}"
PY="$HOME/.venvs/sk-ota/bin/python"
[ -x "$PY" ] || PY="$(command -v python3)"
fails=0

say()  { printf '%s\n' "$*"; }
ok()   { printf '  ok    %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; fails=$((fails + 1)); }

say "== release image =="
if [ -f "$REL/SHA256SUMS" ]; then
  if (cd "$REL" && shasum -a 256 -c SHA256SUMS >/dev/null 2>&1); then
    ok "checksums match ($REL)"
  else
    bad "checksums do NOT match in $REL — do not flash it"
  fi
  sz="$(wc -c <"$REL/studiokeys-flash.bin" 2>/dev/null | tr -d ' ')"
  if [ "$sz" = "602112" ]; then ok "flash part 0x93000 B (ends before BTIF / USR)"
  else bad "flash part is ${sz:-missing} B, expected 602112"; fi
else
  say "  (no SHA256SUMS in $REL — pass the release directory as \$1)"
fi

say "== companion VST3 =="
VST3="$HOME/Library/Audio/Plug-Ins/VST3/StudioKEYS.vst3"
if [ -d "$VST3" ]; then
  bid="$(plutil -extract CFBundleIdentifier raw "$VST3/Contents/Info.plist" 2>/dev/null)"
  if [ "$bid" = "com.LangitCintaStageLabs.vst3.StudioKEYS" ]; then
    ok "installed: $bid"
  else
    bad "unexpected bundle id '${bid:-none}'"
  fi
  if codesign --verify --deep --strict "$VST3" 2>/dev/null; then ok "code signature valid"
  else bad "code signature invalid (rebuild / re-copy the bundle)"; fi
else
  bad "not installed at $VST3"
fi

say "== USB-MIDI ports (the keyboard's four cables) =="
plugged=0
if ! "$PY" -c 'import mido, rtmidi' 2>/dev/null; then
  say "  (skip: no mido/python-rtmidi for $PY — see FIELD_TEST.md)"
else
  ports="$("$PY" - <<'PY' 2>/dev/null
import mido
for n in mido.get_input_names():
    print(n)
PY
)"
  if [ -n "$ports" ]; then
    plugged=1
    printf '%s\n' "$ports" | sed 's/^/    /'
  fi
  if [ -z "$ports" ]; then
    say "  (nothing enumerated: cable in the data port, keyboard powered, no other app holding it)"
  else
    for want in "StudioKEYS" "StudioKEYS Live" "StudioKEYS HUI" "StudioKEYS Link"; do
      if printf '%s\n' "$ports" | grep -qx "$want"; then ok "port '$want'"
      else bad "port '$want' missing"; fi
    done
    # The link only needs the Link endpoint; the other three are the DAW / Live / HUI cables.
    if ! printf '%s\n' "$ports" | grep -q "StudioKEYS Link"; then
      say "    (macOS may show the four embedded cables under different names — note what it prints)"
    fi
  fi
fi

say "== device identity (read-only) =="
if "$PY" tools/fm1_install.py --info 2>&1 | sed 's/^/  /'; then
  ok "installer reached the keyboard"
  plugged=1
else
  say "  (device not found or not answering — expected when unplugged)"
fi

say
if [ "$plugged" -eq 0 ]; then
  say "field check: local side OK; keyboard not connected, so the port / identity checks were skipped"
  exit 0
fi
if [ "$fails" -eq 0 ]; then
  say "field check: local side OK — start the card at step B1"
  exit 0
fi
say "field check: $fails problem(s) above — fix these before flashing"
exit 1
