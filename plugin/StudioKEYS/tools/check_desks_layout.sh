#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
# Layout verifier for the P7 engine desks (see check_desks_layout.cpp).
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -Wall -o /tmp/check_desks_layout check_desks_layout.cpp
/tmp/check_desks_layout
