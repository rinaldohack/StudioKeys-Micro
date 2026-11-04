#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion
"""Build a complete FM-1 update package (.fwsc) from scratch, with no vendor
material in it:

  flash.bin  head [0, 0x4000): flash header (JieLi SDK default values), JLFS
             entries, the SDK SPL (uboot.boot, Apache-2.0), an isd_config blob
             of our own that decodes to the chip key; the head is never written
             by StudioKEYS's loader, it only has to look like a package head
             app area 0x4000..: app_area_head, StudioKEYS app.bin, the SDK
             cfg_tool.bin and eq_cfg_hw.bin, the region descriptors, SFC-encrypted
             with the chip key
  ota.bin    StudioKEYS's own update loader (firmware/loader)

The layout values (offsets, flags, descriptor addresses, header fields) are
facts of the FM-1 flash layout and the SDK file formats.

The three SDK files (uboot.boot, cfg_tool.bin, eq_cfg_hw.bin) are Apache-2.0, not GPL:
ship LICENSES/Apache-2.0.txt with every package (LICENSING.md; make_site.py and
build.py --release do).

  fm1pkg_make.py APP.bin OTA.bin OUT.fwsc [--product FM-1_9XY] [--key 0x980F]
"""
import argparse
import os
import struct
from pathlib import Path

# The FM-1's app slot. Kept only for the synthesised head below (flash_image's SDK path, unbased
# builds); the SMK-37 image reads its slot out of its base image's own region table (base_slot) and
# that is what app.ld, build.py and size_report.py all gate on. It is the ONLY place the number is
# allowed to live: 0x8DFBC is 39,756 B short of the SMK's, and the mismatch used to read as the
# firmware being 532 B over budget.
APP_SLOT = 0x8DFBC
FLASH_SIZE = 0x93000
# The SMK-37's own geometry (stock v016, read off a unit and confirmed against the running firmware):
# the app area is larger than the FM-1's - 0x4000..0x9D000 - and the VM starts at 0x9D000. An image
# built for the FM-1's 0x93000 boundary drops the SPL into u-boot instead of the app, which is why a
# package that looked fine never booted.
SMK_FLASH_SIZE = 0x9D000
SMK_BASE = None          # the proven base image (recovery/*/restore_0_9D000.bin): its head is spliced
KEY = 0x980F
# flash header: JieLi SDK defaults (burner_size 544, VID "0.01", flash_size 0xFF000,
# fs_ver 0x10, PID "AC791N_STORY"), plain, without its CRC
FLASH_HDR = bytes.fromhex("20022f104cc900f00f001010a4ff4e5d0b41e0cd781dc856417fb367cf9f")
UFW_CHIP = b"AC791N"
SDK = None


def _crc_table():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else (c << 1)
        t.append(c & 0xFFFF)
    return t


_T = _crc_table()


def crc16(data, crc=0):
    for b in data:
        crc = ((crc << 8) & 0xFFFF) ^ _T[((crc >> 8) ^ b) & 0xFF]
    return crc


