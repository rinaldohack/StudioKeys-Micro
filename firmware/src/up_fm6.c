/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The user presets' FM6 patches (since 1.0.3): an FM6 user preset carries its track's patch, as a project does.
 * Four storage.c objects (OBJ_UPFM6_0..3, A/B: 0 is the 1.0.3 pair at 0xD0000 / 0xD1000, 1..3 at 0xF8000..0xFDFFF),
 * each holding 25 entries (4 x 25 = the 100 slots exactly), mirrored in RAM (banks 1..3 in the pool, bank 0 in .bss):
 * per user preset slot
 * the 128-byte packed patch (eng_fm6.c fm6_pack) with its 7-bit bytes packed 8 to the byte (112 bytes), and a tag.
 *
 * The tag ties an entry to its record: a hash of the user preset record (upreset.c up_rec_t) without its name. An
 * entry counts only while its record is an FM6 sound and still has that hash, so a rename keeps the patch, and any
 * other change of the record (UP_PUT from an editor that sends no patch, an erase, a restore of the banks, a power
 * cut between the record's write and this object's) leaves the record without one. Such a record loads as before
 * 1.0.3: SLOT F n -> that factory patch, OWN -> the init voice. The record's write comes first, this object's second
 * (up_store): a cut between them never gives a record another record's patch.
 *
 * The FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK, 27 slots B1..B27 that SLOT 8..34 loaded; the editor's FM6 target
 * 1, backup id 8) is retired. Its patches move into the user presets that used them, once (upf_boot):
 *   1. no valid OBJ_UPFM6 copy: the bank's newest valid copy is read; every FM6 user preset whose stored SLOT is
 *      8..34 (B1..B27) and has no entry gets that bank slot's patch (an empty bank slot: none, the init voice as
 *      before; SLOT F1..F8: none, which loads the factory patch as before);
 *   2. the object is written into the sector that does NOT hold that bank copy (st_save_to), and read back;
 *   3. a valid OBJ_UPFM6 copy is the "migration done" mark: the bank is never read again, and the next save of
 *      this object (A/B as any other) may erase the bank's last sector.
 * A power cut in 2 leaves the bank's copy whole and no valid OBJ_UPFM6: the next boot starts again at 1
 * (idempotent: the records did not change). No bank, or no flash: nothing to move, nothing written.
 * A full backup of older firmware (id 8, the bank) restores the same way into the restored user presets
 * (upf_migrate, editor_backup.c). Included by upreset.c. */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B": the retired bank (read only) */
#define FM6_BANK_N 27u
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, FM6_BANK_N */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint32_t rsv;
    uint8_t v[FM6_BANK_N][FM6_PACKED];
} fm6_bank_t;
_Static_assert(sizeof(fm6_bank_t) == 3472u, "FM6 bank layout");

static int fm6_bank_valid(const fm6_bank_t *b)
{
    uint32_t k, i;
    if (b->magic != FM6_BANK_MAGIC || b->ver != 1u || b->nslot != FM6_BANK_N || (b->used >> FM6_BANK_N))
        return 0;
    for (k = 0; k < FM6_BANK_N; k++)
        for (i = 0; i < FM6_PACKED; i++)
            if (b->v[k][i] > 127u)
                return 0;
    return 1;
}

