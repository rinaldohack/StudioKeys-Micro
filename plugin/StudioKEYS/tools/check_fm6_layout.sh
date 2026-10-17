#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
# Build and run the FM6 desk layout verifier (see check_fm6_layout.cpp).
# Exits non-zero if any control overlaps, escapes the desk body, or is too small.
set -euo pipefail
cd "$(dirname "$0")"
out="$(mktemp -t check_fm6_layout)"
trap 'rm -f "$out"' EXIT
c++ -std=c++17 -O0 -o "$out" check_fm6_layout.cpp
"$out"
