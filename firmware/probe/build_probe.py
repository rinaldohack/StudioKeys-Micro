#!/usr/bin/env python3
"""Build the SMK-37 panel probe and splice it into the Elite v016 flash image.

Outputs, next to this tree's build/probe/:
  probe.bin             raw app, XIP at 0x02000120
  smk_probe_full.bin    on-flash image, jl-uboot-tool:  write 0 smk_probe_full.bin

The image is recovery/elite_016/restore_0_9D000.bin with app.bin replaced and
the two JLFS checksums recomputed. Bytes 0x0000..0x4000 are copied verbatim.
Nothing at or above 0x9D000 (VM, BTIF, USRTRIM, USRFLASH, USR) is in the file.
"""
import os
import platform
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SMK = HERE.parents[1]                          # felucca-smk/
ROOT = SMK.parent
OUT = SMK / "build" / "probe"
RESTORE = ROOT / "recovery" / "elite_016" / "restore_0_9D000.bin"
sys.path.insert(0, str(ROOT / "jl-misctools" / "firmware"))
from jltech.cipher import jl_sfc_cipher  # noqa: E402
from jltech.crc import jl_crc16  # noqa: E402

APP_XIP = 0x02000120
APP_OFF = 0x4120
APP_LEN = 0x97B08
APP_ENT = 0x4020
HEAD_ENT = 0x4000
HEAD_SIZE = 0x97DA7
CHIPKEY = 0x980F
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function",
          "-Ifirmware/hal"]


def toolchain():
    tc = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli" / "toolchain")).resolve()
    if not (tc / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit(f"no JieLi toolchain at {tc}")
    return tc


def run(tool, args):
    tc = toolchain()
    rel = []
    for a in args:
        p = Path(a)
        rel.append(str(p.resolve().relative_to(SMK)) if p.is_absolute() and str(p.resolve()).startswith(str(SMK)) else str(a))
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    if os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1":
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64",
               "-v", f"{SMK}:/work", "-v", f"{tc}:/opt/jieli:ro", "-w", "/work",
               "debian:bookworm-slim", f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(tc / tool), *rel]
    r = subprocess.run(cmd, cwd=SMK, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"{tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def build_app():
    OUT.mkdir(parents=True, exist_ok=True)
    run("pi32v2/bin/clang", ["-target", "pi32v2", *CFLAGS, "-c", HERE / "crt0.S", "-o", OUT / "crt0.o"])
    run("pi32v2/bin/clang", ["-target", "pi32v2", *CFLAGS, "-c", HERE / "probe.c", "-o", OUT / "probe.o"])
    run("pi32v2/bin/ld", ["-e", "_start", "-T", HERE / "app.ld", OUT / "crt0.o", OUT / "probe.o",
                          "-o", OUT / "probe.elf"])
    syms = run("common/bin/objdump", ["-t", OUT / "probe.elf"])
    (OUT / "probe.dis").write_text(run("common/bin/objdump", ["-d", OUT / "probe.elf"]))
    run("common/bin/objcopy", ["-O", "binary", "-j", ".text", OUT / "probe.elf", OUT / "text.bin"])
    run("common/bin/objcopy", ["-O", "binary", "-j", ".data", OUT / "probe.elf", OUT / "data.bin"])

    def sym(name):
        import re
        m = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        if not m:
            raise SystemExit(f"missing symbol {name}")
        return int(m.group(1), 16)

    if sym("_start") != APP_XIP:
        raise SystemExit(f"_start at {sym('_start'):#x}, want {APP_XIP:#x}")
    img = bytearray((OUT / "text.bin").read_bytes())
    if img[:4] != bytes.fromhex("04818000"):
        raise SystemExit(f"entry bytes {img[:4].hex()}, want 04818000")
    load = sym("_data_load")
    blob = (OUT / "data.bin").read_bytes() if (OUT / "data.bin").exists() else b""
    if blob:
        if load - APP_XIP < len(img):
            raise SystemExit("data overlaps text")
        img += b"\xff" * (load - APP_XIP - len(img))
        img += blob
    if len(img) > APP_LEN:
        raise SystemExit(f"probe is {len(img)} bytes, Elite app slot is {APP_LEN}")
    img += b"\xff" * (APP_LEN - len(img))
    (OUT / "probe.bin").write_bytes(img)
    print(f"probe.bin {len(img)} bytes, code before pad lives in the first {sym('_etext') - APP_XIP} bytes")
    return bytes(img)


def pack(app):
    raw = bytearray(RESTORE.read_bytes())
    if len(raw) != 0x9D000:
        raise SystemExit(f"restore image is {len(raw):#x}, want 0x9D000")
    jl_sfc_cipher(raw, 0x4000, len(raw) - 0x4000, 0x4000, CHIPKEY)
    old = jl_crc16(bytes(raw[APP_OFF:APP_OFF + APP_LEN]))
    ent = raw[APP_ENT:APP_ENT + 32]
    if jl_crc16(bytes(ent[2:])) != struct.unpack_from("<H", ent)[0] or struct.unpack_from("<H", ent, 2)[0] != old:
        raise SystemExit("restore image app.bin checksum does not match; refusing to pack")
    raw[APP_OFF:APP_OFF + APP_LEN] = app
    struct.pack_into("<H", raw, APP_ENT + 2, jl_crc16(app))
    struct.pack_into("<H", raw, APP_ENT, jl_crc16(bytes(raw[APP_ENT + 2:APP_ENT + 32])))
    head_data = bytes(raw[0x4020:0x4020 + HEAD_SIZE - 32])
    struct.pack_into("<H", raw, HEAD_ENT + 2, jl_crc16(head_data))
    struct.pack_into("<H", raw, HEAD_ENT, jl_crc16(bytes(raw[HEAD_ENT + 2:HEAD_ENT + 32])))
    # checksums must describe the plaintext we are about to encrypt
    if jl_crc16(bytes(raw[APP_OFF:APP_OFF + APP_LEN])) != struct.unpack_from("<H", raw, APP_ENT + 2)[0]:
        raise SystemExit("app checksum write failed")
    jl_sfc_cipher(raw, 0x4000, len(raw) - 0x4000, 0x4000, CHIPKEY)
    out = OUT / "smk_probe_full.bin"
    out.write_bytes(raw)
    print(f"{out} {len(raw)} bytes")
    print("flash with the Elite in UBOOT:  write 0 smk_probe_full.bin")
    print("undo:  write 0 recovery/elite_016/restore_0_9D000.bin")


if __name__ == "__main__":
    pack(build_app())
