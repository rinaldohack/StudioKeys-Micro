/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
 * MENU > LANG bit (PREF_LANG) get/set via host API. */
#include "sk_host.h"
#include <stdio.h>

int main(void)
{
  sk_host_init();
  sk_host_boot();
  sk_host_set_lang(0);
  if (sk_host_lang() != 0) {
    fprintf(stderr, "lang want EN(0) got %d\n", sk_host_lang());
    return 1;
  }
  sk_host_set_lang(1);
  if (sk_host_lang() != 1) {
    fprintf(stderr, "lang want ID(1) got %d\n", sk_host_lang());
    return 1;
  }
  sk_host_set_lang(0);
  if (sk_host_lang() != 0) {
    fprintf(stderr, "lang clear failed got %d\n", sk_host_lang());
    return 1;
  }
  puts("lang smoke ok EN/ID");
  return 0;
}
