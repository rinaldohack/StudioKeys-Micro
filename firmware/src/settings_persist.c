/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER4 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites.
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c).
 * zoom: no longer used (the large readout); kept as it was saved, unless it holds the LEDS setting (panel.c).
 * favorites.factory[15][28]: 1 after settings_init has seen this device (0 migrates a saved GREY to ICE once).
 * favorites.factory[15][24..25]: the key split (P13): [24] = split note, 0 = off; [25] = low | high << 4.
 * favorites.factory[15][29..31]: STYLE, MENU's flags, the layers seen (bytes no engine uses; 0 in older settings).
 * MENU's flags (ui.c PREF_*) are append-only bits whose 0 is the default: BPM SYNC (16) is clear in every older setting
 * = unlocked, LARGE (32) = OFF, read as saved (nothing to migrate, so importing twice changes nothing). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    struct { uint8_t factory[16][32]; uint32_t user, filter; } favorites;
} persist_t;
#define PERSIST_MAGIC 0x50455234u

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
    int old3 = n == (int)(sizeof *p - sizeof p->favorites) && p->magic == 0x50455233u;
    int old2 = n == (int)(sizeof *p - sizeof p->favorites - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old3 || old2 || old1)) return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2) p->bold = 0;
    if (!current) memset(&p->favorites, 0, sizeof p->favorites);
    if (p->favorites.factory[15][29] > 1u)        /* STYLE (ui.c ui_style): 2, the retired PIXEL = LINE; unknown = FLAT */
        p->favorites.factory[15][29] = p->favorites.factory[15][29] == 2u ? 1u : 0u;
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
    settings_leds = (uint8_t)leds_from_stored(p->zoom);
#ifdef SKM_FAVORITES
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites.filter = favorites.filter == 1u;
#if defined(FM4_NPRESETS) && !SKM_FM4
    {   /* DIGITAL's starred presets (engine 1, retired) -> the FM6 presets that cover them (fm4_convert.c) */
        uint32_t k;
        for (k = 0; k < FM4_NPRESETS; k++)
            if ((favorites.factory[ENGI_DIGITAL][0] >> k) & 1u)
                favorites.factory[ENGI_FM6][FM4_TO_FM6[k] / 8u] |= (uint8_t)(1u << (FM4_TO_FM6[k] % 8u));
        memset(favorites.factory[ENGI_DIGITAL], 0, sizeof favorites.factory[ENGI_DIGITAL]);
    }
#endif
#ifdef SMP_SET_PERC
    if ((favorites.factory[ENGI_SAMPLE][0] >> SMP_SET_PERC) & 1u) {   /* SAMPLE's PERC (preset 4, retired): DRUM's */
        favorites.factory[ENGI_DRUM][0] |= 1u;                         /* kit (core.h drum_from_perc) */
        favorites.factory[ENGI_SAMPLE][0] &= (uint8_t)~(1u << SMP_SET_PERC);
    }
#endif
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    /* P13: the key split rides in two bytes no engine uses (0 in every older settings record,
     * i.e. off). split_set clamps and releases sounding notes, so a restored backup never leaves
     * a note on the track the old routing chose. */
    split_set(p->favorites.factory[15][24], (uint32_t)(p->favorites.factory[15][25] & 15u),
              (uint32_t)(p->favorites.factory[15][25] >> 4));
    /* P14: the metronome (0 in every older record = off) and the count-in switch, in the next free byte */
    settings_metro = (uint8_t)((p->favorites.factory[15][26] & 7u) % 5u);
    settings_countin = (uint8_t)((p->favorites.factory[15][26] >> 4) & 1u);
    metro_set(settings_metro, settings_countin);
    return current ? 1 : 2;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom = leds_to_stored(settings.zoom, settings_leds);
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef SKM_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
#endif
    /* P13: the split lives in the favorites scratch bytes, always written from the live routing. */
    p->favorites.factory[15][24] = favorites.factory[15][24] = split_note;
    p->favorites.factory[15][25] = favorites.factory[15][25] = (uint8_t)(split_lo | (split_hi << 4));
    /* P14: the metronome's level and the count-in switch, from the live settings */
    p->favorites.factory[15][26] = favorites.factory[15][26] =
        (uint8_t)((settings_metro % 5u) | (settings_countin << 4));
}