#define UPF_MAGIC 0x36465055u                    /* "UPF6" */
#define UPF_PK 112u                              /* 128 x 7 bits */
#define UPF_PER 25u                              /* slots per object (25 x 116 + 16 = 2,916 B: one payload) */
#define UPF_BANKS ((UP_SLOTS + UPF_PER - 1u) / UPF_PER)
typedef struct {
    uint32_t tag;                                /* upf_tag of the record it belongs to */
    uint8_t pk[UPF_PK];
} upf_ent_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, UPF_PER */
    uint32_t used;                               /* bit k: e[k] holds a patch */
    uint32_t rsv;
    upf_ent_t e[UPF_PER];
} upf_t;
_Static_assert(sizeof(upf_t) == 2916u, "user preset FM6 patches layout (at most ST_PAYLOAD_MAX, 3840)");
_Static_assert(UPF_BANKS == 4u, "storage.c: OBJ_UPFM6_0..3");
_Static_assert(UPF_BANKS * UPF_PER == UP_SLOTS, "100 user presets: 4 patch banks of 25");
_Static_assert(sizeof(up_rec_t) == 192u && sizeof(((up_rec_t *)0)->name) == 12u, "upf_tag: the name at bytes 4..15");
static upf_t upf;                                /* bank 0: slots 0..24 (the 1.0.3 object's pair) */
static upf_t upf_hi[UPF_BANKS - 1u] __attribute__((section(".pool")));
static uint8_t upf_dirty;                        /* bit b: bank b changed since its last write */
static upf_t *upf_bk(uint32_t b) { return b ? &upf_hi[b - 1u] : &upf; }

static int upf_valid(const upf_t *u) { return u->magic == UPF_MAGIC && u->ver == 1u && u->nslot == UPF_PER; }

static void upf_empty_bank(uint32_t b)
{
    upf_t *u = upf_bk(b);
    memset(u, 0, sizeof *u);
    u->magic = UPF_MAGIC;
    u->ver = 1;
    u->nslot = UPF_PER;
}

static void upf_empty(void)
{
    uint32_t b;
    for (b = 0; b < UPF_BANKS; b++)
        upf_empty_bank(b);
    upf_dirty = 0;
}

static uint32_t upf_tag(const up_rec_t *r)       /* FNV-1a of the record without its name (bytes 4..15) */
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *r; i++)
        if (i < 4u || i >= 16u)
            h = (h ^ b[i]) * 16777619u;
    return h;
}

static int upf_fm6(uint32_t k) { return up_used(k) && up_rec(k)->engine == ENGI_FM6; }

/* slot k's patch -> pk (128 bytes), 0 = it has one */
static int upf_get(uint32_t k, uint8_t *pk)
{
    uint32_t i, acc = 0, n = 0, o = 0;
    const uint8_t *d;
    const upf_t *u = k < UP_SLOTS ? upf_bk(k / UPF_PER) : 0;
    const upf_ent_t *e = u ? &u->e[k % UPF_PER] : 0;
    if (!u || !upf_valid(u) || !((u->used >> (k % UPF_PER)) & 1u) || !upf_fm6(k) ||
        e->tag != upf_tag(up_rec(k)))
        return 1;
    d = e->pk;
    for (i = 0; i < FM6_PACKED; i++) {
        while (n < 7u) {
            acc |= (uint32_t)d[o++] << n;
            n += 8u;
        }
        pk[i] = (uint8_t)(acc & 127u);
        acc >>= 7;
        n -= 7u;
    }
    return 0;
}

/* slot k's patch = pk (128 bytes; every value into its range), tagged with the record as it is now. RAM only */
static void upf_set(uint32_t k, const uint8_t *pk)
{
    uint8_t v[FP_SIZE + 1u], c[FM6_PACKED];
    uint32_t i, acc = 0, n = 0, o = 0, b;
    upf_t *u;
    upf_ent_t *e;
    if (k >= UP_SLOTS)
        return;
    b = k / UPF_PER;
    u = upf_bk(b);
    e = &u->e[k % UPF_PER];
    if (!upf_valid(u))
        upf_empty_bank(b);
    fm6_unpack(pk, v);
    fm6_pack(v, c);
    memset(e->pk, 0, UPF_PK);
    for (i = 0; i < FM6_PACKED; i++) {
        acc |= (uint32_t)(c[i] & 127u) << n;
        n += 7u;
        while (n >= 8u) {
            e->pk[o++] = (uint8_t)acc;
            acc >>= 8;
            n -= 8u;
        }
    }
    e->tag = upf_tag(up_rec(k));
    u->used |= 1u << (k % UPF_PER);
    upf_dirty |= (uint8_t)(1u << b);
}

/* bank b to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM). Bank 0's first write keeps the retired bank's
 * newest copy (see the top) */
