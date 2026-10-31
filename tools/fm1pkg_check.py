#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
"""Check the flash image the SMK-37 packer produces, byte for byte, against its base image.

    tools/fm1pkg_check.py [--base IMG] APP.bin OUT.bin

The packer splices this build into the app slot of an image known to boot on the unit (fm1pkg_make's
spl_image). Everything outside that slot must come out identical, and the app_area_head table must be
left exactly as it was - not re-encoded, not re-hashed. The table's CRCs are per descriptor over that
descriptor's own 30-byte body:

    crc16(entry[2:32]) == entry[0:2]

which is precisely what the loader validates (ldr_core step 2: 32 bytes at LDR_APP_LO, decrypted with
the chip key, crc16(sec + 2, 30) against rd16(sec)). None of them hashes a payload, so a splice can
never invalidate one - but hashing a wider span does invalidate the head's, the loader then answers
-6 ("package for another chip key") and the unit drops to the mask-ROM u-boot. That is exactly the
boot regression this script exists to catch.
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fm1pkg_make as P                                    # noqa: E402

AREA_OFF = 0x4000
AREA_LEN = 0x120


def logical(img: bytes) -> bytes:
    """A .fwsc -> the flash image it carries.

    Two wrappers, both from fm1pkg_make: the identity (one marker byte after each of the first 20
    0x2F-byte blocks) and the UFW header plus its entry list (flash.bin sits at a 0x100-aligned
    offset given by its entry). A bare flash image is returned as it is."""
    if len(img) > 20 * 48:
        img = b"".join(img[i * 48:i * 48 + 47] for i in range(20)) + img[20 * 48:]
    hdr = bytearray(img[:0x40])
    P.enc(hdr, 0, 0x40)
    # the UFW header carries no magic: it is identified by its own CRC and a plausible total
    total = struct.unpack_from("<I", hdr, 4)[0]
    if struct.unpack_from("<H", hdr, 0)[0] != P.crc16(bytes(hdr[2:0x40])) or not (0x1000 < total <= len(img)):
        return img
    n = struct.unpack_from("<H", hdr, 8)[0]
    for i in range(n):
        e = bytearray(img[0x40 + i * 0x50:0x40 + (i + 1) * 0x50])
        P.enc(e, 0, 0x50)
        if struct.unpack_from("<H", e, 0)[0] == 0:                  # type 0 = flash.bin
            off = struct.unpack_from("<I", e, 8)[0]
            size = struct.unpack_from("<I", e, 12)[0]
            return img[off:off + size]
    raise SystemExit("fm1pkg_check: no flash.bin entry in the package")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("app")
    ap.add_argument("out")
    ap.add_argument("--base", default=None, help="the image that boots (default: the packer's SMK_BASE)")
    a = ap.parse_args()

    base_path = a.base or P.SMK_BASE or (Path(__file__).resolve().parent.parent.parent /
                                         "recovery" / "bootable" / "restore_0_9D000.bin")
    if not base_path:
        print("fm1pkg_check: no base image (pass --base)", file=sys.stderr)
        return 2
    base = Path(base_path).read_bytes()
    img = logical(Path(a.out).read_bytes())
    if len(base) != P.SMK_FLASH_SIZE or len(img) != P.SMK_FLASH_SIZE:
        print(f"fm1pkg_check: {len(base):#x} / {len(img):#x} B, want {P.SMK_FLASH_SIZE:#x}", file=sys.stderr)
        return 2

    bad = 0

    def check(what: str, ok: bool) -> None:
        nonlocal bad
        print(f"{what:<64} {'ok' if ok else 'FAIL'}")
        bad += 0 if ok else 1

    # the table, decrypted, is what the loader reads first
    table = bytearray(img[AREA_OFF:AREA_OFF + AREA_LEN])
    P.sfc(table, 0, len(table), 0, P.KEY)
    btable = bytearray(base[AREA_OFF:AREA_OFF + AREA_LEN])
    P.sfc(btable, 0, len(btable), 0, P.KEY)

    check("head [0, 0x4000) byte-identical to the base", img[:AREA_OFF] == base[:AREA_OFF])
    check("app_area table byte-identical to the base", img[AREA_OFF:AREA_OFF + AREA_LEN] ==
          base[AREA_OFF:AREA_OFF + AREA_LEN])

    # the loader's step-2 contract, descriptor by descriptor
    ok = struct.unpack_from("<H", table)[0] == P.crc16(bytes(table[2:32]))
    for i in range(1, 8):
        e = table[i * 32:(i + 1) * 32]
        if not any(e[16:23]):
            break
        ok &= struct.unpack_from("<H", e)[0] == P.crc16(bytes(e[2:32]))
    check("every descriptor's crc is crc16 of its own 30-byte body", ok)
    check("the table decrypts to the same bytes as the base's", table == btable)

    # the app slot: our payload, then erased flash to the end of the slot
    e = table[32:64]
    if e[16:23] != b"app.bin":
        check("the first app_area entry is app.bin", False)
        return 1
    a_off, a_size = struct.unpack_from("<II", e, 4)
    app = Path(a.app).read_bytes()
    slot = slice(AREA_OFF + a_off, AREA_OFF + a_off + a_size)
    payload = img[slot]
    check(f"app.bin slot {a_off:#x} + {a_size:#x} holds this build",
          payload[:len(app)] == app)
    check("the rest of the slot is erased (0xFF)",
          set(payload[len(app):]) <= {0xFF})
    check("nothing outside the slot differs from the base",
          all(img[i] == base[i] for i in range(len(img)) if not (AREA_OFF + a_off <= i <
                                                                 AREA_OFF + a_off + a_size)))

    print("fm1pkg_check: all checks ok" if not bad else f"fm1pkg_check: {bad} failed")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
