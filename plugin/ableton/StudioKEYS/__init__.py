# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs)
"""StudioKEYS Micro — Ableton Live MIDI Remote Script.

Install: copy this folder to
  ~/Music/Ableton/User Library/Remote Scripts/StudioKEYS
Then in Live: Link/Tempo/MIDI → Control Surface = StudioKEYS,
Input/Output = StudioKEYS Live (the keyboard's Live cable).

Uses the classic _Framework surface in studiokeys.py so it loads on Live 11/12
without depending on Ableton v3 mode tables. elements.py / mappings.py are
reference for a future v3 port.
"""
from __future__ import absolute_import, print_function, unicode_literals

from .studiokeys import StudioKEYS


def get_capabilities():
    from ableton.v2.control_surface.capabilities import (
        CONTROLLER_ID_KEY,
        NOTES_CC,
        PORTS_KEY,
        REMOTE,
        SCRIPT,
        controller_id,
        inport,
        outport,
    )

    return {
        CONTROLLER_ID_KEY: controller_id(
            vendor_id=0x4353,
            product_ids=[0x4B4D],
            model_name=["StudioKEYS Micro", "StudioKEYS Live"],
        ),
        PORTS_KEY: [
            inport(props=[NOTES_CC, SCRIPT, REMOTE]),
            outport(props=[SCRIPT, REMOTE]),
        ],
    }


def create_instance(c_instance):
    return StudioKEYS(c_instance)
