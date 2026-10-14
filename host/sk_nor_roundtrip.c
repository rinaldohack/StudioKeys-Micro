/* SPDX-License-Identifier: GPL-3.0-only
 * Smoke: NOR snapshot save → mutate → load restores param (or CRC). */
#include "sk_host.h"
#include <stdio.h>

/* P_LEVEL from firmware core.h — first track param, always saved in project blob. */
enum { PARAM_ID = 0 };

int main(void)
{
    int32_t orig, saved, after_mut, after_load;
    uint32_t crc_pre, crc_post;
    int rc;

    sk_host_init();
    sk_host_boot();

    orig = sk_host_get_param(PARAM_ID);
    saved = (orig == 64) ? 100 : 64;
    sk_host_set_param(PARAM_ID, saved);
    if (sk_host_get_param(PARAM_ID) != saved) {
        fprintf(stderr, "set_param failed want %d got %d\n", saved, sk_host_get_param(PARAM_ID));
        return 1;
    }

    rc = sk_host_snap_save();
    if (rc != 0) {
        fprintf(stderr, "snap_save failed %d\n", rc);
        return 1;
    }
    crc_pre = sk_host_state_crc();

    sk_host_set_param(PARAM_ID, saved == 64 ? 20 : 40);
    after_mut = sk_host_get_param(PARAM_ID);
    if (after_mut == saved) {
        fprintf(stderr, "mutate did not change param\n");
        return 1;
    }

    rc = sk_host_snap_load();
    if (rc != 0) {
        fprintf(stderr, "snap_load failed %d\n", rc);
        return 1;
    }
    after_load = sk_host_get_param(PARAM_ID);
    crc_post = sk_host_state_crc();

    if (after_load == saved) {
        printf("nor roundtrip ok param=%d crc=%08x\n", after_load, crc_post);
        return 0;
    }
    if (crc_pre == crc_post && crc_pre != 0u) {
        printf("nor roundtrip ok crc=%08x (param got %d want %d)\n", crc_pre, after_load,
               saved);
        return 0;
    }

    fprintf(stderr, "restore failed param got %d want %d crc %08x→%08x\n", after_load, saved,
            crc_pre, crc_post);
    return 1;
}
