/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
/* P8c: flash map and sector budget.
 *
 * The firmware's persistent objects all live above the SMK Bluetooth block (BTIF 0xC0000, USRTRIM
 * 0xC1000, both through 0xC1FFF) and below the package area. This test asserts the map itself:
 *   - every object starts on a 4 KiB sector and fits the sector(s) it owns,
 *   - no object overlaps BTIF / USRTRIM or another object,
 *   - the whole store stays inside FL_DATA_LO..FL_DATA_HI,
 *   - the widened records (P6's 12 engine params, 100 user presets, their FM6 patch banks) fit one
 *     sector's payload.
 * It is a host test: the same constants the firmware compiles with, checked before a flash write can
 * ever land in the wrong place. Run by tests/run_tests.sh. */
#include <stdint.h>
#include <stdio.h>
#include "../firmware/hal/smk_map.h"

#define SECTOR 4096u
#define PAYLOAD (SECTOR - 256u)              /* storage.c: ST_SECTOR / ST_PAYLOAD_OFF */
#define BT_LO 0x000C0000u                    /* smk_map.h: BTIF */
#define BT_HI 0x000C2000u                    /* USRTRIM's end (0xC1000 + 0x1000) */
#define DATA_HI FL_DATA_HI
#define PROJ_STORE_SIZE 3584u                /* project.c: one project record */
#define PROJ_SLOTS 4u
#define PRE_BANKS 7u                         /* user presets: 100 slots, 7 banks of 16 records */
#define PRE_LO_BANKS 2u                      /* banks 0/1 in the low store */
#define PRE_HI_BANKS (PRE_BANKS - PRE_LO_BANKS)
#define UPF_BANKS 4u                         /* user preset FM6 patch banks (25 slots each: 4 x 25 = 100) */
#define UPF_HI_BANKS (UPF_BANKS - 1u)        /* bank 0 shares the retired FM6 bank's pair */
#define UP_BANK_SIZE 3080u                   /* upreset.c: 8 + 16 * 192 */
#define UPF_SIZE 2916u                       /* up_fm6.c: 25 * 116 + 16 */
#define SMA_OBJ_SECTORS (2u + PROJ_SLOTS * 2u + PRE_LO_BANKS * 2u + 2u + 1u)   /* settings A/B, projects A/B,
                                              * low preset banks A/B, FM6 pair, mixer */
#define SMA_HI_SECTORS (PRE_HI_BANKS * 2u + UPF_HI_BANKS * 2u)                /* high preset/patch banks */

