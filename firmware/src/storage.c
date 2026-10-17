/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#include "../hal/smk_map.h"
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)
/* P8c: the object map, asserted at build time (tests/flash_map_test.c checks the same numbers on the host). */
_Static_assert(FL_SET_BASE % ST_SECTOR == 0u, "settings sector aligned");
_Static_assert(FL_PROJ_BASE == FL_SET_BASE + 2u * ST_SECTOR, "projects follow the settings A/B pair");
_Static_assert(FL_PRE_BASE == FL_PROJ_BASE + 4u * 2u * ST_SECTOR, "user presets follow the four projects");
_Static_assert(FL_FM6_A == FL_PRE_BASE + 2u * 2u * ST_SECTOR, "the FM6 pair follows the two low preset banks");
_Static_assert(FL_FM6_B == FL_FM6_A + ST_SECTOR, "FM6 copy B is the next sector");
_Static_assert(FL_MIX_BASE == FL_FM6_B + ST_SECTOR, "mixer sector follows FM6 copy B");
_Static_assert(FL_MIX_BASE + ST_SECTOR <= FL_OTA_LO, "mixer sector stays out of the OTA staging window");
_Static_assert(FL_OTA_HI <= SMP_USER_BASE, "OTA staging stays clear of the user samples");
_Static_assert(SMP_USER_BASE + SMP_USER_SIZE <= FL_PRE_HI, "user samples stay clear of the high preset banks");
_Static_assert(FL_PRE_HI + 5u * 2u * ST_SECTOR == FL_UPF_BASE, "the high preset banks fill the space before the upf banks");
_Static_assert(FL_UPF_BASE + 3u * 2u * ST_SECTOR <= FL_DATA_HI, "the upf banks stay inside the store");
_Static_assert(FL_DATA_LO > 0xC0000u && FL_DATA_LO >= 0xC2000u, "the store starts above BTIF / USRTRIM");

/* flash map, above the SMK Bluetooth block (0xC0000) and trim (0xC1000):
 *   settings        FL_SET_BASE      A/B
 *   projects        FL_PROJ_BASE     4 x A/B
 *   user presets    FL_PRE_BASE      banks 0/1, A/B (0xCC000..0xCFFFF)
 *   FM6 patches     FL_FM6_A/B       OBJ_FM6BANK (retired) and OBJ_UPFM6_0: one pair, see below
 *   mixer parts     FL_MIX_BASE      one record (mixer_keep.c)
 *   OTA staging     FL_OTA_LO        the loader's window
 *   user samples    SMP_USER_BASE    (eng_sample.c)
 *   user presets    FL_PRE_HI        banks 2..6, A/B (0xEE000..0xF7FFF)
 *   FM6 patches     FL_UPF_BASE      OBJ_UPFM6_1..3, A/B (0xF8000..0xFDFFF)
 * The five high preset banks and the three high patch banks sit above the user samples because the low store is
 * full (P8c: 0xCC000..0xD2FFF). FL_FM6_A held the FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK, retired): both that
 * object and OBJ_UPFM6_0 use the same pair, told apart by the commit record's type. The bank is only read, once, to
 * move its patches into the user presets (up_fm6.c upf_boot); the first write of the new object goes to the sector
 * that does not hold the bank's newest copy (st_save_to), so a power cut never loses both.
 * A table, not arithmetic: the map is no longer one run of A/B pairs. */
enum {
    OBJ_SETTINGS,
    OBJ_PROJECT0, OBJ_PROJECT1, OBJ_PROJECT2, OBJ_PROJECT3,
    OBJ_UPRESET0, OBJ_UPRESET1, OBJ_UPRESET2, OBJ_UPRESET3, OBJ_UPRESET4, OBJ_UPRESET5, OBJ_UPRESET6,
    OBJ_FM6BANK,
    OBJ_UPFM6_0, OBJ_UPFM6_1, OBJ_UPFM6_2, OBJ_UPFM6_3,
    OBJ_COUNT
};
_Static_assert(OBJ_UPRESET6 - OBJ_UPRESET0 == 6, "upreset.c: one object per user preset bank (seven)");
_Static_assert(OBJ_UPFM6_3 - OBJ_UPFM6_0 == 3, "up_fm6.c: one object per patch bank above 0 (four)");

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

/* copy A's sector of every object (copy B is the next one); see the flash map above */
static const uint32_t st_addr[OBJ_COUNT] = {
    [OBJ_SETTINGS]     = FL_SET_BASE,
    [OBJ_PROJECT0]     = FL_PROJ_BASE,
    [OBJ_PROJECT1]     = FL_PROJ_BASE + 2u * ST_SECTOR,
    [OBJ_PROJECT2]     = FL_PROJ_BASE + 4u * ST_SECTOR,
    [OBJ_PROJECT3]     = FL_PROJ_BASE + 6u * ST_SECTOR,
    [OBJ_UPRESET0]     = FL_PRE_BASE,
    [OBJ_UPRESET1]     = FL_PRE_BASE + 2u * ST_SECTOR,
    [OBJ_UPRESET2]     = FL_PRE_HI,
    [OBJ_UPRESET3]     = FL_PRE_HI + 2u * ST_SECTOR,
    [OBJ_UPRESET4]     = FL_PRE_HI + 4u * ST_SECTOR,
    [OBJ_UPRESET5]     = FL_PRE_HI + 6u * ST_SECTOR,
    [OBJ_UPRESET6]     = FL_PRE_HI + 8u * ST_SECTOR,
    [OBJ_FM6BANK]      = FL_FM6_A,
    [OBJ_UPFM6_0]      = FL_FM6_A,                    /* (the bank's pair: told apart by the type) */
    [OBJ_UPFM6_1]      = FL_UPF_BASE,
    [OBJ_UPFM6_2]      = FL_UPF_BASE + 2u * ST_SECTOR,
    [OBJ_UPFM6_3]      = FL_UPF_BASE + 4u * ST_SECTOR,
};

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    return st_addr[obj] + copy * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > ST_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || st_current(obj, &h) < 0 || h.len > max)
        return -1;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* save into copy `to` (0 A, 1 B; -1: the one that is not the current copy, as st_save) */
static int st_save_to(uint32_t obj, const void *src, uint32_t len, int to)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX || to > 1)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    if (to < 0)
        to = cur == 0 ? 1 : 0;                        /* write the other copy */
    base = st_sector(obj, (uint32_t)to);
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)to;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = (uint32_t)to;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}

static int st_save(uint32_t obj, const void *src, uint32_t len) { return st_save_to(obj, src, len, -1); }
