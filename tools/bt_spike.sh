#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
#
# P6e spike: what would the JieLi BT stack cost us? (plan §4 P6e)
#
# The SDK's BT libs are LLVM bitcode (0xDEC043 objects), so the linker only reads them with JieLi's
# gold plugin; tools/bt_spike.c is the two calls that pull the whole stack in. This links the stack
# into one relocatable object (twice: alone, and with the SDK's os/event/cpu layer) and prints what
# each costs, then the undefined symbols that still need an owner.
#
#   tools/bt_spike.sh                     # AC79_SDK, JIELI_TOOLCHAIN from the environment
#   AC79_SDK=~/fw-AC79_AIoT_SDK JIELI_TOOLCHAIN=~/.jieli/toolchain tools/bt_spike.sh
#
# Not part of the build or the tests: it changes nothing under firmware/.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
SDK=${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}
TC=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}
IMAGE=${JIELI_DOCKER_IMAGE:-debian:bookworm-slim}
OUT=$SRC/build/spike
mkdir -p "$OUT"

[ -f "$SDK/cpu/wl82/liba/btstack.a" ] || { echo "bt_spike: no $SDK/cpu/wl82/liba/btstack.a (set AC79_SDK)" >&2; exit 1; }
[ -x "$TC/pi32v2/bin/clang" ] || { echo "bt_spike: no $TC/pi32v2/bin/clang (set JIELI_TOOLCHAIN)" >&2; exit 1; }

cat > "$OUT/measure.sh" <<'EOS'
set -e
L=/sdk/cpu/wl82/liba
P=/opt/jieli/pi32v2/bin
$P/clang -target pi32v2 -Os -c tools/bt_spike.c -o build/spike/bt_spike.o
blob() {   # name, libs...
    n=$1; shift
    $P/ld --plugin $P/LLVMgold.so -r -o build/spike/$n.o build/spike/bt_spike.o \
        --start-group "$@" --end-group 2>/dev/null || true
    $P/objdump -h build/spike/$n.o > build/spike/$n.hdr 2>/dev/null
    awk -v n="$n" '
    function h2d(s,  i,c,v){v=0; for(i=1;i<=length(s);i++){c=index("0123456789abcdef", substr(s,i,1)); v=v*16+(c-1)} return v}
    $1 ~ /^[0-9]+$/ && $2 ~ /^\./ && $2 !~ /rela|debug|comment|symtab|strtab|note/ {
      sz = h2d($3); tot += sz
      if ($2 ~ /bss|stack|heap/) ram += sz
      else if ($2 ~ /const|rodata|str/) con += sz
      else code += sz
    }
    END { printf "%-22s code %6.1f KB  const %6.1f KB  bss/stack %6.1f KB  total %6.1f KB\n",
                 n, code/1024, con/1024, ram/1024, tot/1024 }' build/spike/$n.hdr
    # what is still unresolved: a real (non-relocatable) link says so, -r hides it
    $P/ld --plugin $P/LLVMgold.so -o build/spike/$n.elf build/spike/bt_spike.o \
        --start-group "$@" --end-group 2> build/spike/$n.undef || true
    printf "%-22s still undefined: %d symbols\n" "$n" \
        "$(grep "undefined reference to" build/spike/$n.undef | sed "s/.*to .//; s/.$//" | sort -u | wc -l)"
}
blob bt_only   $L/btstack.a $L/btctrler.a
blob bt_with_os $L/btstack.a $L/btctrler.a $L/system.a $L/event.a $L/cpu.a $L/common_lib.a $L/fs.a $L/cfg_tool.a
echo
echo "top undefined symbols (with the SDK os layer):"
grep "undefined reference to" build/spike/bt_with_os.undef | sed 's/.*to .//; s/.$//' | sort | uniq -c | sort -rn | head -12
EOS

exec docker run --rm --platform linux/amd64 \
    -v "$SRC:/work" -v "$TC:/opt/jieli:ro" -v "$SDK:/sdk:ro" -w /work "$IMAGE" sh build/spike/measure.sh