static int bad;
static void ok(const char *what, int cond)
{
    printf("flash: %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond)
        bad = 1;
}
static int overlaps(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return a < b + bn && b < a + an;
}
/* one object: sector-aligned, inside the store, clear of BTIF, and not overlapping the others */
static int fits(const char *name, uint32_t off, uint32_t n)
{
    if (off % SECTOR) {
        printf("flash: %s is not on a 4 KiB sector (0x%05X)\n", name, off);
        return 0;
    }
    if (off < FL_DATA_LO || off + n > DATA_HI) {
        printf("flash: %s (0x%05X..0x%05X) leaves the store\n", name, off, off + n);
        return 0;
    }
    if (overlaps(off, n, BT_LO, BT_HI - BT_LO) || FL_HITS_BT(off, n)) {
        printf("flash: %s (0x%05X..0x%05X) hits BTIF / USRTRIM\n", name, off, off + n);
        return 0;
    }
    return 1;
}

int main(void)
{
    /* the objects, in the order storage.c lays them out */
    const struct {
        const char *name;
        uint32_t off, n;
    } obj[] = {
        {"settings A/B", FL_SET_BASE, 2u * SECTOR},
        {"projects A/B", FL_PROJ_BASE, PROJ_SLOTS * 2u * SECTOR},
        {"user preset banks 0/1 A/B", FL_PRE_BASE, PRE_LO_BANKS * 2u * SECTOR},
        {"FM6 pair (retired bank + patch bank 0)", FL_FM6_A, 2u * SECTOR},
        {"mixer parts", FL_MIX_BASE, SECTOR},
        {"OTA staging", FL_OTA_LO, FL_OTA_HI - FL_OTA_LO},
        {"user samples", SMP_USER_BASE, SMP_USER_SIZE},
        {"user preset banks 2..6 A/B", FL_PRE_HI, PRE_HI_BANKS * 2u * SECTOR},
        {"FM6 patch banks 1..3 A/B", FL_UPF_BASE, UPF_HI_BANKS * 2u * SECTOR},
    };
    const uint32_t n = (uint32_t)(sizeof obj / sizeof obj[0]);
    uint32_t i, j;

    for (i = 0; i < n; i++)
        ok(obj[i].name, fits(obj[i].name, obj[i].off, obj[i].n));
    for (i = 0; i < n; i++)
        for (j = i + 1u; j < n; j++)
            if (overlaps(obj[i].off, obj[i].n, obj[j].off, obj[j].n)) {
                printf("flash: %s overlaps %s\n", obj[i].name, obj[j].name);
                bad = 1;
            }
    ok("no two objects overlap", !bad);

    /* the store's objects tile from FL_DATA_LO to FL_OTA_LO with no hole inside the A/B pairs */
    ok("settings start at FL_DATA_LO", FL_SET_BASE == FL_DATA_LO);
    ok("projects follow settings", FL_PROJ_BASE == FL_SET_BASE + 2u * SECTOR);
    ok("low preset banks follow projects", FL_PRE_BASE == FL_PROJ_BASE + PROJ_SLOTS * 2u * SECTOR);
    ok("FM6 pair follows the low preset banks", FL_FM6_A == FL_PRE_BASE + PRE_LO_BANKS * 2u * SECTOR);
    ok("mixer follows the FM6 pair", FL_MIX_BASE == FL_FM6_A + 2u * SECTOR);
    ok("mixer ends before the OTA window", FL_MIX_BASE + SECTOR <= FL_OTA_LO);
    ok("OTA ends before the user samples", FL_OTA_HI <= SMP_USER_BASE);
    ok("user samples stay clear of the high preset banks", SMP_USER_BASE + SMP_USER_SIZE <= FL_PRE_HI);
    ok("high preset banks follow the user samples", FL_PRE_HI >= SMP_USER_BASE + SMP_USER_SIZE);
    ok("patch banks follow the high preset banks",
       FL_UPF_BASE == FL_PRE_HI + PRE_HI_BANKS * 2u * SECTOR);
    ok("patch banks stay inside the store", FL_UPF_BASE + UPF_HI_BANKS * 2u * SECTOR <= DATA_HI);

    /* the object budget: 17 low sectors + 16 high sectors; the store has 62, 15 stay free */
    ok("low object map is 17 sectors", SMA_OBJ_SECTORS == 17u);
    ok("high object map is 16 sectors", SMA_HI_SECTORS == 16u);
    ok("the store holds every object", (FL_OTA_HI - FL_OTA_LO) / SECTOR + SMA_OBJ_SECTORS + SMA_HI_SECTORS +
                                             (SMP_USER_SIZE / SECTOR) <= (DATA_HI - FL_DATA_LO) / SECTOR);

    /* P6's record growth and the 100-slot stores: every record still fits one sector's payload */
    ok("project record fits a sector", PROJ_STORE_SIZE <= PAYLOAD);
    ok("project header + record fits a sector", 32u + PROJ_STORE_SIZE <= SECTOR);
    ok("user preset record fits a sector", 72u * 2u + 64u <= PAYLOAD);   /* UP_PMAX * 2 bytes + name */
    ok("user preset bank fits a sector", UP_BANK_SIZE <= PAYLOAD);
    ok("user preset patch bank fits a sector", UPF_SIZE <= PAYLOAD);

    printf(bad ? "flash: FAILED\n" : "flash: all passed\n");
    return bad;
}
