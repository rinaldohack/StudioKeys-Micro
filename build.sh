#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
# Build StudioKEYS Micro on macOS (see BUILDING.md). Artifacts are studiokeys.*.
#   ./build.sh [--release X.Y]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
if [ -z "${AC79_SDK:-}" ]; then
    # The documented default first, then the checkouts this tree is usually cloned next to (a
    # jl-ref/ work area) so a plain ./build.sh works on a machine that keeps the SDK there.
    AC79_SDK="$HOME/fw-AC79_AIoT_SDK"
    for d in "$HOME/fw-AC79_AIoT_SDK" "$HOME/jl-ref/fw-AC79_AIoT_SDK" "$HOME/src/fw-AC79_AIoT_SDK"; do
        if [ -f "$d/cpu/wl82/tools/uboot.boot" ]; then AC79_SDK="$d"; break; fi
    done
fi
export AC79_SDK
PY="${PYTHON:-}"
if [ -z "$PY" ]; then
    # Prefer an interpreter that can run both generators: gen_aa_icons.py needs fontTools and the
    # font/keycap extractors need Pillow. Xcode's python3 often has neither, so pick the first
    # python3.X on PATH that has both instead of failing on stale generated headers.
    for p in python3 python3.14 python3.13 python3.12 python3.11 python3.10; do
        command -v "$p" >/dev/null 2>&1 || continue
        if "$p" -c 'import PIL, fontTools' 2>/dev/null; then PY="$p"; break; fi
    done
    PY="${PY:-python3}"
fi

"$PY" -c 'import PIL, fontTools' 2>/dev/null || {
    echo "build.sh: $PY needs Pillow and fontTools (pip3 install Pillow fonttools; or set PYTHON=...)"
    exit 1
}
[ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || { echo "build.sh: no toolchain in $JIELI_TOOLCHAIN (run tools/get_toolchain.sh)"; exit 1; }
[ -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no JieLi AC79 SDK in $AC79_SDK"; exit 1; }
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running"; exit 1; }
fi
exec "$PY" tools/build.py "$@"