static int upf_save_bank(uint32_t b)
{
#if SKM_FLASH
    st_hdr_t h;
    int to = -1;
    if (!flash_ok)
        return 3;
    if (b == 0u && st_current(OBJ_UPFM6_0, &h) < 0)
        to = st_current(OBJ_FM6BANK, &h) == 0 ? 1 : 0;
    if (st_save_to(OBJ_UPFM6_0 + b, upf_bk(b), sizeof(upf_t), to))
        return 2;
#else
    (void)b;
    return 3;
#endif
    return 0;
}

/* every changed bank to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM). Bank 0 goes last: it is the mark of
 * a finished migration (upf_boot), so a cut while the retired bank's patches move cannot pass for "done" */
static int upf_save(void)
{
    uint32_t b = UPF_BANKS;
    int rc = 3;
    while (b--) {
        if (!(upf_dirty & (1u << b)))
            continue;
        rc = upf_save_bank(b);
        if (rc == 2)
            return 2;
        if (rc == 0)
            upf_dirty &= (uint8_t)~(1u << b);
    }
    return rc;
}

/* the retired bank's patches -> the FM6 user presets whose stored SLOT is a B slot and have no patch yet; the count */
static uint32_t upf_migrate(const fm6_bank_t *b)
{
    uint32_t k, moved = 0;
    for (k = 0; k < UP_SLOTS; k++) {
        const up_rec_t *r = up_rec(k);
        uint8_t pk[FM6_PACKED];
        int32_t s;
        if (!upf_fm6(k) || !upf_get(k, pk))
            continue;
        s = up_value(r, r->np - 1u) - 8;   /* 1.0.2 wrote B1..B27 as SLOT 8..34, when the factory was F1..F8 */
        if (s >= 0 && s < (int32_t)FM6_BANK_N && ((b->used >> s) & 1u)) {
            upf_set(k, b->v[s]);
            moved++;
        }
    }
    return moved;
}

static void upf_boot(void)                       /* up_boot, after the user preset banks */
{
#if SKM_FLASH
    st_hdr_t h;
    int have0 = flash_ok && st_load(OBJ_UPFM6_0, &upf, sizeof upf) == (int)sizeof upf && upf_valid(&upf);
    uint32_t b;
    upf_dirty = 0;
    if (!have0)
        upf_empty_bank(0);
    for (b = 1; b < UPF_BANKS; b++) {
        if (flash_ok && st_load(OBJ_UPFM6_0 + b, upf_bk(b), sizeof(upf_t)) == (int)sizeof(upf_t) &&
            upf_valid(upf_bk(b)))
            continue;
        upf_empty_bank(b);
    }
    if (!have0 && flash_ok && st_current(OBJ_FM6BANK, &h) >= 0 && h.len == sizeof(fm6_bank_t) &&
        fm6_bank_valid((const fm6_bank_t *)st_buf)) {
        upf_migrate((const fm6_bank_t *)st_buf);   /* (st_buf holds the bank until upf_save) */
        upf_save();
    }
#else
    upf_empty();
#endif
}

/* up_load: track t (its values just loaded from slot k) gets the record's patch; without one, as before 1.0.3:
 * SLOT F n that factory patch, OWN the init voice. SLOT then shows F n or OWN (fm6_adopt) */
static void upf_track_load(track_t *t, uint32_t k)
{
    uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NTRK || t->eng_req != ENGI_FM6)
        return;
    if (upf_get(k, pk)) {
        int32_t s = t->p[P_E7];
        memcpy(pk, s >= 0 && s < 8 ? FM6_FACTORY[s] : FM6_INIT, FM6_PACKED);   /* F1..F8; 8 was OWN, 9..34 the bank */
    }
    fm6_unpack(pk, v);
    fm6_set_patch(tr, v);
    fm6_adopt(tr);
}

/* up_store: slot k (its record just written from track tr) gets that track's patch; upf_save's result */
static int upf_store(uint32_t k, uint32_t tr)
{
    uint8_t pk[FM6_PACKED];
    fm6_pack(fm6_patch[tr % NTRK], pk);
    upf_set(k, pk);
    return upf_save();
}
