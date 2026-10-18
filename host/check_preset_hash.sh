#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
# Phase-2 helper: render a fixed host note and hash the float dump (Mac today).
# Win/Linux DAW hash remains open until those VST3 builds exist.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build/host
GEN=(-Ibuild/gen -Ifirmware/src -Ihost)
CCFLAGS=(-O2 -ffp-contract=off -w -DNVOICE=128)

cc "${CCFLAGS[@]}" "${GEN[@]}" host/sk_host.c host/sk_host_check.c -o build/host/sk_host_check -lm
# Hermetic: sk_host overlays the NOR it saved last time ($HOME/Library/Application Support/
# StudioKEYS/nor.bin) so a DAW session or a pluginval run could otherwise change what this
# renders. An empty HOME makes the render depend on the tree only.
STATE_HOME="$(mktemp -d)"
trap 'rm -rf "$STATE_HOME"' EXIT
HOME="$STATE_HOME" ./build/host/sk_host_check build/host/preset_hash.f32

if command -v shasum >/dev/null; then
  HASH=$(shasum -a 256 build/host/preset_hash.f32 | awk '{print $1}')
elif command -v sha256sum >/dev/null; then
  HASH=$(sha256sum build/host/preset_hash.f32 | awk '{print $1}')
else
  echo "no sha256 tool"; exit 1
fi

GOLD=host/preset_hash.sha256
if [ ! -f "$GOLD" ]; then
  echo "$HASH" > "$GOLD"
  echo "wrote gold $GOLD = $HASH"
else
  WANT=$(tr -d '[:space:]' < "$GOLD")
  if [ "$HASH" != "$WANT" ]; then
    echo "preset hash mismatch want=$WANT got=$HASH" >&2
    exit 1
  fi
  echo "preset hash ok $HASH"
fi
