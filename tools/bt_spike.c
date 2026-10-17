/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* P6e spike: the smallest object that makes the linker pull the SDK's BT stack in, so tools/bt_spike.sh
 * can measure what it costs. Not part of the firmware build. */
#include <stdint.h>

int btctrler_task_init(const void *transport, const void *config);
int btstack_init(void);

int bt_spike(void)
{
    return btctrler_task_init(0, 0) + btstack_init();
}