def enc(buf, off, size, key=0xFFFF):
    """JieLi ENC stream cipher (in place)"""
    for i in range(size):
        buf[off + i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF


def sfc(buf, off, size, base, key):
    """SFC cipher: ENC per 32-byte block, key mixed with the block address"""
    for i in range(0, size, 32):
        enc(buf, off + i, min(size - i, 32), key ^ ((off + i - base) >> 2))


def chipkey_decode(d):
    """chip key from the 32-byte isd_config blob"""
    thesum = sum(d[:16]) & 0xFF
    thesum = 0xAA if thesum >= 0xE0 else 0x55 if thesum <= 0x10 else thesum
    return sum(1 << i for i in range(16) if (d[16 + i] ^ d[15 - i]) < thesum)


def entry(dcrc, offset, size, flags, resvd, index, name, fill=0xFF):
    if isinstance(name, bytes):                     # 16 raw bytes: name + the packer's trailing fields
        nm = name
    else:
        nm = name.encode() + b"\0"
        nm = nm + bytes([fill]) * (16 - len(nm))
    body = struct.pack("<HIIBBH16s", dcrc, offset & 0xFFFFFFFF, size & 0xFFFFFFFF, flags, resvd, index, nm)
    return struct.pack("<H", crc16(body)) + body


def key_blob(key):
    """32 bytes that chipkey_decode() turns into `key` (+ CRC16)"""
    d = bytearray([0x10] * 16) + bytearray(16)        # sum 0 -> threshold 0x55
    for i in range(16):
        d[16 + i] = d[15 - i] ^ (0x00 if key >> i & 1 else 0xFF)
    assert chipkey_decode(bytes(d)) == key
    return bytes(d) + struct.pack("<H", crc16(d))


def sdk_file(rel):
    root = Path(SDK or os.environ.get("AC79_SDK", ""))
    path = root / "cpu" / "wl82" / "tools" / rel
    try:
        return path.read_bytes()
    except OSError as e:
        raise SystemExit(f"fm1pkg_make: {path}: {e.strerror}. Set AC79_SDK to a JieLi AC79 SDK checkout.")


def flash_image(app, key):
    if SMK_BASE:
        return spl_image(app, key)
    spl = sdk_file("uboot.boot")                         # SDK SPL, BANKCB-encoded as in the SDK
    cfg_tool = sdk_file("cfg_tool.bin")
    eq = sdk_file("cfg/eq_cfg_hw.bin")
    isd = key_blob(key) + b"[FELUCCA]\r\n"
    f = bytearray(b"\xFF" * FLASH_SIZE)
    # ---- head
    spl_off = 0xA0
    isd_off = spl_off + len(spl)
    assert isd_off + len(isd) <= 0x4000
    f[spl_off:spl_off + len(spl)] = spl
    f[isd_off:isd_off + len(isd)] = isd
    top = (entry(crc16(spl), spl_off, len(spl), 0x00, 0x00, 0, "uboot.boot") +
           entry(crc16(isd), isd_off, len(isd), 0x02, 0x80, 0, "isd_config.ini") +
           entry(0xFFFF, 0x4000, 0xFFFFFFFF, 0x81, 0xFF, 0, "app_dir_head") +
           entry(0xFFFF, 0xFF000, 0x1000, 0x12, 0x01, 0xFFFF, "key_mac"))
    hdr = struct.pack("<H", crc16(FLASH_HDR)) + FLASH_HDR
    head = bytearray(hdr + top)
    for o in range(0, len(head), 32):                   # header and each JLFS entry: ENC 0xFFFF
        enc(head, o, 32)                                # (a flash dump of a real SMK37 has them encoded)
    f[0:len(head)] = head
    # ---- app area (plain first, then SFC)
    if len(app) > APP_SLOT:
        raise SystemExit(f"app is {len(app)} B, the slot is {APP_SLOT} B")
    app = app + b"\xFF" * (APP_SLOT - len(app))
    blk = 0x120 + APP_SLOT + len(cfg_tool)              # app_area_head block size
    area = bytearray(b"\xFF" * blk)
    area[0x120:0x120 + APP_SLOT] = app
    area[0x120 + APP_SLOT:blk] = cfg_tool
    ents = [entry(crc16(app), 0x120, APP_SLOT, 0x82, 0xFF, 0, "app.bin"),
            entry(crc16(cfg_tool), 0x120 + APP_SLOT, len(cfg_tool), 0x82, 0xFF, 0, "cfg_tool.bin"),
            # region descriptors; the bytes after the names are SDK packer fields (VM size, "CODE", "AUTO")
            entry(0xFFFF, 0x93000, 352256, 0x12, 0x80, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),
            entry(0xFFFF, 0, FLASH_SIZE, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),
            entry(0xFFFF, 0xE9000, 0x1000, 0x92, 0x80, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")),
            entry(0xFFFF, 0xEA000, 0x12000, 0x92, 0x81, 1, "USR")]
    area[32:32 + len(b"".join(ents))] = b"".join(ents)
    area[0:32] = entry(crc16(area[32:blk]), 0x02000120, blk, 0x83, 0xFF, 0, "app_area_head")
    # nested cfg directory right after the block: SDK default EQ table
    eq_e = entry(crc16(eq), 0x40, len(eq), 0x82, 0xFF, 1, "eq_cfg_hw.bin")
    cfg_body = eq_e + eq
    cfg = entry(crc16(cfg_body), 0x20, 0x20 + len(cfg_body), 0x83, 0xFF, 1, "cfg") + cfg_body
    region = bytearray(area + cfg)
    sfc(region, 0, len(region), 0, key)
    f[0x4000:0x4000 + len(region)] = region
    return bytes(f)


def _jrle_dec(buf, off, size):
    """JieLi RLE: a flag byte of 8 pack bits, then per bit 0 = one literal byte, 1 = (count-1, byte)."""
    out = bytearray()
    p, end = off, off + size
    while p < end:
        flag = buf[p]
        p += 1
        for b in range(8):
            if p >= end:
                break
            if flag >> b & 1:
                n, v = buf[p], buf[p + 1]
                p += 2
                out += bytes([v]) * (n + 1)
            else:
                out.append(buf[p])
                p += 1
    return bytes(out)

def _jrle_enc(data):
    """The inverse: pack runs of >= 2 equal bytes: a flag byte of 8 pack bits, 1 = (count-1, byte)."""
    out = bytearray()
    i = 0
    while i < len(data):
        flag_at = len(out)
        out.append(0)
        flag = 0
        for b in range(8):
            if i >= len(data):
                break
            j = i + 1
            while j < len(data) and j - i < 256 and data[j] == data[i]:
                j += 1
            if j - i >= 2:
                flag |= 1 << b
                out += bytes([j - i - 1, data[i]])
                i = j
            else:
                out.append(data[i])
                i += 1
        out[flag_at] = flag
    return bytes(out)

def base_slot(base_path=None, key=None):
    """(offset, size) of the app.bin slot, read out of the base image's own app_area_head table.

    This is the authority for every size gate in the build: app.ld's XIP LENGTH, build.py's image
    check and size_report.py's budget. It is not a board constant - the FM-1's 0x8DFBC is 39,756 B
    short of what the SMK-37 base actually reserves, and taking the constant as the limit made a
    valid image look 532 B over budget (and threw away 39 KB of usable flash).

    The table is the decrypted 0x120-byte block at 0x4000; app_area_head is entry 0 and app.bin the
    first descriptor inside it (entry 1), with its offset relative to the table's own base (0x4000)."""
    from pathlib import Path
    path = Path(base_path or SMK_BASE)
    key = KEY if key is None else key
    if not path or not path.exists():
        raise SystemExit(f"base_slot: no base image at {path}")
    with open(path, "rb") as f:
        f.seek(0x4000)
        area = bytearray(f.read(0x120))
    if len(area) != 0x120:
        raise SystemExit(f"base_slot: {path}: no app_area table at 0x4000")
    sfc(area, 0, len(area), 0, key)
    if area[16:25] != b"app_area_":
        raise SystemExit(f"base_slot: {path}: the area head is not app_area_head (wrong key?)")
    e = area[32:64]
    if e[16:23] != b"app.bin":
        raise SystemExit(f"base_slot: {path}: the first app_area entry is not app.bin")
    return struct.unpack("<I", e[4:8])[0], struct.unpack("<I", e[8:12])[0]

def spl_image(app, key):
    """The SMK-37 flash image: the proven bootable image with this app in the app slot.

    That image is the last build known to boot on the unit, so its head (the SPL and the isd_config
    that sets up the clocks, the SPI and the SDRAM) and its app_area_head region table are exactly
    what the part boots from. The head is copied verbatim: building one from SDK defaults is the
    FM-1's way and does not boot here.

    Only the app payload inside the slot changes. The table is not rewritten at all, and in
    particular every descriptor keeps its stored CRC, because the CRCs are per-descriptor over that
    descriptor's own 30-byte body:

        crc16(entry[2:32]) == entry[0:2]

    which is exactly what the loader checks (ldr_core step 2: read 32 bytes at LDR_APP_LO, decrypt
    them with the chip key, compare crc16(sec + 2, 30) with rd16(sec)). None of them hashes the
    payload, so a splice cannot invalidate one. Recomputing a crc over a wider span than 30 bytes -
    as an earlier revision of this function did over the whole 0x120 block - makes that check fail
    and the unit drops to the mask-ROM u-boot, which looks like "the package was rejected" but is
    really "the app area no longer decrypts". The descriptors are therefore left byte for byte
    alone and the result is asserted to differ from the base only inside the slot.

    The app slot is read from the table (offset 0x120, size 0x97B08) rather than assumed.

    The payload is spliced SFC-encrypted, exactly as the SDK packer writes it (flash_image encrypts
    the whole app_area block with one call). The part does not read the app raw: isd_config turns on
    the SPI-flash cipher and the controller XORs every 32-byte block with key ^ (offset-from-0x4000 >>
    2) on the way through, in hardware, for the XIP fetches the SPL makes. Plain bytes through that
    decryptor is noise, so a raw splice boots as a blank screen and does not fall back to u-boot
    either, because the descriptor CRCs it jumps past are all still valid.

    The stream is positioned from 0x4000, the app_area block's own base, so a splice at 0x120 inside
    it uses the same per-block keys the loader and the hardware use. The app.bin descriptor's dcrc
    field does hash this padded payload on a stock-packed image - crc16(app.bin plaintext + 0xFF
    padding) - but nothing validates it (ldr_core step 2 checks only the 32-byte area head), so the
    table keeps the base's value."""
    from pathlib import Path
    base = bytearray(Path(SMK_BASE).read_bytes())
    if len(base) != SMK_FLASH_SIZE:
        raise SystemExit(f"base {SMK_BASE}: {len(base):#x} B, want {SMK_FLASH_SIZE:#x}")
    table = bytes(base[0x4000:0x4120])                  # kept to check the splice leaves it alone
    # entry layout: crc16[0:2] dcrc[2:4] offset[4:8] size[8:12] flags[12] resvd[13] index[14:16] name[16:32]
    a_off, a_size = base_slot(SMK_BASE, key)
    if a_off == 0 or a_size == 0 or 0x4000 + a_off + a_size > len(base):
        raise SystemExit(f"base {SMK_BASE}: implausible app entry ({a_off:#x} + {a_size:#x})")
    if len(app) > a_size:
        raise SystemExit(f"app is {len(app)} B, the slot is {a_size:#x} B")
    plain = app + b"\xFF" * (a_size - len(app))
    base[0x4000 + a_off:0x4000 + a_off + a_size] = plain
    sfc(base, 0x4000 + a_off, a_size, 0x4000, key)       # the stream counts from the area's own 0x4000
    if bytes(base[0x4000:0x4120]) != table:
        raise SystemExit("splice_clobbered the area table")     # cannot happen: the slot starts at 0x120
    # The round trip is the boot check: the part decrypts what is written here and jumps to it, so
    # anything that does not come back as this exact app is a blank screen on the stage.
    back = bytearray(base[0x4000 + a_off:0x4000 + a_off + a_size])
    sfc(back, 0, len(back), -a_off, key)
    if bytes(back) != plain:
        raise SystemExit("splice: the app slot does not decrypt back to this build")
    if bytes(base[0x4000 + a_off:0x4000 + a_off + 256]) == plain[:256]:
        raise SystemExit("splice: the app slot is unencrypted - the part reads it as noise")
    return bytes(base)


def ufw(flash, ota, product):
    files = [(0, "flash.bin", flash), (100, "ota.bin", ota)]
    off, ents, data = 0x400, [], bytearray()
    for i, (typ, name, d) in enumerate(files):
        off = (off + 0xFF) & ~0xFF
        e = bytearray(0x50)
        struct.pack_into("<HHHHIII", e, 0, typ, i, crc16(d), 0, off, len(d), (len(d) + 31) & ~31)
        e[0x40:0x40 + len(name)] = name.encode()
        ents.append((e, off, d))
        off += len(d)
    total = off
    hdr = bytearray(0x40)
    struct.pack_into("<IH", hdr, 4, total, len(files))
    struct.pack_into("<HHH", hdr, 10, 0x0004, 0x0200, 0)     # header fields as written by the SDK packer
    hdr[16:16 + len(UFW_CHIP)] = UFW_CHIP
    lst = bytearray()
    for e, _, _ in ents:
        x = bytearray(e)
        enc(x, 0, 0x50)
        lst += x
    struct.pack_into("<H", hdr, 2, crc16(lst))
    struct.pack_into("<H", hdr, 0, crc16(hdr[2:0x40]))
    enc(hdr, 0, 0x40)
    logical = bytearray(b"\xFF" * total)
    logical[0:0x40] = hdr
    logical[0x40:0x40 + len(lst)] = lst
    for e, o, d in ents:
        logical[o:o + len(d)] = d
    # .fwsc: one marker byte after each of the first 20 0x2F-byte blocks (the identity)
    if len(product) > 20 or any((ord(c) + i + 1) & 0xFF == 0x7D for i, c in enumerate(product)):
        raise SystemExit(f"product {product!r}: at most 20 characters, none encoding to the unused marker")
    raw = bytearray()
    for i in range(20):
        ch = product[i] if i < len(product) else None
        m = (ord(ch) + i + 1) & 0xFF if ch else 0x7D
        raw += logical[i * 0x2F:(i + 1) * 0x2F] + bytes([m])
    raw += logical[20 * 0x2F:]
    return bytes(raw)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app", type=Path)
    ap.add_argument("ota", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--product", default="FM-1_900")
    ap.add_argument("--key", type=lambda s: int(s, 0), default=KEY)
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--base", type=Path, default=None, metavar="BIN",
                    help="the proven stock image the package is built on (its head, SPL and boot config); "
                         "default: <repo>/recovery/bootable/restore_0_9D000.bin")
    a = ap.parse_args()
    global SDK, SMK_BASE
    if a.base is None:
        a.base = Path(__file__).resolve().parent.parent.parent / "recovery" / "bootable" / "restore_0_9D000.bin"
    SMK_BASE = Path(a.base)
    SDK = a.sdk
    pkg = ufw(flash_image(a.app.read_bytes(), a.key), a.ota.read_bytes(), a.product)
    a.out.write_bytes(pkg)
    print(f"package {a.out}: {len(pkg)} B, identity {a.product}, chip key {a.key:#06x}")


if __name__ == "__main__":
    main()
