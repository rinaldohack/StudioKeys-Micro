#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
# Install StudioKEYS Ableton Live remote script into the user library.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/plugin/ableton/StudioKEYS"
DEST="${HOME}/Music/Ableton/User Library/Remote Scripts/StudioKEYS"
if [[ ! -d "$SRC" ]]; then
  echo "missing $SRC" >&2
  exit 1
fi
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
cp -R "$SRC" "$DEST"
echo "Installed Ableton remote script → $DEST"
echo "Restart Live, then: Control Surface = StudioKEYS, I/O = StudioKEYS Live"
