/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * Desk / page sync smoke (plan phase 4). */
#include "sk_host.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
  uint32_t d, pages, i;
  sk_host_ui_t ui;

  sk_host_init();
  sk_host_boot();

  for (d = 0; d < 13u; d++) {
    sk_host_set_desk(d);
    sk_host_ui_get(&ui);
    if (ui.desk != (uint8_t)d) {
      fprintf(stderr, "desk %u ui.desk=%u\n", d, ui.desk);
      return 1;
    }
    if (!ui.engine_name[0]) {
      fprintf(stderr, "desk %u empty engine\n", d);
      return 1;
    }
  }

  sk_host_set_desk(13); /* SETTINGS */
  sk_host_ui_get(&ui);
  if (ui.desk != 0xFFu || !ui.menu) {
    fprintf(stderr, "settings desk want desk=0xFF menu=1 got desk=%u menu=%u home=%u\n",
            ui.desk, ui.menu, ui.home);
    return 1;
  }
  {
    const uint8_t settings_desk = ui.desk;

  pages = sk_host_page_count();
  if (pages < 37u) {
    fprintf(stderr, "page count %u\n", pages);
    return 1;
  }
  for (i = 0; i < pages; i++) {
    const char *t = sk_host_page_title(i);
    if (!t || !t[0])
      continue;
    if (sk_host_goto_page(i) != 0)
      continue;
    sk_host_ui_get(&ui);
    if (ui.page_title[0] && strcmp(ui.page_title, t) != 0) {
      fprintf(stderr, "page %u title host=%s ui=%s\n", i, t, ui.page_title);
      return 1;
    }
  }

  printf("desk sync ok engines=13 pages=%u settings_desk=%u\n", pages, settings_desk);
  }
  return 0;
}
