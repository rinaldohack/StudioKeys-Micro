#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
# Layout verifier for the P8 PAGE inspector under the mixer (see check_page_inspector_layout.cpp).
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -Wall -o /tmp/check_page_inspector_layout check_page_inspector_layout.cpp
/tmp/check_page_inspector_layout
