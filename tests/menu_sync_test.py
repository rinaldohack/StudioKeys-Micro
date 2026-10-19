#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS companion
"""Drift guard: the companion's MENU rows must be the firmware's MENU rows.

The SETTINGS card addresses MENU rows by index through sk_host_menu_get/put (row == the CFW's
MI_ value). When firmware/src/menu_items.c grew METRO / COUNT-IN (P14) and FLAPPY / TETRIS
(P19/P20), plugin/StudioKEYS/SettingsDesk.h kept the old numbering, so
  - the ABOUT card, FLAPPY and TETRIS rows called sk_host_menu_put() with the wrong row (or a
    row past the end), and clicking them did nothing at all, and
  - the standalone "row >= MI_VALUES -> onAction()" mapping pointed at a value row.
Nothing crashed, so no test noticed. This one does: it parses both enums and fails on any
mismatch, and it also checks that every action row the plugin marks has a handler in
StudioKEYS.cpp (an unhandled action row is a dead button).

  tests/menu_sync_test.py [--quiet]
"""

import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
MENU_C = SRC / "firmware" / "src" / "menu_items.c"
SETTINGS_H = SRC / "plugin" / "StudioKEYS" / "SettingsDesk.h"
STUDIOKEYS_CPP = SRC / "plugin" / "StudioKEYS" / "StudioKEYS.cpp"

fails = 0


def fail(msg):
    global fails
    fails += 1
    print("menu sync: FAIL  " + msg)


def ok(msg):
    print("menu sync: %-58s ok" % msg)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def enum_body(text, name_regex=r"enum"):
    """Return the body of the first `enum [Name] { ... }` in text."""
    m = re.search(name_regex + r"\s*(?:[A-Za-z_]\w*)?\s*\{([^}]*)\}", text, flags=re.S)
    return m.group(1) if m else None


def parse_c_names(body):
    """Ordered list of (name, explicit_value_or_None) from a C enum body."""
    out = []
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        m = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*(-?\d+))?$", item)
        if not m:
            continue
        out.append((m.group(1), int(m.group(2)) if m.group(2) else None))
    return out


def values(names):
    """Resolve running values: (name -> value) and the order."""
    val, cur = {}, 0
    order = []
    for name, explicit in names:
        if explicit is not None:
            cur = explicit
        val[name] = cur
        order.append(name)
        cur += 1
    return val, order


def main():
    quiet = "--quiet" in sys.argv
    menu = strip_comments(MENU_C.read_text())
    settings = strip_comments(SETTINGS_H.read_text())
    plugin = strip_comments(STUDIOKEYS_CPP.read_text())

    body = enum_body(menu)
    if body is None:
        fail("no anonymous enum in firmware/src/menu_items.c")
        return 1
    fw_val, fw_order = values(parse_c_names(body))
    mi_rows = [n for n in fw_order if n.startswith("MI_") and n not in ("MI_COUNT", "MI_VALUES")]
    if not mi_rows:
        fail("MI_ rows not found in the firmware enum")
        return 1
    # the firmware's own guard: the action rows start at MI_VALUES (#define MI_VALUES MI_PANEL)
    m = re.search(r"#define\s+MI_VALUES\s+([A-Za-z_]\w*)", menu)
    if not m or m.group(1) not in fw_val:
        fail("MI_VALUES missing from the firmware")
        return 1
    n_values = fw_val[m.group(1)]

    body = enum_body(settings)
    if body is None:
        fail("no enum in plugin/StudioKEYS/SettingsDesk.h")
        return 1
    sk_val, sk_order = values(parse_c_names(body))
    k_rows = [n for n in sk_order if n.startswith("kMi")]
    if not k_rows:
        fail("kMi rows not found in SettingsDesk.h")
        return 1

    # 1. every firmware MENU row has the same index in the plugin
    for name in mi_rows:
        expect = fw_val[name]
        cand = "kMi" + name[3:].lower().capitalize()
        # kMiBpmsync, kMiRmaud, kMiExtClock ... the plugin spells each row kMi<Camel>
        matches = [k for k in k_rows if k.lower() == cand.lower()]
        if not matches:
            fail("%s (row %d) has no kMi counterpart in SettingsDesk.h" % (name, expect))
            continue
        got = sk_val[matches[0]]
        if got != expect:
            fail("%s is kMi row %d but the firmware's %s is row %d" % (matches[0], got, name, expect))
    if not quiet and not fails:
        ok("every firmware MENU row matches its kMi index (%d rows)" % len(mi_rows))

    # 2. nothing in the plugin points past the firmware's rows
    for k in k_rows:
        if k in ("kMiExtClock", "kMiUnleashed"):
            continue
        if sk_val[k] >= n_values and ("MI_" + k[3:].upper()) not in fw_val:
            fail("%s = %d is past MI_VALUES=%d and is not a firmware action row"
                 % (k, sk_val[k], n_values))

    # 3. the values are 0..N-1 in firmware order (the panel compares `row >= MI_VALUES`)
    if [sk_val[k] for k in k_rows if k not in ("kMiExtClock", "kMiUnleashed")] != list(range(len(mi_rows))):
        fail("the kMi rows are not 0..%d in firmware order" % (len(mi_rows) - 1))
    elif not quiet and not fails:
        ok("the kMi rows are contiguous 0..%d" % (len(mi_rows) - 1))

    # 4. every action row (row >= MI_VALUES) has a handler: a dead card row is the bug this
    #    file exists for. ExtClock / Unleashed are the plugin's own rows.
    handled = set(re.findall(r"row\s*==\s*sksettings::(kMi\w+)", plugin))
    action_rows = [k for k in k_rows
                   if sk_val[k] >= n_values and k not in ("kMiExtClock", "kMiUnleashed")]
    for k in action_rows:
        if k not in handled:
            fail("%s (row %d) is an action row but nothing handles it: a dead button"
                 % (k, sk_val[k]))
    if not quiet and action_rows and not fails:
        ok("every action row has a handler (%d rows)" % len(action_rows))

    # 5. and the plugin does not handle a row the firmware treats as a value row
    for k in sorted(handled):
        if k in sk_val and sk_val[k] < n_values:
            fail("%s is a value row (%d < MI_VALUES %d) but is handled as an action"
                 % (k, sk_val[k], n_values))

    if fails:
        print("MENU SYNC: %d failure(s)" % fails)
        return 1
    print("menu sync: the companion's MENU rows match the firmware")
    return 0


if __name__ == "__main__":
    sys.exit(main())
