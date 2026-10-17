#!/bin/sh
# Layout verifier for the P7 engine desks (see check_desks_layout.cpp).
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -Wall -o /tmp/check_desks_layout check_desks_layout.cpp
/tmp/check_desks_layout
