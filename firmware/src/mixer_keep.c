/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The four mixer parts across power-off. Each part is one sound: its engine and its preset.
 * song.sel is kept with them. The track parameters and the sequences stay the power-on ones.
 *
 * One 4K sector at FL_MIX_BASE (0xD2000), the gap after FM6 copy B (0xD1000) and before OTA
 * (0xE0000). Bluetooth and trim (0xC0000..0xC1FFF) are not this sector. This is not persist_t:
 * settings size checks stay as they are, and st_save's A/B objects are not used.
 *
 * persist_boot runs before skm_init, and skm_init's track_defaults loop then replaces
 * every part. A restore from persist_boot would be overwritten. mixer_keep_boot is the recall. */
#if !SKM_FLASH
/* Must be called at the END of skm_init in main.c after the track_defaults loop. */
void mixer_keep_boot(void) {}
void mixer_keep_save(void) {}
#else
#define MIX_MAGIC 0x584D494Du                         /* 'MIX' */
#define MIX_VER   1u

typedef struct {
    uint32_t magic;
    uint8_t sel;                                      /* song.sel */
    uint8_t ep[NPART * 2u];                           /* four times: eng_req, preset */
    uint8_t ver;
    uint8_t sum;
    uint8_t rsv;                                      /* written 0; the record is 16 bytes */
} mix_keep_t;

_Static_assert(NPART == 4u, "mixer record is the four parts");
_Static_assert(sizeof(mix_keep_t) == 16u, "mixer record");
_Static_assert(FL_MIX_BASE == 0x000D2000u, "mixer sector");
_Static_assert(FL_MIX_BASE >= 0x000C2000u, "mixer sector stays out of Bluetooth and trim");
_Static_assert(FL_MIX_BASE == FL_FM6_B + 0x1000u, "mixer sector follows FM6 B");
_Static_assert(FL_MIX_BASE + 0x1000u <= FL_OTA_LO, "mixer sector stays out of OTA");

static mix_keep_t mix_shadow;
static uint8_t mix_have;

static uint8_t mix_csum(const mix_keep_t *r)
{
    uint8_t s = (uint8_t)r->magic;
    uint32_t i;
    s = (uint8_t)(s + (uint8_t)(r->magic >> 8));
    s = (uint8_t)(s + (uint8_t)(r->magic >> 16));
    s = (uint8_t)(s + (uint8_t)(r->magic >> 24));
    s = (uint8_t)(s + r->sel + r->ver + r->rsv);
    for (i = 0; i < sizeof r->ep; i++)
        s = (uint8_t)(s + r->ep[i]);
    return s;
}

static void mix_capture(mix_keep_t *r)
{
    uint32_t i;
    memset(r, 0, sizeof *r);
    r->magic = MIX_MAGIC;
    r->ver = MIX_VER;
    r->sel = song.sel;
    for (i = 0; i < NPART; i++) {
        r->ep[i * 2u] = trk[i].eng_req;
        r->ep[i * 2u + 1u] = trk[i].preset;
    }
    r->sum = mix_csum(r);
}

static int mix_sane(const mix_keep_t *r)
{
    uint32_t i;
    if (r->magic != MIX_MAGIC || r->ver != MIX_VER || r->rsv || r->sum != mix_csum(r))
        return 0;
    if (r->sel >= NTRK)
        return 0;
    for (i = 0; i < NPART; i++)
        if (r->ep[i * 2u] >= NENGINES)
            return 0;
    return 1;
}

/* Must be called at the END of skm_init in main.c after the track_defaults loop. */
void mixer_keep_boot(void)
{
    mix_keep_t r;
    uint32_t i;
    mix_have = 0;
    if (!flash_ok || st_read(FL_MIX_BASE, &r, sizeof r) || !mix_sane(&r))
        return;                                         /* magic mismatch, or a bad record: factory tracks stay */
    undo_depth++;                                       /* power-on recall takes no undo copy */
    for (i = 0; i < NPART; i++) {
        track_t *t = &trk[i];
        set_engine_of(t, r.ep[i * 2u]);
        apply_preset_to(t, r.ep[i * 2u + 1u]);
        t->engine = t->eng_req;                         /* sounding engine, as skm_init snaps it */
    }
    song.sel = r.sel;
    undo_depth--;
    mix_shadow = r;
    mix_have = 1;
}

/* Main loop. Returns without erasing when the RAM shadow already holds this record.
 * Returns without erasing while the transport runs: st_erase is fl_erase4k with the audio
 * buffer silenced and IRQs off. Call again once playback has stopped. */
void mixer_keep_save(void)
{
    mix_keep_t rec, got;
    if (!flash_ok)
        return;
    mix_capture(&rec);
    if (mix_have && !memcmp(&rec, &mix_shadow, sizeof rec))
        return;
    if (transport_busy())
        return;
    if (st_erase(FL_MIX_BASE) || st_prog(FL_MIX_BASE, &rec, sizeof rec))
        return;
    if (st_read(FL_MIX_BASE, &got, sizeof got) || memcmp(&got, &rec, sizeof rec))
        return;
    mix_shadow = rec;
    mix_have = 1;
}
#endif
